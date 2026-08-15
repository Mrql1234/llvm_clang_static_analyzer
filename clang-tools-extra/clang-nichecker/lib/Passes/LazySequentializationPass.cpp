#include "clang-nichecker/Passes/LazySequentializationPass.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FormatVariadic.h"

#include <optional>
#include <string>
#include <vector>

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

struct ThreadPlan {
  const FunctionDecl *Function = nullptr;
  std::string Name;
  unsigned Index = 0;
  unsigned StatementCount = 0;
  bool IsMainThread = false;
};

bool isLazySeqRequested(const PipelineOptions &Options) {
  return Options.PipelineProfile == "lazy" ||
         StringRef(Options.PipelineSpec).contains("lazyseq");
}

std::optional<std::string> sourceText(const Stmt *Node,
                                      const PipelineContext &Context) {
  return getSourceText(Node->getSourceRange(), Context.getSourceManager(),
                       Context.getLangOpts());
}

std::optional<std::string> sourceText(const Expr *Node,
                                      const PipelineContext &Context) {
  return getSourceText(Node->getSourceRange(), Context.getSourceManager(),
                       Context.getLangOpts());
}

std::string indentLines(StringRef Text, StringRef Prefix) {
  std::string Output;
  raw_string_ostream OS(Output);
  size_t Start = 0;
  while (Start <= Text.size()) {
    size_t End = Text.find('\n', Start);
    StringRef Line =
        End == StringRef::npos ? Text.substr(Start) : Text.slice(Start, End);
    OS << Prefix << Line;
    if (End == StringRef::npos)
      break;
    OS << '\n';
    Start = End + 1;
  }
  return OS.str();
}

std::string ensureStatementTerminator(StringRef Text, const Stmt *Node) {
  std::string Result = Text.rtrim(" \t\r\n").str();
  if ((isa<Expr>(Node) || isa<DeclStmt>(Node) || isa<ReturnStmt>(Node) ||
       isa<BreakStmt>(Node) || isa<ContinueStmt>(Node) || isa<GotoStmt>(Node)) &&
      !Result.empty() && !StringRef(Result).ends_with(";")) {
    Result += ';';
  }
  return Result;
}

const CallExpr *getDirectCall(const Stmt *Node) {
  const auto *ExprNode = dyn_cast<Expr>(Node);
  if (!ExprNode)
    return nullptr;
  ExprNode = ExprNode->IgnoreParenImpCasts();
  return dyn_cast<CallExpr>(ExprNode);
}

const FunctionDecl *functionFromThreadStartArgument(const CallExpr *Call) {
  if (!Call || Call->getNumArgs() < 3)
    return nullptr;
  const Expr *ThreadStart = Call->getArg(2)->IgnoreParenImpCasts();
  while (const auto *Cast = dyn_cast<ExplicitCastExpr>(ThreadStart))
    ThreadStart = Cast->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *Address = dyn_cast<UnaryOperator>(ThreadStart)) {
    if (Address->getOpcode() == UO_AddrOf)
      ThreadStart = Address->getSubExpr()->IgnoreParenImpCasts();
  }
  const auto *Reference = dyn_cast<DeclRefExpr>(ThreadStart);
  return Reference ? dyn_cast<FunctionDecl>(Reference->getDecl()) : nullptr;
}

std::string buildDefaultArguments(const FunctionDecl *FD) {
  std::string Arguments;
  raw_string_ostream OS(Arguments);
  for (unsigned Index = 0; Index < FD->getNumParams(); ++Index) {
    if (Index)
      OS << ", ";
    OS << "0";
  }
  return OS.str();
}

std::optional<TextReplacement>
renameFunction(const FunctionDecl *FD, StringRef NewName,
               const PipelineContext &Context) {
  std::optional<unsigned> Offset =
      getFileOffset(FD->getLocation(), Context.getSourceManager());
  std::optional<unsigned> Length = getTokenLength(
      FD->getLocation(), Context.getSourceManager(), Context.getLangOpts());
  if (!Offset || !Length || !*Length)
    return std::nullopt;
  return TextReplacement{*Offset, *Length, NewName.str()};
}

std::string buildRuntimePrelude(const std::vector<ThreadPlan> &Plans) {
  std::string Lines;
  raw_string_ostream OS(Lines);
  const unsigned LockSlots = Plans.size() + 1;
  OS << "/* Native lazyseq runtime state. instrumenter adds CBMC bitwidths later. */\n";
  OS << "extern void __VERIFIER_assume(int);\n";
  OS << "static unsigned __cs_active_thread[" << Plans.size() << "] = {1};\n";
  OS << "static unsigned __cs_pc[" << Plans.size() << "] = {0};\n";
  OS << "static unsigned __cs_pc_cs[" << Plans.size() << "] = {0};\n";
  OS << "static unsigned __cs_thread_index = 0;\n";
  OS << "static void *__cs_threadargs[" << Plans.size() << "] = {0};\n";
  OS << "static const unsigned __cs_thread_lines[" << Plans.size() << "] = {";
  for (size_t Index = 0; Index < Plans.size(); ++Index) {
    if (Index)
      OS << ", ";
    OS << Plans[Index].StatementCount;
  }
  OS << "};\n";
  OS << "static void *__cs_lock_address[" << LockSlots << "] = {0};\n";
  OS << "static unsigned __cs_lock_owner[" << LockSlots << "] = {0};\n\n";
  OS << "#define __CS_LAZY_IF(T, A, B) \\\n";
  OS << "  if ((__cs_pc[(T)] > (A)) || ((A) >= __cs_pc_cs[(T)])) goto B\n";
  OS << "#define __CS_PTHREAD_CREATE(NewThread, Attr, Start, Arg, Id) \\\n";
  OS << "  do { *(NewThread) = (Id); __cs_active_thread[(Id)] = 1; \\\n";
  OS << "       __cs_threadargs[(Id)] = (void *)(Arg); } while (0)\n\n";
  OS << "static unsigned __cs_lock_slot(void *Address)\n{\n";
  OS << "  unsigned Index;\n";
  OS << "  for (Index = 0; Index < " << LockSlots << "; ++Index) {\n";
  OS << "    if (__cs_lock_address[Index] == Address || !__cs_lock_address[Index]) {\n";
  OS << "      __cs_lock_address[Index] = Address;\n";
  OS << "      return Index;\n    }\n  }\n";
  OS << "  return 0;\n}\n\n";
  OS << "static void __cs_pthread_mutex_init(void *Address)\n{\n";
  OS << "  __cs_lock_owner[__cs_lock_slot(Address)] = 0;\n}\n\n";
  OS << "static void __cs_pthread_mutex_lock(void *Address)\n{\n";
  OS << "  unsigned Slot = __cs_lock_slot(Address);\n";
  OS << "  __VERIFIER_assume(__cs_lock_owner[Slot] == 0);\n";
  OS << "  __cs_lock_owner[Slot] = __cs_thread_index + 1;\n}\n\n";
  OS << "static void __cs_pthread_mutex_unlock(void *Address)\n{\n";
  OS << "  unsigned Slot = __cs_lock_slot(Address);\n";
  OS << "  __VERIFIER_assume(__cs_lock_owner[Slot] == __cs_thread_index + 1);\n";
  OS << "  __cs_lock_owner[Slot] = 0;\n}\n\n";
  OS << "static void __cs_pthread_mutex_destroy(void *Address)\n{\n";
  OS << "  __cs_lock_owner[__cs_lock_slot(Address)] = 0;\n}\n\n";
  OS << "static void __cs_pthread_join(unsigned ThreadId)\n{\n";
  OS << "  __VERIFIER_assume(__cs_pc[ThreadId] == __cs_thread_lines[ThreadId]);\n}\n\n";
  return OS.str();
}

std::string buildScheduler(const std::vector<ThreadPlan> &Plans,
                           unsigned Rounds) {
  std::string Scheduler;
  raw_string_ostream OS(Scheduler);

  OS << "\nint main(void)\n{\n";
  for (unsigned Round = 0; Round < Rounds; ++Round) {
    OS << "  /* lazyseq round " << Round << " */\n";
    for (const ThreadPlan &Plan : Plans) {
      const std::string Temp =
          formatv("__cs_tmp_t{0}_r{1}", Plan.Index, Round).str();
      OS << "  {\n";
      OS << "    unsigned " << Temp << ";\n";
      if (!Plan.IsMainThread)
        OS << "    if (!__cs_active_thread[" << Plan.Index << "])\n      goto "
           << Temp << "_done;\n";
      OS << "    __cs_thread_index = " << Plan.Index << ";\n";
      OS << "    __cs_pc_cs[" << Plan.Index << "] = " << Temp << ";\n";
      if (Round == 0 && Plan.IsMainThread)
        OS << "    __VERIFIER_assume(__cs_pc_cs[" << Plan.Index
           << "] > 0);\n";
      else
        OS << "    __VERIFIER_assume(__cs_pc_cs[" << Plan.Index
           << "] >= __cs_pc[" << Plan.Index << "]);\n";
      OS << "    __VERIFIER_assume(__cs_pc_cs[" << Plan.Index << "] <= "
         << Plan.StatementCount << ");\n";
      if (Plan.IsMainThread)
        OS << "    main_thread(" << buildDefaultArguments(Plan.Function)
           << ");\n";
      else
        OS << "    " << Plan.Name << "(__cs_threadargs[" << Plan.Index
           << "]);\n";
      OS << "    __cs_pc[" << Plan.Index << "] = __cs_pc_cs["
         << Plan.Index << "];\n";
      if (!Plan.IsMainThread)
        OS << "  " << Temp << "_done:\n";
      OS << "    ;\n  }\n";
    }
  }
  OS << "  return 0;\n}\n";
  return OS.str();
}

class ThreadEntryCollector : public RecursiveASTVisitor<ThreadEntryCollector> {
public:
  explicit ThreadEntryCollector(ASTContext &AST)
      : AST(AST), SM(AST.getSourceManager()) {}

  bool VisitCallExpr(CallExpr *Call) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (!Callee || Callee->getName() != "pthread_create" ||
        !isMainFileLocation(Call->getBeginLoc(), SM))
      return true;
    if (const FunctionDecl *Entry = functionFromThreadStartArgument(Call))
      Entries.push_back(Entry);
    return true;
  }

  const std::vector<const FunctionDecl *> &entries() const { return Entries; }

private:
  ASTContext &AST;
  SourceManager &SM;
  std::vector<const FunctionDecl *> Entries;
};

class LazyPlanCollector {
public:
  LazyPlanCollector(ASTContext &AST, const ProgramSummary &Summary)
      : AST(AST), Summary(Summary), SM(AST.getSourceManager()) {}

  Expected<std::vector<ThreadPlan>> collect() const {
    StringMap<const FunctionDecl *> Definitions;
    for (Decl *DeclNode : AST.getTranslationUnitDecl()->decls()) {
      const auto *FD = dyn_cast<FunctionDecl>(DeclNode);
      if (!FD || !FD->hasBody() || !isMainFileLocation(FD->getLocation(), SM) ||
          FD->getName().empty())
        continue;
      Definitions[FD->getName()] = FD;
    }

    const FunctionDecl *Main = Summary.MainFunction;
    if (!Main || !Main->hasBody() || !isMainFileLocation(Main->getLocation(), SM)) {
      auto It = Definitions.find("main");
      Main = It == Definitions.end() ? nullptr : It->second;
    }
    if (!Main)
      return createStringError(inconvertibleErrorCode(),
                               "lazyseq 未找到 main 函数定义");

    std::vector<ThreadPlan> Plans;
    Plans.push_back(ThreadPlan{Main, Main->getNameAsString(), 0, 0, true});
    ThreadEntryCollector EntryCollector(AST);
    EntryCollector.TraverseDecl(AST.getTranslationUnitDecl());
    StringSet<> SeenEntries;
    for (const FunctionDecl *Entry : EntryCollector.entries()) {
      const FunctionDecl *Definition = Entry ? Entry->getDefinition() : nullptr;
      if (!Definition || Definition == Main ||
          !isMainFileLocation(Definition->getLocation(), SM))
        continue;
      const std::string Name = Definition->getNameAsString();
      // Duplicator makes every static create entry distinct. Keep the old
      // de-duplication fallback when lazyseq is invoked without that pass.
      if (!SeenEntries.insert(Name).second)
        continue;
      Plans.push_back(ThreadPlan{Definition, Name,
                                 static_cast<unsigned>(Plans.size()), 0, false});
    }
    if (Plans.size() == 1) {
      return createStringError(inconvertibleErrorCode(),
                               "lazyseq 未找到 pthread_create 对应的线程函数");
    }
    return Plans;
  }

private:
  ASTContext &AST;
  const ProgramSummary &Summary;
  SourceManager &SM;
};

std::string rewriteCreateCall(const CallExpr *Call, unsigned ThreadIndex,
                              const PipelineContext &Context) {
  std::optional<std::string> ThreadId = sourceText(Call->getArg(0), Context);
  std::optional<std::string> Attributes =
      Call->getNumArgs() >= 2 ? sourceText(Call->getArg(1), Context)
                              : std::optional<std::string>("0");
  std::optional<std::string> StartRoutine =
      Call->getNumArgs() >= 3 ? sourceText(Call->getArg(2), Context)
                              : std::optional<std::string>("0");
  std::optional<std::string> ThreadArg =
      Call->getNumArgs() >= 4 ? sourceText(Call->getArg(3), Context)
                              : std::optional<std::string>("0");
  if (!ThreadId || !Attributes || !StartRoutine || !ThreadArg)
    return "";
  return formatv("__CS_PTHREAD_CREATE({0}, {1}, {2}, {3}, {4});", *ThreadId,
                 *Attributes, *StartRoutine, *ThreadArg, ThreadIndex)
      .str();
}

std::string rewriteJoinCall(const CallExpr *Call, const PipelineContext &Context) {
  if (Call->getNumArgs() < 1)
    return "";
  std::optional<std::string> ThreadId = sourceText(Call->getArg(0), Context);
  if (!ThreadId)
    return "";
  return formatv("__cs_pthread_join((unsigned)({0}));", *ThreadId)
      .str();
}

std::string generatedLabelName(const ThreadPlan &Plan, unsigned StatementIndex) {
  return formatv("__cs_label_{0}_{1}", Plan.Name, StatementIndex).str();
}

std::string rewriteFunctionBody(
    const ThreadPlan &Plan, const StringMap<unsigned> &ThreadIndices,
    const PipelineContext &Context, unsigned &StatementCount) {
  const auto *Body = dyn_cast<CompoundStmt>(Plan.Function->getBody());
  if (!Body)
    return "";

  std::string HoistedDeclarations;
  for (const Stmt *Statement : Body->body()) {
    const auto *DeclStatement = dyn_cast<DeclStmt>(Statement);
    if (!DeclStatement)
      continue;
    for (const clang::Decl *DeclNode : DeclStatement->decls()) {
      const auto *Variable = dyn_cast<VarDecl>(DeclNode);
      if (!Variable || Variable->getName().empty())
        continue;
      HoistedDeclarations +=
          "  static " + Variable->getType().getAsString() + " " +
          Variable->getNameAsString() + ";\n";
    }
  }

  std::string Output = "{\n" + HoistedDeclarations;
  unsigned StatementIndex = 0;
  for (const Stmt *Statement : Body->body()) {
    std::optional<std::string> Text = sourceText(Statement, Context);
    if (!Text)
      continue;

    std::string Rewritten = ensureStatementTerminator(*Text, Statement);
    if (const auto *DeclStatement = dyn_cast<DeclStmt>(Statement)) {
      Rewritten.clear();
      for (const clang::Decl *DeclNode : DeclStatement->decls()) {
        const auto *Variable = dyn_cast<VarDecl>(DeclNode);
        if (!Variable || !Variable->hasInit())
          continue;
        std::optional<std::string> Initializer =
            sourceText(Variable->getInit(), Context);
        if (!Initializer)
          continue;
        if (!Rewritten.empty())
          Rewritten += "\n";
        Rewritten += Variable->getNameAsString() + " = " + *Initializer + ";";
      }
    }

    if (const CallExpr *Call = getDirectCall(Statement)) {
      const FunctionDecl *Callee = Call->getDirectCallee();
      StringRef CalleeName = Callee ? Callee->getName() : "";
      if (CalleeName == "pthread_create") {
        const FunctionDecl *Entry = functionFromThreadStartArgument(Call);
        auto It = Entry ? ThreadIndices.find(Entry->getName()) : ThreadIndices.end();
        if (It != ThreadIndices.end())
          Rewritten = rewriteCreateCall(Call, It->second, Context);
      } else if (CalleeName == "pthread_join") {
        Rewritten = rewriteJoinCall(Call, Context);
      } else if (CalleeName == "pthread_mutex_init") {
        if (Call->getNumArgs() >= 1) {
          if (std::optional<std::string> Address = sourceText(Call->getArg(0), Context))
            Rewritten = "__cs_pthread_mutex_init((void *)" + *Address + ");";
        }
      } else if (CalleeName == "pthread_mutex_lock") {
        if (Call->getNumArgs() >= 1) {
          if (std::optional<std::string> Address = sourceText(Call->getArg(0), Context))
            Rewritten = "__cs_pthread_mutex_lock((void *)" + *Address + ");";
        }
      } else if (CalleeName == "pthread_mutex_unlock") {
        if (Call->getNumArgs() >= 1) {
          if (std::optional<std::string> Address = sourceText(Call->getArg(0), Context))
            Rewritten = "__cs_pthread_mutex_unlock((void *)" + *Address + ");";
        }
      } else if (CalleeName == "pthread_mutex_destroy") {
        if (Call->getNumArgs() >= 1) {
          if (std::optional<std::string> Address = sourceText(Call->getArg(0), Context))
            Rewritten = "__cs_pthread_mutex_destroy((void *)" + *Address + ");";
        }
      } else if (CalleeName == "pthread_exit") {
        Rewritten = "return;";
      }
    }

    if (Rewritten.empty())
      continue;
    Output += formatv("  __CS_LAZY_IF({0}, {1}, {2});\n", Plan.Index,
                      StatementIndex,
                      generatedLabelName(Plan, StatementIndex + 1))
                  .str();
    Output += indentLines(Rewritten, "  ");
    Output += "\n" + generatedLabelName(Plan, StatementIndex + 1) + ":\n  ;\n";
    ++StatementIndex;
  }
  Output += "}\n";
  StatementCount = StatementIndex;
  return Output;
}

} // namespace

llvm::StringRef LazySequentializationPass::name() const { return "lazyseq"; }

llvm::Error LazySequentializationPass::run(const PipelineContext &Context,
                                           TransformResult &Result) const {
  if (!isLazySeqRequested(Context.Options))
    return Error::success();
  if (Result.Summary.Kind == ProgramKind::Sequential) {
    Result.Notes.push_back("phase5: lazyseq 跳过顺序输入");
    return Error::success();
  }

  LazyPlanCollector Collector(Context.getASTContext(), Result.Summary);
  Expected<std::vector<ThreadPlan>> PlansOrErr = Collector.collect();
  if (!PlansOrErr)
    return PlansOrErr.takeError();
  std::vector<ThreadPlan> Plans = std::move(*PlansOrErr);

  StringMap<unsigned> ThreadIndices;
  for (const ThreadPlan &Plan : Plans)
    ThreadIndices[Plan.Name] = Plan.Index;

  std::vector<TextReplacement> Replacements;
  for (ThreadPlan &Plan : Plans) {
    std::optional<unsigned> BodyOffset =
        getFileOffset(Plan.Function->getBody()->getBeginLoc(),
                      Context.getSourceManager());
    std::optional<std::string> BodyText = sourceText(Plan.Function->getBody(), Context);
    if (!BodyOffset || !BodyText)
      continue;

    unsigned StatementCount = 0;
    std::string RewrittenBody =
        rewriteFunctionBody(Plan, ThreadIndices, Context, StatementCount);
    if (RewrittenBody.empty())
      continue;
    Plan.StatementCount = StatementCount;
    Replacements.push_back(TextReplacement{*BodyOffset,
                                           static_cast<unsigned>(BodyText->size()),
                                           std::move(RewrittenBody)});
  }

  if (Replacements.size() != Plans.size()) {
    return createStringError(inconvertibleErrorCode(),
                             "lazyseq 无法读取所有目标函数的函数体");
  }
  if (std::optional<TextReplacement> Rename =
          renameFunction(Plans.front().Function, "main_thread", Context))
    Replacements.push_back(*Rename);

  std::string Source = materializeSource(Context, Result);
  applyReplacements(Source, Replacements);
  Result.Source = buildRuntimePrelude(Plans) + Source +
                  buildScheduler(Plans, Context.Options.Rounds);
  Result.PendingReplacements.clear();
  Result.Notes.push_back(
      formatv("phase5: native lazyseq 重写了 {0} 个线程函数并生成 {1} 轮调度器",
              Plans.size(), Context.Options.Rounds)
          .str());
  if (Result.Summary.Kind == ProgramKind::InterruptDriven) {
    Result.Notes.push_back(
        "phase5: 当前 native lazyseq 仅调度 pthread_create 入口；ISR 优先级与约束尚未迁移");
  }
  return Error::success();
}

} // namespace clang::nichecker
