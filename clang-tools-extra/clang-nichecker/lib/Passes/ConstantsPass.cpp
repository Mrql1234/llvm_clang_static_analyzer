#include "clang-nichecker/Passes/ConstantsPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace llvm;

namespace clang::nichecker {
namespace {

class ConstantsVisitor : public RecursiveASTVisitor<ConstantsVisitor> {
public:
  explicit ConstantsVisitor(ASTContext &Context) : Context(Context) {}

  bool VisitBinaryOperator(BinaryOperator *Operator) {
    if (!isMainFileLocation(Operator->getBeginLoc(), Context.getSourceManager()))
      return true;
    auto Parents = Context.getParents(*Operator);
    if (!Parents.empty() && Parents[0].get<BinaryOperator>())
      return true;
    std::optional<APInt> Value = evaluate(Operator);
    if (!Value)
      return true;
    auto Begin = getFileOffset(Operator->getBeginLoc(), Context.getSourceManager());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Operator->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (!Begin || !End || *End <= *Begin)
      return true;
    Replacements.push_back(
        TextReplacement{*Begin, *End - *Begin,
                        std::to_string(Value->getZExtValue())});
    ++Folded;
    return true;
  }

  std::vector<TextReplacement> takeReplacements() {
    return std::move(Replacements);
  }
  unsigned folded() const { return Folded; }

private:
  std::optional<APInt> evaluate(const Expr *Expression) const {
    Expression = Expression->IgnoreParenImpCasts();
    if (const auto *Literal = dyn_cast<IntegerLiteral>(Expression))
      return Literal->getValue();
    const auto *Operator = dyn_cast<BinaryOperator>(Expression);
    if (!Operator)
      return std::nullopt;
    auto LHS = evaluate(Operator->getLHS());
    auto RHS = evaluate(Operator->getRHS());
    if (!LHS || !RHS)
      return std::nullopt;
    switch (Operator->getOpcode()) {
    case BO_Add: return *LHS + *RHS;
    case BO_Sub: return *LHS - *RHS;
    case BO_Mul: return *LHS * *RHS;
    case BO_Div:
      if (RHS->isZero() || !LHS->urem(*RHS).isZero())
        return std::nullopt;
      return LHS->udiv(*RHS);
    default: return std::nullopt;
    }
  }
  ASTContext &Context;
  std::vector<TextReplacement> Replacements;
  unsigned Folded = 0;
};

} // namespace

llvm::StringRef ConstantsPass::name() const { return "constants"; }

llvm::Error ConstantsPass::run(const PipelineContext &Context,
                               TransformResult &Result) const {
  ConstantsVisitor Visitor(Context.getASTContext());
  Visitor.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Visitor.takeReplacements();
  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      formatv("phase4: constants 折叠了 {0} 个整数二元表达式", Visitor.folded()).str());
  return Error::success();
}

} // namespace clang::nichecker
