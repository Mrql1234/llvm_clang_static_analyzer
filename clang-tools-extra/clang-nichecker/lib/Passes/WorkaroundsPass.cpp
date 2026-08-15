#include "clang-nichecker/Passes/WorkaroundsPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace llvm;

namespace clang::nichecker {
namespace {

class WorkaroundsVisitor : public RecursiveASTVisitor<WorkaroundsVisitor> {
public:
  explicit WorkaroundsVisitor(ASTContext &Context) : Context(Context) {}

  bool VisitIfStmt(IfStmt *Statement) {
    if (!isMainFileLocation(Statement->getBeginLoc(), Context.getSourceManager()) ||
        !isConstantFalse(Statement->getCond()))
      return true;
    addReplacement(Statement, "");
    ++DeadIfs;
    return true;
  }

  bool VisitCallExpr(CallExpr *Call) {
    if (!isMainFileLocation(Call->getBeginLoc(), Context.getSourceManager()))
      return true;
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (Callee && Callee->getName() == "pthread_create" &&
        Call->getNumArgs() >= 3) {
      const Expr *StartRoutine = Call->getArg(2)->IgnoreParenImpCasts();
      if (const auto *Cast = dyn_cast<ExplicitCastExpr>(StartRoutine)) {
        auto Text = getSourceText(Cast->getSubExpr()->getSourceRange(),
                                  Context.getSourceManager(), Context.getLangOpts());
        if (Text) {
          addReplacement(Cast, *Text);
          ++PthreadCasts;
        }
      }
    }

    if (!Callee)
      return true;
    CompoundStmt *Compound = containingCompound(Call);
    if (!Compound)
      return true;
    if (Callee->getName() == "__VERIFIER_atomic_begin") {
      AtomicBeginSeen[Compound] = true;
    } else if (Callee->getName() == "__VERIFIER_atomic_end" &&
               !AtomicBeginSeen.lookup(Compound)) {
      addReplacement(Call, "__CSEQ_noop()");
      ++DisabledAtomicEnds;
    }
    return true;
  }

  bool VisitMemberExpr(MemberExpr *Member) {
    if (!Member->isArrow() ||
        !isMainFileLocation(Member->getBeginLoc(), Context.getSourceManager()))
      return true;
    auto Base = getSourceText(Member->getBase()->getSourceRange(),
                              Context.getSourceManager(), Context.getLangOpts());
    auto Field = getSourceText(Member->getMemberNameInfo().getSourceRange(),
                               Context.getSourceManager(), Context.getLangOpts());
    if (!Base || !Field)
      return true;
    addReplacement(Member, "(*" + *Base + ")." + *Field);
    ++ArrowDereferences;
    return true;
  }

  bool VisitDeclStmt(DeclStmt *Statement) {
    if (!isMainFileLocation(Statement->getBeginLoc(), Context.getSourceManager()) ||
        !Statement->isSingleDecl())
      return true;
    auto Text = getSourceText(Statement->getSourceRange(),
                              Context.getSourceManager(), Context.getLangOpts());
    if (!Text)
      return true;
    const size_t PrefixLength = storagePrefixLength(*Text);
    if (!PrefixLength)
      return true;
    auto Begin = getFileOffset(Statement->getBeginLoc(), Context.getSourceManager());
    if (!Begin)
      return true;
    // Keep the declaration's initializer intact so a nested MemberExpr can
    // still be lowered by its own source replacement.
    Replacements.push_back(TextReplacement{*Begin,
                                           static_cast<unsigned>(PrefixLength), ""});
    ++StrippedDeclarations;
    return true;
  }

  std::vector<TextReplacement> takeReplacements() {
    // Only retain non-overlapping transformations. An enclosing dead if has
    // precedence over source fragments discovered inside it.
    llvm::sort(Replacements, [](const TextReplacement &LHS,
                                const TextReplacement &RHS) {
      if (LHS.Offset != RHS.Offset)
        return LHS.Offset < RHS.Offset;
      return LHS.Length > RHS.Length;
    });
    std::vector<TextReplacement> Filtered;
    unsigned LastEnd = 0;
    for (TextReplacement &Replacement : Replacements) {
      if (!Filtered.empty() && Replacement.Offset < LastEnd)
        continue;
      LastEnd = Replacement.Offset + Replacement.Length;
      Filtered.push_back(std::move(Replacement));
    }
    return Filtered;
  }

  unsigned deadIfs() const { return DeadIfs; }
  unsigned pthreadCasts() const { return PthreadCasts; }
  unsigned arrowDereferences() const { return ArrowDereferences; }
  unsigned strippedDeclarations() const { return StrippedDeclarations; }
  unsigned disabledAtomicEnds() const { return DisabledAtomicEnds; }

private:
  bool isConstantFalse(const Expr *Expression) const {
    Expression = Expression->IgnoreParenImpCasts();
    if (const auto *Literal = dyn_cast<IntegerLiteral>(Expression))
      return Literal->getValue().isZero();
    const auto *Unary = dyn_cast<UnaryOperator>(Expression);
    if (!Unary || Unary->getOpcode() != UO_LNot)
      return false;
    const auto *Literal = dyn_cast<IntegerLiteral>(
        Unary->getSubExpr()->IgnoreParenImpCasts());
    return Literal && Literal->getValue() == 1;
  }

  CompoundStmt *containingCompound(const Stmt *Node) const {
    const Stmt *Current = Node;
    while (Current) {
      auto Parents = Context.getParents(*Current);
      if (Parents.empty())
        return nullptr;
      if (auto *Compound = const_cast<CompoundStmt *>(Parents[0].get<CompoundStmt>()))
        return Compound;
      Current = Parents[0].get<Stmt>();
    }
    return nullptr;
  }

  static size_t storagePrefixLength(StringRef Text) {
    StringRef Remaining = Text.ltrim();
    bool Changed = false;
    while (true) {
      StringRef Next = Remaining;
      for (StringRef Prefix : {"auto ", "inline ", "extern ", "volatile ",
                               "register "}) {
        if (Remaining.starts_with(Prefix)) {
          Next = Remaining.drop_front(Prefix.size()).ltrim();
          Changed = true;
          break;
        }
      }
      if (Next == Remaining)
        break;
      Remaining = Next;
    }
    return Changed ? Text.size() - Remaining.size() : 0;
  }

  void addReplacement(const Stmt *Statement, std::string Text) {
    auto Begin = getFileOffset(Statement->getBeginLoc(), Context.getSourceManager());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Statement->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (Begin && End && *End > *Begin)
      Replacements.push_back(TextReplacement{*Begin, *End - *Begin,
                                             std::move(Text)});
  }

  ASTContext &Context;
  DenseMap<const CompoundStmt *, bool> AtomicBeginSeen;
  std::vector<TextReplacement> Replacements;
  unsigned DeadIfs = 0;
  unsigned PthreadCasts = 0;
  unsigned ArrowDereferences = 0;
  unsigned StrippedDeclarations = 0;
  unsigned DisabledAtomicEnds = 0;
};

} // namespace

llvm::StringRef WorkaroundsPass::name() const { return "workarounds"; }

llvm::Error WorkaroundsPass::run(const PipelineContext &Context,
                                 TransformResult &Result) const {
  WorkaroundsVisitor Visitor(Context.getASTContext());
  Visitor.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Visitor.takeReplacements();
  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      formatv("phase1: workarounds 删除恒假 if={0}，去除 pthread cast={1}，"
              "展开 ->={2}，移除声明限定符={3}，禁用孤立 atomic_end={4}",
              Visitor.deadIfs(), Visitor.pthreadCasts(), Visitor.arrowDereferences(),
              Visitor.strippedDeclarations(), Visitor.disabledAtomicEnds())
          .str());
  return Error::success();
}

} // namespace clang::nichecker
