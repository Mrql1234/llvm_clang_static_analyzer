#include "clang-nichecker/Analysis/ProgramAnalyzer.h"
#include "clang-nichecker/Passes/ProgramClassifierPass.h"

using namespace clang;

namespace clang::nichecker {

llvm::StringRef ProgramClassifierPass::name() const {
  return "program-classifier";
}

llvm::Error ProgramClassifierPass::run(const PipelineContext &Context,
                                       TransformResult &Result) const {
  Result.Summary = analyzeProgram(Context.getASTContext());
  if (!Context.Options.InterruptConfigPath.empty()) {
    if (llvm::Error Error = applyInterruptConfig(
            Result.Summary, Context.Options.InterruptConfigPath))
      return Error;
  }
  Result.Source = Context.CurrentSource.str();
  Result.Notes.push_back("phase1: completed AST analysis and program classification");
  return llvm::Error::success();
}

} // namespace clang::nichecker
