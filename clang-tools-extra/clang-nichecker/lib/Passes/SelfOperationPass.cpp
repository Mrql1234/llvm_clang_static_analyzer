#include "clang-nichecker/Passes/SelfOperationPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

class SelfOperationVisitor : public RecursiveASTVisitor<SelfOperationVisitor> {
public:
  explicit SelfOperationVisitor(ASTContext &Context) : Context(Context) {}

  bool VisitUnaryOperator(UnaryOperator *Op) {
    if (!Op->isIncrementDecrementOp() || !isStandaloneStatement(Op))
      return true;
    auto Operand = getSourceText(Op->getSubExpr()->getSourceRange(),
                                 Context.getSourceManager(), Context.getLangOpts());
    addReplacement(Op, Operand ? *Operand + (Op->isIncrementOp() ? " = " + *Operand + " + 1"
                                                                  : " = " + *Operand + " - 1")
                               : "");
    return true;
  }

  bool VisitCompoundAssignOperator(CompoundAssignOperator *Op) {
    std::string Operator = Op->getOpcodeStr().str();
    if (Operator != "+=" && Operator != "-=" && Operator != "*=" && Operator != "/=")
      return true;
    auto Left = getSourceText(Op->getLHS()->getSourceRange(), Context.getSourceManager(),
                              Context.getLangOpts());
    auto Right = getSourceText(Op->getRHS()->getSourceRange(), Context.getSourceManager(),
                               Context.getLangOpts());
    if (!Left || !Right)
      return true;
    addReplacement(Op, *Left + " = " + *Left + " " + Operator.substr(0, 1) + " " + *Right);
    return true;
  }

  std::vector<TextReplacement> takeReplacements() { return std::move(Replacements); }

private:
  bool isStandaloneStatement(const Stmt *Stmt) const {
    DynTypedNode Node = DynTypedNode::create(*Stmt);
    while (true) {
      auto Parents = Context.getParents(Node);
      if (Parents.empty())
        return false;
      const DynTypedNode Parent = Parents[0];
      if (Parent.get<CompoundStmt>())
        return true;
      if (Parent.get<ExprWithCleanups>() || Parent.get<ImplicitCastExpr>() ||
          Parent.get<ParenExpr>()) {
        Node = Parent;
        continue;
      }
      return false;
    }
  }

  void addReplacement(const Stmt *Stmt, std::string Text) {
    if (Text.empty() || !isMainFileLocation(Stmt->getBeginLoc(), Context.getSourceManager()))
      return;
    auto Begin = getFileOffset(Stmt->getBeginLoc(), Context.getSourceManager());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Stmt->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (!Begin || !End || *End <= *Begin)
      return;
    Replacements.push_back(TextReplacement{*Begin, *End - *Begin, std::move(Text)});
  }

  ASTContext &Context;
  std::vector<TextReplacement> Replacements;
};

} // namespace

llvm::StringRef SelfOperationPass::name() const { return "selfop"; }

llvm::Error SelfOperationPass::run(const PipelineContext &Context,
                                   TransformResult &Result) const {
  SelfOperationVisitor Visitor(Context.getASTContext());
  Visitor.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Visitor.takeReplacements();
  if (!Replacements.empty())
    Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                      Replacements.begin(), Replacements.end());
  Result.Notes.push_back(formatv("phase3: selfop 展开了 {0} 个自操作", Replacements.size()).str());
  return Error::success();
}

} // namespace clang::nichecker
