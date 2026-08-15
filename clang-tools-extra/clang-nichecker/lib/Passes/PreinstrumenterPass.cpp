#include "clang-nichecker/Passes/PreinstrumenterPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

class PreinstrumenterVisitor
    : public RecursiveASTVisitor<PreinstrumenterVisitor> {
public:
  PreinstrumenterVisitor(ASTContext &Context, StringRef Source)
      : Context(Context), Source(Source) {
    StringSet<> Seen;
    for (const Decl *Decl : Context.getTranslationUnitDecl()->decls()) {
      const auto *Function = dyn_cast<FunctionDecl>(Decl);
      if (!Function || Function->getName().empty() ||
          !isMainFileLocation(Function->getLocation(), Context.getSourceManager()))
        continue;
      const FunctionDecl *Canonical = Function->getCanonicalDecl();
      if (Seen.insert(Canonical->getQualifiedNameAsString()).second)
        Functions.push_back(Canonical);
    }
  }

  bool TraverseLabelStmt(LabelStmt *Label) {
    if (!isErrorLabel(Label))
      return RecursiveASTVisitor<PreinstrumenterVisitor>::TraverseLabelStmt(Label);
    addReplacement(Label, "__VERIFIER_error();", true);
    ++ErrorSites;
    // Python discards the original labelled statement after lowering ERROR.
    return true;
  }

  bool VisitGotoStmt(GotoStmt *Goto) {
    if (!Goto->getLabel() || Goto->getLabel()->getName() != "ERROR")
      return true;
    addReplacement(Goto, "__VERIFIER_error()");
    ++ErrorSites;
    return true;
  }

  bool VisitCallExpr(CallExpr *Call) {
    if (Call->getDirectCallee() ||
        !isMainFileLocation(Call->getBeginLoc(), Context.getSourceManager()))
      return true;

    QualType TargetType = Call->getCallee()->getType();
    if (TargetType->isPointerType())
      TargetType = TargetType->getPointeeType();
    if (!TargetType->isFunctionType())
      return true;

    const Stmt *Statement = containingCompoundStatement(Call);
    if (!Statement)
      return true;
    const auto *Assignment = dyn_cast<BinaryOperator>(Statement);
    const bool IsExpressionStatement = Statement == Call;
    const bool IsDirectAssignment =
        Assignment && Assignment->isAssignmentOp() &&
        Assignment->getRHS()->IgnoreParenImpCasts() == Call;
    if (!IsExpressionStatement && !IsDirectAssignment)
      return true;

    std::vector<const FunctionDecl *> Candidates;
    for (const FunctionDecl *Function : Functions) {
      if (Context.hasSameType(Function->getType(), TargetType))
        Candidates.push_back(Function);
    }
    if (Candidates.empty())
      return true;

    auto Callee = getSourceText(Call->getCallee()->getSourceRange(),
                                Context.getSourceManager(), Context.getLangOpts());
    std::string Arguments;
    for (unsigned I = 0; I < Call->getNumArgs(); ++I) {
      auto Argument = getSourceText(Call->getArg(I)->getSourceRange(),
                                    Context.getSourceManager(), Context.getLangOpts());
      if (!Callee || !Argument)
        return true;
      if (I)
        Arguments += ", ";
      Arguments += *Argument;
    }

    std::string Left;
    std::string Operator;
    if (IsDirectAssignment) {
      auto LeftText = getSourceText(Assignment->getLHS()->getSourceRange(),
                                    Context.getSourceManager(), Context.getLangOpts());
      if (!LeftText)
        return true;
      Left = *LeftText + " ";
      Operator = " " + Assignment->getOpcodeStr().str() + " ";
    }

    std::string Dispatch;
    for (size_t I = 0; I < Candidates.size(); ++I) {
      Dispatch += I == 0 ? "if (" : " else if (";
      Dispatch += *Callee + " == " + Candidates[I]->getNameAsString() + ") { ";
      Dispatch += Left + Operator + Candidates[I]->getNameAsString() + "(" +
                  Arguments + "); }";
    }
    addReplacement(Statement, std::move(Dispatch));
    ++IndirectCalls;
    return true;
  }

  std::vector<TextReplacement> takeReplacements() {
    return std::move(Replacements);
  }
  unsigned errorSites() const { return ErrorSites; }
  unsigned indirectCalls() const { return IndirectCalls; }

private:
  bool isErrorLabel(const LabelStmt *Label) const {
    return StringRef(Label->getName()) == "ERROR" &&
           isMainFileLocation(Label->getBeginLoc(), Context.getSourceManager());
  }

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

  void addReplacement(const Stmt *Statement, std::string Text,
                      bool ConsumeTrailingSemicolon = false) {
    if (!isMainFileLocation(Statement->getBeginLoc(), Context.getSourceManager()))
      return;
    auto Begin = getFileOffset(Statement->getBeginLoc(), Context.getSourceManager());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Statement->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (!Begin || !End || *End <= *Begin)
      return;
    if (ConsumeTrailingSemicolon && *End < Source.size() && Source[*End] == ';')
      ++*End;
    Replacements.push_back(TextReplacement{*Begin, *End - *Begin, std::move(Text)});
  }

  ASTContext &Context;
  StringRef Source;
  std::vector<const FunctionDecl *> Functions;
  std::vector<TextReplacement> Replacements;
  unsigned ErrorSites = 0;
  unsigned IndirectCalls = 0;
};

} // namespace

llvm::StringRef PreinstrumenterPass::name() const { return "preinstrumenter"; }

llvm::Error PreinstrumenterPass::run(const PipelineContext &Context,
                                     TransformResult &Result) const {
  PreinstrumenterVisitor Visitor(Context.getASTContext(), Context.CurrentSource);
  Visitor.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Visitor.takeReplacements();
  if (!Replacements.empty())
    Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                      Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      formatv("phase3: preinstrumenter 降级 ERROR 位置={0}，展开函数指针调用={1}",
              Visitor.errorSites(), Visitor.indirectCalls())
          .str());
  return Error::success();
}

} // namespace clang::nichecker
