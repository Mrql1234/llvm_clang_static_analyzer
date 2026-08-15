#include "clang-nichecker/Passes/FunctionTrackerPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

class FunctionTrackerVisitor
    : public RecursiveASTVisitor<FunctionTrackerVisitor> {
public:
  explicit FunctionTrackerVisitor(ASTContext &Context) : Context(Context) {}

  bool TraverseFunctionDecl(FunctionDecl *Function) {
    const std::string Previous = CurrentFunction;
    if (Function->hasBody() &&
        isMainFileLocation(Function->getLocation(), Context.getSourceManager())) {
      CurrentFunction = Function->getNameAsString();
      if (Function->isMain())
        EntryLine = Context.getSourceManager().getSpellingLineNumber(
            Context.getSourceManager().getSpellingLoc(Function->getLocation()));
    }
    const bool Continue =
        RecursiveASTVisitor<FunctionTrackerVisitor>::TraverseFunctionDecl(Function);
    CurrentFunction = Previous;
    return Continue;
  }

  bool VisitStmt(Stmt *Statement) {
    if (CurrentFunction.empty() ||
        !isMainFileLocation(Statement->getBeginLoc(), Context.getSourceManager()))
      return true;
    const unsigned Line = Context.getSourceManager().getSpellingLineNumber(
        Context.getSourceManager().getSpellingLoc(Statement->getBeginLoc()));
    LineToFunction[Line] = CurrentFunction;
    return true;
  }

  bool VisitCallExpr(CallExpr *Call) {
    const Stmt *Statement = containingCompoundStatement(Call);
    if (Statement != Call ||
        !isMainFileLocation(Call->getBeginLoc(), Context.getSourceManager()))
      return true;

    std::string Suffix;
    for (const Expr *Argument : Call->arguments()) {
      const auto *Operation = dyn_cast<UnaryOperator>(Argument->IgnoreParenImpCasts());
      if (!Operation || !Operation->isPostfix() ||
          !Operation->isIncrementDecrementOp())
        continue;
      auto Operand = getSourceText(Operation->getSubExpr()->getSourceRange(),
                                   Context.getSourceManager(), Context.getLangOpts());
      auto OperationText = getSourceText(Operation->getSourceRange(),
                                         Context.getSourceManager(), Context.getLangOpts());
      if (!Operand || !OperationText)
        continue;
      addReplacement(Operation, *Operand);
      Suffix += "; " + *OperationText;
      ++SplitArguments;
    }
    if (Suffix.empty())
      return true;

    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Call->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    if (auto Offset = getFileOffset(EndLoc, Context.getSourceManager()))
      Replacements.push_back(TextReplacement{*Offset, 0, std::move(Suffix)});
    return true;
  }

  std::vector<TextReplacement> takeReplacements() { return std::move(Replacements); }
  const std::map<unsigned, std::string> &lineToFunction() const {
    return LineToFunction;
  }
  unsigned entryLine() const { return EntryLine; }
  unsigned splitArguments() const { return SplitArguments; }

private:
  const Stmt *containingCompoundStatement(const Stmt *Node) const {
    const Stmt *Current = Node;
    while (Current) {
      auto Parents = Context.getParents(*Current);
      if (Parents.empty())
        return nullptr;
      if (Parents[0].get<CompoundStmt>())
        return Current;
      Current = Parents[0].get<Stmt>();
    }
    return nullptr;
  }

  void addReplacement(const Stmt *Node, std::string Text) {
    auto Begin = getFileOffset(Node->getBeginLoc(), Context.getSourceManager());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Node->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (Begin && End && *End > *Begin)
      Replacements.push_back(TextReplacement{*Begin, *End - *Begin, std::move(Text)});
  }

  ASTContext &Context;
  std::string CurrentFunction;
  std::map<unsigned, std::string> LineToFunction;
  std::vector<TextReplacement> Replacements;
  unsigned EntryLine = 0;
  unsigned SplitArguments = 0;
};

} // namespace

llvm::StringRef FunctionTrackerPass::name() const { return "functiontracker"; }

llvm::Error FunctionTrackerPass::run(const PipelineContext &Context,
                                     TransformResult &Result) const {
  FunctionTrackerVisitor Visitor(Context.getASTContext());
  Visitor.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  Result.SourceLineFunctions = Visitor.lineToFunction();
  Result.EntryLine = Visitor.entryLine();
  std::vector<TextReplacement> Replacements = Visitor.takeReplacements();
  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      formatv("phase2: functiontracker 记录 {0} 行函数归属，拆分 {1} 个后缀自操作实参",
              Result.SourceLineFunctions.size(), Visitor.splitArguments())
          .str());
  return Error::success();
}

} // namespace clang::nichecker
