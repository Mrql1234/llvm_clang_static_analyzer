#include "clang-nichecker/Analysis/ProgramAnalyzer.h"
#include "clang-nichecker/Passes/ProgramClassifierPass.h"

using namespace clang;

namespace clang::nichecker {

llvm::StringRef ProgramClassifierPass::name() const {
  return "program-classifier";
}

llvm::Error ProgramClassifierPass::run(const PipelineContext &Context,
                                       TransformResult &Result) const {
  ASTContext &AST = Context.CI.getASTContext();
  ProgramAnalyzer Analyzer(AST);
  Analyzer.TraverseDecl(AST.getTranslationUnitDecl());
  Result.Summary = Analyzer.finalize();
  Result.Source = Context.OriginalSource.str();
  Result.Notes.push_back("phase1: 完成 AST 解析与程序分类");
  return llvm::Error::success();
}

} // namespace clang::nichecker
