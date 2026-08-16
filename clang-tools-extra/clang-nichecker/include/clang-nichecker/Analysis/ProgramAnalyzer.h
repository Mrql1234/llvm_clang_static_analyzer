#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_ANALYSIS_PROGRAMANALYZER_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_ANALYSIS_PROGRAMANALYZER_H

#include "clang-nichecker/Support/Types.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace clang::nichecker {

class ProgramAnalyzer : public RecursiveASTVisitor<ProgramAnalyzer> {
public:
  explicit ProgramAnalyzer(ASTContext &Context);

  bool VisitFunctionDecl(FunctionDecl *FD);
  bool VisitCallExpr(CallExpr *Call);

  ProgramSummary finalize();

private:
  ASTContext &Context;
  SourceManager &SM;
  ProgramSummary Summary;
};

ProgramSummary analyzeProgram(ASTContext &Context);
llvm::Error applyInterruptConfig(ProgramSummary &Summary,
                                 llvm::StringRef ConfigPath);
ProgramSummary refreshSummaryForCurrentAST(ASTContext &Context,
                                           const ProgramSummary &Previous);

} // namespace clang::nichecker

#endif
