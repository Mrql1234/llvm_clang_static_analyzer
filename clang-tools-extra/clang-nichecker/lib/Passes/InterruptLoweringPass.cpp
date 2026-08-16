#include "clang-nichecker/Passes/InterruptLoweringPass.h"
#include "clang-nichecker/Support/SourceUtils.h"

#include "llvm/ADT/StringSet.h"

#include <cctype>
#include <cstring>
#include <vector>

using namespace clang;

namespace clang::nichecker {

static std::string pthreadHandleType(const ASTContext &AST) {
  for (Decl *Node : AST.getTranslationUnitDecl()->decls()) {
    const auto *Function = dyn_cast<FunctionDecl>(Node);
    if (!Function || Function->getName() != "pthread_create" ||
        Function->getNumParams() == 0)
      continue;
    QualType ParameterType = Function->getParamDecl(0)->getType();
    if (const auto *Pointer = ParameterType->getAs<PointerType>())
      return Pointer->getPointeeType().getAsString();
  }
  // Old nichecker examples often provide only a function declaration. Keep
  // their source parseable until lazyseq replaces pthread_create.
  return "unsigned";
}

static const InterruptInfo *findInterruptInfo(llvm::StringRef Name,
                                              const ProgramSummary &Summary) {
  for (const InterruptInfo &Info : Summary.InterruptInfos)
    if (Info.Name == Name)
      return &Info;
  return nullptr;
}

static unsigned countInfiniteLoopStatements(llvm::StringRef Source) {
  unsigned Statements = 0;
  size_t SearchFrom = 0;
  while (SearchFrom < Source.size()) {
    size_t While = Source.find("while", SearchFrom);
    if (While == llvm::StringRef::npos)
      break;
    SearchFrom = While + strlen("while");
    size_t Cursor = While + strlen("while");
    while (Cursor < Source.size() && isspace(static_cast<unsigned char>(Source[Cursor])))
      ++Cursor;
    if (Cursor >= Source.size() || Source[Cursor++] != '(')
      continue;
    while (Cursor < Source.size() && isspace(static_cast<unsigned char>(Source[Cursor])))
      ++Cursor;
    if (Cursor >= Source.size() || Source[Cursor++] != '1')
      continue;
    while (Cursor < Source.size() && isspace(static_cast<unsigned char>(Source[Cursor])))
      ++Cursor;
    if (Cursor >= Source.size() || Source[Cursor++] != ')')
      continue;
    while (Cursor < Source.size() && isspace(static_cast<unsigned char>(Source[Cursor])))
      ++Cursor;
    if (Cursor >= Source.size() || Source[Cursor] != '{')
      continue;
    const size_t BodyBegin = ++Cursor;
    unsigned Depth = 1;
    while (Cursor < Source.size() && Depth) {
      if (Source[Cursor] == '{')
        ++Depth;
      else if (Source[Cursor] == '}')
        --Depth;
      ++Cursor;
    }
    if (Depth)
      break;
    Statements += Source.slice(BodyBegin, Cursor - 1).count(';');
    SearchFrom = Cursor;
  }
  return Statements;
}

llvm::StringRef InterruptLoweringPass::name() const {
  return "interrupt-lowering";
}

llvm::Error InterruptLoweringPass::run(const PipelineContext &Context,
                                       TransformResult &Result) const {
  if (Result.Summary.Kind != ProgramKind::InterruptDriven)
    return llvm::Error::success();

  if (Result.Summary.UsesPthreadCreate) {
    Result.Notes.push_back(
        "phase2: detected pthread_create in the current source; skipping duplicate interrupt lowering");
    return llvm::Error::success();
  }

  const FunctionDecl *Main = Result.Summary.MainFunction;
  if (!Main || !Main->hasBody()) {
    Result.Notes.push_back(
        "phase2: main definition was not found in the current AST; kept source unchanged");
    return llvm::Error::success();
  }

  const SourceManager &SM = Context.getSourceManager();
  const LangOptions &LangOpts = Context.getLangOpts();

  std::optional<std::string> MainBodyText =
      getSourceText(Main->getBody()->getSourceRange(), SM, LangOpts);
  if (!MainBodyText) {
    Result.Notes.push_back(
        "phase2: failed to extract the main body text; kept source unchanged");
    return llvm::Error::success();
  }

  std::optional<unsigned> BeginOffset = getFileOffset(Main->getBeginLoc(), SM);
  SourceLocation EndLoc =
      Lexer::getLocForEndOfToken(Main->getEndLoc(), 0, SM, LangOpts);
  std::optional<unsigned> EndOffset = getFileOffset(EndLoc, SM);
  if (!BeginOffset || !EndOffset || *BeginOffset > *EndOffset ||
      *EndOffset > Context.CurrentSource.size()) {
    Result.Notes.push_back(
        "phase2: failed to locate the main definition range in the current source; kept source unchanged");
    return llvm::Error::success();
  }

  std::string Replacement;
  llvm::raw_string_ostream OS(Replacement);
  const std::string PthreadHandleType = pthreadHandleType(Context.getASTContext());
  const unsigned InfiniteLoopStatements =
      countInfiniteLoopStatements(*MainBodyText);
  std::vector<std::string> InterruptStarts;
  for (const std::string &Name : Result.Summary.InterruptFunctions) {
    const InterruptInfo *Info = findInterruptInfo(Name, Result.Summary);
    unsigned Instances = 1;
    if (Info && Info->Kind == "event")
      Instances = Context.Options.Unwind;
    else if (Info && Info->Kind == "timer")
      Instances = Info->Period
                      ? Context.Options.Unwind * InfiniteLoopStatements /
                            Info->Period
                      : 0;
    for (unsigned Instance = 0; Instance < Instances; ++Instance)
      InterruptStarts.push_back(Name);
  }
  OS << "void *main_task(void *__cs_param_main_task_arg)\n"
     << *MainBodyText << "\n\n";
  OS << "int main()\n{\n";
  OS << "  " << PthreadHandleType << " __cs_local_main_t0";
  for (size_t I = 0; I < InterruptStarts.size(); ++I)
    OS << ", __cs_local_isr_t" << I;
  OS << ";\n";
  OS << "  pthread_create(&__cs_local_main_t0, 0, main_task, 0);\n";
  for (size_t I = 0; I < InterruptStarts.size(); ++I)
    OS << "  pthread_create(&__cs_local_isr_t" << I << ", 0, "
       << InterruptStarts[I] << ", 0);\n";
  OS << "  return 0;\n";
  OS << "}\n";
  OS.flush();

  Result.PendingReplacements.push_back(
      TextReplacement{*BeginOffset, *EndOffset - *BeginOffset, Replacement});
  llvm::StringSet<> InterruptNames;
  for (const std::string &Name : Result.Summary.InterruptFunctions)
    InterruptNames.insert(Name);
  for (Decl *Node : Context.getASTContext().getTranslationUnitDecl()->decls()) {
    const auto *Function = dyn_cast<FunctionDecl>(Node);
    if (!Function || !InterruptNames.contains(Function->getName()) ||
        !isMainFileLocation(Function->getBeginLoc(), SM))
      continue;
    std::optional<unsigned> FunctionBegin =
        getFileOffset(Function->getBeginLoc(), SM);
    if (!FunctionBegin)
      continue;
    const std::string Header = "void *" + Function->getNameAsString() +
                               "(void *__cs_param_" +
                               Function->getNameAsString() + "_arg)";
    if (!Function->doesThisDeclarationHaveABody()) {
      std::optional<std::string> Declaration =
          getSourceText(Function->getSourceRange(), SM, LangOpts);
      if (Declaration)
        Result.PendingReplacements.push_back(TextReplacement{
            *FunctionBegin, static_cast<unsigned>(Declaration->size()),
            Header});
      continue;
    }
    std::optional<unsigned> BodyBegin =
        getFileOffset(Function->getBody()->getBeginLoc(), SM);
    if (BodyBegin && *BodyBegin >= *FunctionBegin)
      Result.PendingReplacements.push_back(
          TextReplacement{*FunctionBegin, *BodyBegin - *FunctionBegin, Header + "\n"});
  }
  Result.Notes.push_back(
      "phase2: lowered main and ISR entries into pthread-compatible lazy threads");
  return llvm::Error::success();
}

} // namespace clang::nichecker
