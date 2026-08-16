#include "clang-nichecker/Passes/InstrumenterPass.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatVariadic.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

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

} // namespace

llvm::StringRef InstrumenterPass::name() const { return "instrumenter"; }

llvm::Error InstrumenterPass::run(const PipelineContext &Context,
                                  TransformResult &Result) const {
  if (!shouldRunInstrumenter(Context.Options))
    return Error::success();

  std::string Source = materializeRawLines(materializeSource(Context, Result));
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
  return Error::success();
}

} // namespace clang::nichecker
