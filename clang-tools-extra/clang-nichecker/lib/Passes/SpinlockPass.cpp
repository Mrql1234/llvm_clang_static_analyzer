#include "clang-nichecker/Passes/SpinlockPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

class SideEffectFinder : public RecursiveASTVisitor<SideEffectFinder> {
public:
  bool VisitCallExpr(CallExpr *) { return mark(); }
  bool VisitBinaryOperator(BinaryOperator *Op) {
    if (Op->isAssignmentOp())
      return mark();
    return true;
  }
  bool VisitUnaryOperator(UnaryOperator *Op) {
    if (Op->isIncrementDecrementOp())
      return mark();
    return true;
  }
  bool found() const { return Found; }

private:
  bool mark() {
    Found = true;
    return false;
  }
  bool Found = false;
};

class SpinlockVisitor : public RecursiveASTVisitor<SpinlockVisitor> {
public:
  explicit SpinlockVisitor(ASTContext &Context) : Context(Context) {}

  bool VisitWhileStmt(WhileStmt *Stmt) {
    if (!isMainFileLocation(Stmt->getWhileLoc(), Context.getSourceManager()) ||
        !isEmptyBody(Stmt->getBody()))
      return true;
    SideEffectFinder Effects;
    Effects.TraverseStmt(Stmt->getCond());
    if (Effects.found())
      return true;

    auto Begin = getFileOffset(Stmt->getBeginLoc(), Context.getSourceManager());
    auto Cond = getSourceText(Stmt->getCond()->getSourceRange(),
                              Context.getSourceManager(), Context.getLangOpts());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Stmt->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (!Begin || !End || !Cond || *End <= *Begin)
      return true;
    Replacements.push_back(
        TextReplacement{*Begin, *End - *Begin, "__VERIFIER_assume(!(" + *Cond + "));"});
    return true;
  }

  std::vector<TextReplacement> takeReplacements() { return std::move(Replacements); }

private:
  static bool isEmptyBody(const Stmt *Stmt) {
    if (isa<NullStmt>(Stmt))
      return true;
    const auto *Compound = dyn_cast<CompoundStmt>(Stmt);
    return Compound && (Compound->body_empty() ||
                        (Compound->size() == 1 && isa<NullStmt>(*Compound->body_begin())));
  }

  ASTContext &Context;
  std::vector<TextReplacement> Replacements;
};

} // namespace

llvm::StringRef SpinlockPass::name() const { return "spinlock"; }

llvm::Error SpinlockPass::run(const PipelineContext &Context,
                              TransformResult &Result) const {
  SpinlockVisitor Visitor(Context.getASTContext());
  Visitor.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Visitor.takeReplacements();
  if (Replacements.empty()) {
    Result.Notes.push_back("phase3: spinlock 未发现可安全改写的空自旋循环");
    return Error::success();
  }
  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      formatv("phase3: spinlock 将 {0} 个空自旋循环改写为 assume", Replacements.size()).str());
  return Error::success();
}

} // namespace clang::nichecker
