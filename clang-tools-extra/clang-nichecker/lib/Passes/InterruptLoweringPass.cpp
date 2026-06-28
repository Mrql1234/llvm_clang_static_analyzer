#include "clang-nichecker/Passes/InterruptLoweringPass.h"
#include "clang-nichecker/Support/SourceUtils.h"

using namespace clang;

namespace clang::nichecker {

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
  OS << "void *main_task(void *__cs_param_main_task_arg)\n"
     << *MainBodyText << "\n\n";
  OS << "int main()\n{\n";
  OS << "  pthread_t __cs_local_main_t0;\n";
  OS << "  pthread_create(&__cs_local_main_t0, 0, main_task, 0);\n";
  OS << "  return 0;\n";
  OS << "}\n";
  OS.flush();

  Result.PendingReplacements.push_back(
      TextReplacement{*BeginOffset, *EndOffset - *BeginOffset, Replacement});
  Result.Notes.push_back(
      "phase2: lowered the interrupt-style main entry into main_task + wrapper main");
  return llvm::Error::success();
}

} // namespace clang::nichecker
