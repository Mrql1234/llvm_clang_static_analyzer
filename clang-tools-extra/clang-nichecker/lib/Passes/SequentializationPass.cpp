#include "clang-nichecker/Passes/SequentializationPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatVariadic.h"

#include <optional>
#include <string>
#include <vector>

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

struct CallableFunction {
  std::string Name;
  std::vector<std::string> DefaultArgs;
};

class SeqProgramCollector : public RecursiveASTVisitor<SeqProgramCollector> {
public:
  explicit SeqProgramCollector(ASTContext &Context)
      : Context(Context), SM(Context.getSourceManager()),
        LangOpts(Context.getLangOpts()) {}

  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (!FD->hasBody() || !isMainFileLocation(FD->getLocation(), SM))
      return true;

    StringRef Name = FD->getName();
    if (Name == "main")
      MainFunction = FD;
    else if (Name == "main_thread")
      MainThreadFunction = FD;
    else if (!FallbackMainFunction && Name.ends_with("_main"))
      FallbackMainFunction = FD;

    if (std::optional<CallableFunction> Callable = buildCallableFunction(FD))
      CallableFunctions.push_back(std::move(*Callable));
    return true;
  }

  bool VisitWhileStmt(WhileStmt *WS) {
    if (!isMainFileLocation(WS->getWhileLoc(), SM))
      return true;
    if (isLiteralTrue(WS->getCond()))
      InfiniteLoops.push_back(WS);
    return true;
  }

  const FunctionDecl *MainFunction = nullptr;
  const FunctionDecl *MainThreadFunction = nullptr;
  const FunctionDecl *FallbackMainFunction = nullptr;
  std::vector<CallableFunction> CallableFunctions;
  std::vector<const WhileStmt *> InfiniteLoops;

private:
  static bool isLiteralTrue(const Expr *ExprNode) {
    if (!ExprNode)
      return false;
    ExprNode = ExprNode->IgnoreParenImpCasts();
    if (const auto *IL = dyn_cast<IntegerLiteral>(ExprNode))
      return IL->getValue() == 1;
    return false;
  }

  std::optional<CallableFunction> buildCallableFunction(FunctionDecl *FD) const {
    StringRef Name = FD->getName();
    if (Name.empty() || Name == "main" || Name == "main_thread" ||
        Name.starts_with("__")) {
      return std::nullopt;
    }

    CallableFunction Callable{FD->getNameAsString(), {}};
    for (const ParmVarDecl *Param : FD->parameters()) {
      std::optional<std::string> DefaultArg = buildDefaultArgument(Param);
      if (!DefaultArg)
        return std::nullopt;
      Callable.DefaultArgs.push_back(*DefaultArg);
    }

    return Callable;
  }

  std::optional<std::string>
  buildDefaultArgument(const ParmVarDecl *Param) const {
    QualType Type = Param->getType();
    if (Type->isPointerType() || Type->isArrayType() ||
        Type->isFunctionPointerType()) {
      return std::string("0");
    }
    if (Type->isIntegerType() || Type->isBooleanType() ||
        Type->isEnumeralType() || Type->isRealFloatingType()) {
      return std::string("0");
    }
    return std::nullopt;
  }

  ASTContext &Context;
  SourceManager &SM;
  const LangOptions &LangOpts;
};

static bool shouldRunSeqProgramMode(const PipelineOptions &Options,
                                    const TransformResult &Result) {
  if (Result.Summary.Kind != ProgramKind::Sequential)
    return false;

  if (Options.PipelineProfile == "shenfei")
    return true;

  StringRef Spec = Options.PipelineSpec;
  return Spec.contains("sequentialization") || Spec.contains("seq");
}

static std::string indentLines(StringRef Text, StringRef Prefix) {
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

static std::string trimTrailingWhitespace(StringRef Text) {
  return Text.rtrim(" \t\r\n").str();
}

static std::optional<TextReplacement>
buildFunctionRenameReplacement(const FunctionDecl *FD, StringRef NewName,
                               const SourceManager &SM,
                               const LangOptions &LangOpts) {
  std::optional<unsigned> Offset = getFileOffset(FD->getLocation(), SM);
  std::optional<unsigned> Length =
      getTokenLength(FD->getLocation(), SM, LangOpts);
  if (!Offset || !Length || *Length == 0)
    return std::nullopt;

  return TextReplacement{*Offset, *Length, NewName.str()};
}

static std::optional<TextReplacement>
buildInfiniteLoopReplacement(const WhileStmt *WS, const PipelineContext &Context,
                             unsigned LoopIndex) {
  const SourceManager &SM = Context.getSourceManager();
  const LangOptions &LangOpts = Context.getLangOpts();

  std::optional<unsigned> Offset = getFileOffset(WS->getBeginLoc(), SM);
  std::optional<std::string> LoopText =
      getSourceText(WS->getSourceRange(), SM, LangOpts);
  std::optional<std::string> BodyText =
      getSourceText(WS->getBody()->getSourceRange(), SM, LangOpts);
  if (!Offset || !LoopText || !BodyText)
    return std::nullopt;

  std::string Indent = getLineIndent(Context.CurrentSource, *Offset);
  std::string LoopCounter = formatv("__cs_seq_unwind_k_{0}", LoopIndex).str();
  std::string NormalizedBody = trimTrailingWhitespace(*BodyText);
  std::string WrappedBody;

  if (StringRef(NormalizedBody).starts_with("{")) {
    size_t BracePos = NormalizedBody.find('{');
    if (BracePos == std::string::npos)
      return std::nullopt;
    NormalizedBody.replace(BracePos, 1, "{\n    " + LoopCounter + "++;");
    WrappedBody = indentLines(NormalizedBody, Indent + "  ");
  } else {
    WrappedBody = indentLines(
        formatv("{{\n    {0}++;\n    {1}\n}}", LoopCounter, NormalizedBody)
            .str(),
        Indent + "  ");
  }

  std::string Replacement;
  raw_string_ostream OS(Replacement);
  OS << "{\n";
  OS << Indent << "  int " << LoopCounter << " = 0;\n";
  OS << Indent << "  while (" << LoopCounter << " < " << Context.Options.Unwind
     << ")\n";
  OS << WrappedBody << "\n";
  OS << Indent << "}";
  OS.flush();

  return TextReplacement{*Offset, static_cast<unsigned>(LoopText->size()),
                         Replacement};
}

static std::string joinArguments(const std::vector<std::string> &Arguments) {
  if (Arguments.empty())
    return "";

  std::string Output;
  raw_string_ostream OS(Output);
  for (size_t I = 0; I < Arguments.size(); ++I) {
    if (I)
      OS << ", ";
    OS << Arguments[I];
  }
  return OS.str();
}

static std::string buildSyntheticMainThread(
    const std::vector<CallableFunction> &CallableFunctions) {
  std::string Output;
  raw_string_ostream OS(Output);
  OS << "\nvoid main_thread(void)\n{\n";
  for (const CallableFunction &Callable : CallableFunctions) {
    OS << "  " << Callable.Name << "(" << joinArguments(Callable.DefaultArgs)
       << ");\n";
  }
  OS << "}\n";
  return OS.str();
}

static std::string buildWrapperMain(const std::vector<std::string> &Arguments) {
  std::string Output;
  raw_string_ostream OS(Output);
  OS << "\nint main(void)\n{\n";
  OS << "  main_thread(" << joinArguments(Arguments) << ");\n";
  OS << "  return 0;\n";
  OS << "}\n";
  return OS.str();
}

} // namespace

llvm::StringRef SequentializationPass::name() const {
  return "sequentialization";
}

llvm::Error SequentializationPass::run(const PipelineContext &Context,
                                       TransformResult &Result) const {
  if (!shouldRunSeqProgramMode(Context.Options, Result)) {
    Result.Notes.push_back(
        "phase5: sequentialization only runs for sequential shenfei/seq pipelines");
    return Error::success();
  }

  ASTContext &AST = Context.getASTContext();
  SeqProgramCollector Collector(AST);
  Collector.TraverseDecl(AST.getTranslationUnitDecl());

  std::vector<TextReplacement> CombinedReplacements = Result.PendingReplacements;
  std::vector<std::string> MainThreadArguments;
  bool AddedWrapperMain = false;
  bool AddedSyntheticMainThread = false;
  bool RenamedEntryFunction = false;

  const FunctionDecl *EntryFunction = nullptr;
  if (Collector.MainFunction && !Collector.MainThreadFunction) {
    EntryFunction = Collector.MainFunction;
  } else if (!Collector.MainFunction && !Collector.MainThreadFunction &&
             Collector.FallbackMainFunction) {
    EntryFunction = Collector.FallbackMainFunction;
  }

  if (EntryFunction) {
    if (std::optional<TextReplacement> Rename =
            buildFunctionRenameReplacement(EntryFunction, "main_thread",
                                           Context.getSourceManager(),
                                           Context.getLangOpts())) {
      CombinedReplacements.push_back(*Rename);
      RenamedEntryFunction = true;
      for (const ParmVarDecl *Param : EntryFunction->parameters()) {
        QualType Type = Param->getType();
        if (Type->isPointerType() || Type->isArrayType() ||
            Type->isFunctionPointerType() || Type->isIntegerType() ||
            Type->isBooleanType() || Type->isEnumeralType() ||
            Type->isRealFloatingType()) {
          MainThreadArguments.push_back("0");
        } else {
          Result.Notes.push_back(formatv(
              "phase5: entry function {0} has unsupported parameter types; wrapper main will not forward arguments",
              EntryFunction->getNameAsString())
                                     .str());
          MainThreadArguments.clear();
          break;
        }
      }
    }
  }

  unsigned RewrittenLoopCount = 0;
  for (const WhileStmt *WS : Collector.InfiniteLoops) {
    std::optional<TextReplacement> LoopRewrite =
        buildInfiniteLoopReplacement(WS, Context, RewrittenLoopCount);
    if (!LoopRewrite)
      continue;
    CombinedReplacements.push_back(*LoopRewrite);
    ++RewrittenLoopCount;
  }

  std::string WorkingSource = Context.CurrentSource.str();
  applyReplacements(WorkingSource, CombinedReplacements);

  if (RenamedEntryFunction || Collector.MainThreadFunction) {
    WorkingSource += buildWrapperMain(MainThreadArguments);
    AddedWrapperMain = true;
  } else if (!Collector.CallableFunctions.empty()) {
    WorkingSource += buildSyntheticMainThread(Collector.CallableFunctions);
    WorkingSource += buildWrapperMain({});
    AddedSyntheticMainThread = true;
    AddedWrapperMain = true;
  }

  Result.PendingReplacements.clear();
  Result.PendingReplacements.push_back(TextReplacement{
      0, static_cast<unsigned>(Context.CurrentSource.size()), WorkingSource});

  Result.Notes.push_back(
      formatv("phase5: sequentialization rewrote entry={0}, while(1)={1}",
              RenamedEntryFunction ? 1 : 0, RewrittenLoopCount)
          .str());
  if (AddedSyntheticMainThread) {
    Result.Notes.push_back(formatv(
                               "phase5: synthesized main_thread with {0} callable functions",
                               Collector.CallableFunctions.size())
                               .str());
  }
  if (AddedWrapperMain)
    Result.Notes.push_back("phase5: appended wrapper main");
  if (!AddedWrapperMain && !RenamedEntryFunction && RewrittenLoopCount == 0) {
    Result.Notes.push_back(
        "phase5: no sequentializable entry or infinite loop found; kept source structure unchanged");
  }
  return Error::success();
}

} // namespace clang::nichecker
