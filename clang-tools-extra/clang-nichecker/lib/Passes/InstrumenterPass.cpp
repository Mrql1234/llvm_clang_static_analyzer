#include "clang-nichecker/Passes/InstrumenterPass.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatVariadic.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

bool shouldRunInstrumenter(const PipelineOptions &Options) {
  return Options.PipelineProfile == "lazy" ||
         StringRef(Options.PipelineSpec).contains("instrumenter");
}

unsigned bitWidth(unsigned Value) {
  unsigned Width = 1;
  while (Value > 1) {
    Value >>= 1;
    ++Width;
  }
  return Width;
}

std::vector<unsigned> parseThreadSizes(StringRef Source) {
  size_t Name = Source.find("__cs_thread_lines");
  if (Name == StringRef::npos)
    return {};

  size_t Equal = Source.find('=', Name);
  size_t OpenBrace = Source.find('{', Equal);
  if (Equal == StringRef::npos || OpenBrace == StringRef::npos)
    return {};
  size_t CloseBrace = Source.find('}', OpenBrace + 1);
  if (CloseBrace == StringRef::npos)
    return {};

  std::vector<unsigned> Sizes;
  StringRef Values = Source.slice(OpenBrace + 1, CloseBrace);
  SmallVector<StringRef, 8> Parts;
  Values.split(Parts, ',', -1, false);
  for (StringRef Part : Parts) {
    unsigned Value = 0;
    if (!Part.trim().getAsInteger(10, Value))
      Sizes.push_back(Value);
  }
  return Sizes;
}

std::string materializeRawLines(StringRef Source) {
  std::string Output;
  size_t Cursor = 0;
  constexpr StringRef Prefix = "__CSEQ_rawline(\"";
  while (Cursor < Source.size()) {
    size_t Start = Source.find(Prefix, Cursor);
    if (Start == StringRef::npos) {
      Output.append(Source.substr(Cursor));
      break;
    }
    Output.append(Source.slice(Cursor, Start));
    size_t StringStart = Start + Prefix.size();
    size_t EndQuote = StringStart;
    bool Escaped = false;
    for (; EndQuote < Source.size(); ++EndQuote) {
      char C = Source[EndQuote];
      if (C == '"' && !Escaped)
        break;
      Escaped = C == '\\' && !Escaped;
      if (C != '\\')
        Escaped = false;
    }
    if (EndQuote == Source.size() ||
        !Source.substr(EndQuote).starts_with("\");")) {
      Output.append(Source.substr(Start));
      break;
    }

    StringRef Raw = Source.slice(StringStart, EndQuote);
    for (size_t I = 0; I < Raw.size(); ++I) {
      if (Raw[I] == '\\' && I + 1 < Raw.size()) {
        char Next = Raw[++I];
        Output.push_back(Next == 'n' ? '\n' : Next);
      } else {
        Output.push_back(Raw[I]);
      }
    }
    Cursor = EndQuote + 3;
  }
  return Output;
}

void replaceAll(std::string &Source, StringRef From, StringRef To) {
  size_t Offset = 0;
  while ((Offset = Source.find(From, Offset)) != std::string::npos) {
    Source.replace(Offset, From.size(), To);
    Offset += To.size();
  }
}

void lowerControlVariable(std::string &Source, StringRef Name, unsigned Width) {
  size_t Cursor = 0;
  const std::string Needle = "unsigned " + Name.str();
  const std::string Replacement =
      "unsigned __CPROVER_bitvector[" + std::to_string(Width) + "] " +
      Name.str();
  while ((Cursor = Source.find(Needle, Cursor)) != std::string::npos) {
    Source.replace(Cursor, Needle.size(), Replacement);
    Cursor += Replacement.size();
  }
}

void lowerControlVariablesWithPrefix(std::string &Source, StringRef Prefix,
                                     unsigned Width) {
  const std::string Needle = "unsigned " + Prefix.str();
  const std::string Type =
      "unsigned __CPROVER_bitvector[" + std::to_string(Width) + "] ";
  size_t Cursor = 0;
  while ((Cursor = Source.find(Needle, Cursor)) != std::string::npos) {
    size_t NameEnd = Cursor + Needle.size();
    while (NameEnd < Source.size() &&
           (std::isalnum(static_cast<unsigned char>(Source[NameEnd])) ||
            Source[NameEnd] == '_'))
      ++NameEnd;
    const std::string Name =
        Source.substr(Cursor + StringRef("unsigned ").size(),
                      NameEnd - Cursor - StringRef("unsigned ").size());
    Source.replace(Cursor, NameEnd - Cursor, Type + Name);
    Cursor += Type.size() + Name.size();
  }
}

void lowerVerifierPrimitives(std::string &Source, StringRef Backend) {
  // Keep this table aligned with modules/instrumenter.py.  These names are
  // backend syntax, so this runs only after the final AST-reparse boundary.
  if (Backend != "cbmc" && Backend != "cbmc-ext" &&
      Backend != "cbmc-5.10" && Backend != "cbmc-svcomp2020")
    return;

  replaceAll(Source, "__VERIFIER_assume", "__CPROVER_assume");
  replaceAll(Source, "__VERIFIER_assertext", "__CPROVER_assert");
  replaceAll(Source, "__VERIFIER_assert", "assert");
  replaceAll(Source, "__VERIFIER_nondet_int", "nondet_int");
  replaceAll(Source, "__VERIFIER_nondet_uint", "nondet_uint");
  replaceAll(Source, "__VERIFIER_nondet_bool", "nondet_bool");
  replaceAll(Source, "__VERIFIER_nondet_char", "nondet_char");
  replaceAll(Source, "__VERIFIER_nondet_uchar", "nondet_uchar");
}

std::string cbmcExtraDeclarations() {
  return R"(/* Native equivalent of modules/cbmc_extra.c. */
#ifndef __CS_CBMC_EXTRA_DECLS
#define __CS_CBMC_EXTRA_DECLS
extern int nondet_int(void);
extern unsigned int nondet_uint(void);
extern _Bool nondet_bool(void);
extern char nondet_char(void);
extern unsigned char nondet_uchar(void);
#endif

)";
}

bool isIdentifier(StringRef Value) {
  if (Value.empty() ||
      !(std::isalpha(static_cast<unsigned char>(Value.front())) ||
        Value.front() == '_'))
    return false;
  return std::all_of(Value.begin() + 1, Value.end(), [](char C) {
    return std::isalnum(static_cast<unsigned char>(C)) || C == '_';
  });
}

unsigned sourceOffset(const SourceManager &SourceMgr, SourceLocation Location) {
  return SourceMgr.getFileOffset(SourceMgr.getSpellingLoc(Location));
}

unsigned statementEndOffset(StringRef Source, unsigned Start) {
  bool InString = false;
  bool InCharacter = false;
  bool Escaped = false;
  for (unsigned Index = Start; Index < Source.size(); ++Index) {
    const char C = Source[Index];
    if (Escaped) {
      Escaped = false;
      continue;
    }
    if (C == '\\' && (InString || InCharacter)) {
      Escaped = true;
      continue;
    }
    if (C == '"' && !InCharacter) {
      InString = !InString;
      continue;
    }
    if (C == '\'' && !InString) {
      InCharacter = !InCharacter;
      continue;
    }
    if (!InString && !InCharacter && C == ';')
      return Index + 1;
  }
  return 0;
}

class RwrAccessCollector
    : public RecursiveASTVisitor<RwrAccessCollector> {
public:
  RwrAccessCollector(const PipelineContext &Context, StringRef Source,
                     StringRef Variable)
      : Context(Context), Source(Source), Variable(Variable),
        SourceMgr(Context.getSourceManager()) {}

  bool VisitBinaryOperator(BinaryOperator *Operator) {
    if (!Operator->isAssignmentOp() || !isTargetLValue(Operator->getLHS()))
      return true;
    recordWrite(Operator->getLHS(), Operator->getEndLoc());
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *Operator) {
    if (!Operator->isIncrementDecrementOp() ||
        !isTargetLValue(Operator->getSubExpr()))
      return true;
    recordWrite(Operator->getSubExpr(), Operator->getEndLoc());
    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *Reference) {
    const auto *Declaration = dyn_cast<VarDecl>(Reference->getDecl());
    if (!Declaration || !Declaration->hasGlobalStorage() ||
        Declaration->getName() != Variable)
      return true;
    if (Type.empty())
      Type = Declaration->getType().getAsString();
    const unsigned Position =
        statementEndOffset(Source, sourceOffset(SourceMgr, Reference->getEndLoc()));
    if (Position != 0)
      ReadCandidates.emplace_back(sourceOffset(SourceMgr, Reference->getBeginLoc()),
                                  Position);
    return true;
  }

  std::vector<unsigned> readEnds() const {
    std::set<unsigned> Ends;
    for (const auto &[Begin, End] : ReadCandidates) {
      const bool IsWriteLValue = std::any_of(
          WriteRanges.begin(), WriteRanges.end(), [&](const auto &Range) {
            return Begin >= Range.first && Begin < Range.second;
          });
      if (!IsWriteLValue && WriteEnds.find(End) == WriteEnds.end())
        Ends.insert(End);
    }
    return std::vector<unsigned>(Ends.begin(), Ends.end());
  }

  const std::set<unsigned> &writeEnds() const { return WriteEnds; }
  std::vector<unsigned> writeBegins() const {
    std::set<unsigned> Begins;
    for (const auto &Range : WriteRanges)
      Begins.insert(Range.first);
    return std::vector<unsigned>(Begins.begin(), Begins.end());
  }
  StringRef type() const { return Type; }

private:
  bool isTargetLValue(const Expr *Expression) const {
    Expression = Expression->IgnoreParenImpCasts();
    const auto *Reference = dyn_cast<DeclRefExpr>(Expression);
    if (!Reference)
      return false;
    const auto *Declaration = dyn_cast<VarDecl>(Reference->getDecl());
    return Declaration && Declaration->hasGlobalStorage() &&
           Declaration->getName() == Variable;
  }

  void recordWrite(const Expr *LValue, SourceLocation EndLocation) {
    const unsigned Begin = sourceOffset(SourceMgr, LValue->getBeginLoc());
    const unsigned End = sourceOffset(SourceMgr, LValue->getEndLoc());
    WriteRanges.emplace_back(Begin, End);
    const unsigned StatementEnd = statementEndOffset(
        Source, sourceOffset(SourceMgr, EndLocation));
    if (StatementEnd != 0)
      WriteEnds.insert(StatementEnd);
  }

  const PipelineContext &Context;
  StringRef Source;
  StringRef Variable;
  const SourceManager &SourceMgr;
  std::string Type;
  std::vector<std::pair<unsigned, unsigned>> WriteRanges;
  std::vector<std::pair<unsigned, unsigned>> ReadCandidates;
  std::set<unsigned> WriteEnds;
};

struct RwrInstrumentation {
  unsigned Reads = 0;
  unsigned Writes = 0;
  std::string Type;
};

RwrInstrumentation instrumentRwrScalar(const PipelineContext &Context,
                                        std::string &Source,
                                        unsigned ThreadCount) {
  RwrInstrumentation Result;
  const StringRef Variable = Context.Options.SvpVariable;
  if (!isIdentifier(Variable) ||
      StringRef(Source).contains("__CS_SVP_RWR_INSTRUMENTED"))
    return Result;

  RwrAccessCollector Collector(Context, Source, Variable);
  Collector.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  const std::vector<unsigned> ReadEnds = Collector.readEnds();
  const std::set<unsigned> &WriteEnds = Collector.writeEnds();
  if (ReadEnds.empty() && WriteEnds.empty())
    return Result;

  Result.Type = Context.Options.SvpType.empty() ? Collector.type().str()
                                                : Context.Options.SvpType;
  if (Result.Type.empty())
    return Result;

  std::map<unsigned, std::string> Insertions;
  for (unsigned End : ReadEnds) {
    Insertions[End] += "\n  {\n    " + Result.Type +
                       " __cs_svp_rwr_read = " + Variable.str() +
                       ";\n    if (__cs_svp_rwr_seen[__cs_thread_index])\n"
                       "      assert(__cs_svp_rwr_read == "
                       "__cs_svp_rwr_last[__cs_thread_index]);\n"
                       "    __cs_svp_rwr_seen[__cs_thread_index] = 1;\n"
                       "    __cs_svp_rwr_last[__cs_thread_index] = "
                       "__cs_svp_rwr_read;\n  }\n";
    ++Result.Reads;
  }
  for (unsigned End : WriteEnds) {
    Insertions[End] +=
        "\n  __cs_svp_rwr_seen[__cs_thread_index] = 0;\n";
    ++Result.Writes;
  }
  for (auto It = Insertions.rbegin(); It != Insertions.rend(); ++It)
    Source.insert(It->first, It->second);

  Source = "#define __CS_SVP_RWR_INSTRUMENTED 1\n" + Result.Type +
           " __cs_svp_rwr_last[" + std::to_string(ThreadCount) +
           "];\nunsigned __cs_svp_rwr_seen[" +
           std::to_string(ThreadCount) + "];\n\n" + Source;
  return Result;
}

RwrInstrumentation instrumentRwwScalar(const PipelineContext &Context,
                                        std::string &Source,
                                        unsigned ThreadCount) {
  RwrInstrumentation Result;
  const StringRef Variable = Context.Options.SvpVariable;
  if (!isIdentifier(Variable) ||
      StringRef(Source).contains("__CS_SVP_RWW_INSTRUMENTED"))
    return Result;

  RwrAccessCollector Collector(Context, Source, Variable);
  Collector.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  const std::vector<unsigned> ReadEnds = Collector.readEnds();
  const std::vector<unsigned> WriteBegins = Collector.writeBegins();
  if (ReadEnds.empty() && WriteBegins.empty())
    return Result;

  Result.Type = Context.Options.SvpType.empty() ? Collector.type().str()
                                                : Context.Options.SvpType;
  if (Result.Type.empty())
    return Result;

  std::map<unsigned, std::string> Insertions;
  for (unsigned End : ReadEnds) {
    Insertions[End] += "\n  {\n    " + Result.Type +
                       " __cs_svp_rww_read = " + Variable.str() +
                       ";\n    __cs_svp_rww_seen[__cs_thread_index] = 1;\n"
                       "    __cs_svp_rww_last[__cs_thread_index] = "
                       "__cs_svp_rww_read;\n  }\n";
    ++Result.Reads;
  }
  for (unsigned Begin : WriteBegins) {
    Insertions[Begin] +=
        "if (__cs_svp_rww_seen[__cs_thread_index]) {\n"
        "    assert(" + Variable.str() +
        " == __cs_svp_rww_last[__cs_thread_index]);\n"
        "    __cs_svp_rww_seen[__cs_thread_index] = 0;\n"
        "  }\n  ";
    ++Result.Writes;
  }
  for (auto It = Insertions.rbegin(); It != Insertions.rend(); ++It)
    Source.insert(It->first, It->second);

  Source = "#define __CS_SVP_RWW_INSTRUMENTED 1\n" + Result.Type +
           " __cs_svp_rww_last[" + std::to_string(ThreadCount) +
           "];\nunsigned __cs_svp_rww_seen[" +
           std::to_string(ThreadCount) + "];\n\n" + Source;
  return Result;
}

} // namespace

llvm::StringRef InstrumenterPass::name() const { return "instrumenter"; }

llvm::Error InstrumenterPass::run(const PipelineContext &Context,
                                  TransformResult &Result) const {
  if (!shouldRunInstrumenter(Context.Options))
    return Error::success();

  std::string Source = materializeSource(Context, Result);
  std::vector<unsigned> ThreadSizes = parseThreadSizes(Source);
  if (ThreadSizes.empty()) {
    // Raw lines still belong to the backend-only representation even when a
    // caller provides no lazyseq thread metadata.
    Result.Source = std::move(Source);
    Result.PendingReplacements.clear();
    Result.RequiresASTReparse = false;
    Result.Notes.push_back(
        "phase6: instrumenter 未发现 lazyseq 线程大小元数据，保持源码不变");
    return Error::success();
  }

  RwrInstrumentation Svp;
  if (Context.Options.SvpMode == "rwr")
    Svp = instrumentRwrScalar(Context, Source,
                              static_cast<unsigned>(ThreadSizes.size()));
  if (Context.Options.SvpMode == "rww")
    Svp = instrumentRwwScalar(Context, Source,
                              static_cast<unsigned>(ThreadSizes.size()));
  Source = materializeRawLines(Source);

  unsigned MaxThreadSize =
      *std::max_element(ThreadSizes.begin(), ThreadSizes.end());
  const unsigned PcWidth = bitWidth(MaxThreadSize);
  const unsigned ThreadWidth = bitWidth(ThreadSizes.size());

  lowerVerifierPrimitives(Source, Context.Options.Backend);
  lowerControlVariable(Source, "__cs_active_thread", 1);
  lowerControlVariable(Source, "__cs_disable_thread", 1);
  lowerControlVariable(Source, "__cs_pc", PcWidth);
  lowerControlVariable(Source, "__cs_pc_cs", PcWidth);
  lowerControlVariable(Source, "__cs_thread_lines", PcWidth);
  lowerControlVariable(Source, "__cs_thread_index", ThreadWidth);
  lowerControlVariable(Source, "__cs_last_thread", ThreadWidth);
  lowerControlVariable(Source, "__cs_tid", ThreadWidth);
  lowerControlVariable(Source, "__cs_cs", PcWidth);
  // Python's no-robin scheduler uses bounded PC increments. The current
  // round-robin rewrite still has broader native label accounting, so keep
  // its temporary choices unsigned until that metadata is migrated.
  if (Context.Options.NoRoundRobin)
    lowerControlVariablesWithPrefix(Source, "__cs_tmp_t", PcWidth);
  lowerControlVariablesWithPrefix(Source, "__cs_run_t", 1);
  if (Context.Options.Backend == "cbmc" ||
      Context.Options.Backend == "cbmc-ext" ||
      Context.Options.Backend == "cbmc-5.10" ||
      Context.Options.Backend == "cbmc-svcomp2020")
    Source = cbmcExtraDeclarations() + Source;
  Source = "#define __CS_LAZY_INSTRUMENTED 1\n" + Source;

  Result.Source = std::move(Source);
  Result.PendingReplacements.clear();
  Result.RequiresASTReparse = false;
  Result.Notes.push_back(
      formatv("phase6: instrumenter 已映射 CBMC 原语、注入 nondet 声明，并降级控制变量 bitwidth(pc={0}, thread={1})",
              PcWidth, ThreadWidth)
          .str());
  if (Context.Options.SvpMode == "rwr") {
    if (Svp.Reads || Svp.Writes)
      Result.Notes.push_back(
          formatv("phase6: instrumenter 已插入 rwr 标量访问序监控（变量 {0}，读 {1} 处，写 {2} 处）",
                  Context.Options.SvpVariable, Svp.Reads, Svp.Writes)
              .str());
    else
      Result.Notes.push_back(
          "phase6: rwr 当前仅支持命名标量全局变量的直接读写；未发现可插桩访问");
  }
  if (Context.Options.SvpMode == "rww") {
    if (Svp.Reads || Svp.Writes)
      Result.Notes.push_back(
          formatv("phase6: instrumenter 已插入 rww 标量访问序监控（变量 {0}，读 {1} 处，写 {2} 处）",
                  Context.Options.SvpVariable, Svp.Reads, Svp.Writes)
              .str());
    else
      Result.Notes.push_back(
          "phase6: rww 当前仅支持命名标量全局变量的直接读写；未发现可插桩访问");
  }
  return Error::success();
}

} // namespace clang::nichecker
