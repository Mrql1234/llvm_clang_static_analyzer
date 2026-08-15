#include "clang-nichecker/Passes/InlinerPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace llvm;

namespace clang::nichecker {
namespace {

struct CallSite {
  const Stmt *Statement = nullptr;
  const CallExpr *Call = nullptr;
  const FunctionDecl *Callee = nullptr;
  enum Kind { Expression, Assignment, Return } Kind = Expression;
  const Expr *AssignmentLHS = nullptr;
};

class InlinerVisitor : public RecursiveASTVisitor<InlinerVisitor> {
public:
  explicit InlinerVisitor(ASTContext &Context) : Context(Context) {}

  bool VisitCallExpr(CallExpr *Call) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (!Callee || !Callee->hasBody() || Callee->isMain() ||
        Callee->isVariadic() || Callee->getName().starts_with("__VERIFIER_") ||
        Callee->getName().starts_with("__CSEQ_") ||
        !isMainFileLocation(Call->getBeginLoc(), Context.getSourceManager()))
      return true;
    const Stmt *Statement = containingCompoundStatement(Call);
    if (!Statement || handledByOuterCall(Call, Statement))
      return true;

    CallSite Site;
    Site.Statement = Statement;
    Site.Call = Call;
    Site.Callee = Callee->getDefinition();
    if (!Site.Callee)
      return true;
    if (Statement == Call) {
      Site.Kind = CallSite::Expression;
    } else if (const auto *Assignment = dyn_cast<BinaryOperator>(Statement)) {
      if (!Assignment->isAssignmentOp() ||
          Assignment->getRHS()->IgnoreParenImpCasts() != Call)
        return true;
      Site.Kind = CallSite::Assignment;
      Site.AssignmentLHS = Assignment->getLHS();
    } else if (const auto *Return = dyn_cast<ReturnStmt>(Statement)) {
      if (Return->getRetValue()->IgnoreParenImpCasts() != Call)
        return true;
      Site.Kind = CallSite::Return;
    } else {
      return true;
    }
    Sites.push_back(Site);
    return true;
  }

  std::vector<TextReplacement> build() {
    for (const CallSite &Site : Sites) {
      if (auto Replacement = buildSite(Site))
        Replacements.push_back(std::move(*Replacement));
    }
    return std::move(Replacements);
  }
  unsigned count() const { return Sites.size(); }

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

  bool handledByOuterCall(const CallExpr *Call, const Stmt *Statement) const {
    const Stmt *Current = Call;
    while (Current && Current != Statement) {
      auto Parents = Context.getParents(*Current);
      if (Parents.empty())
        return false;
      if (const auto *OuterCall = Parents[0].get<CallExpr>())
        return OuterCall != Call;
      Current = Parents[0].get<Stmt>();
    }
    return false;
  }

  std::optional<std::string> source(const Stmt *Node) const {
    return getSourceText(Node->getSourceRange(), Context.getSourceManager(),
                         Context.getLangOpts());
  }

  std::string instanceBody(const FunctionDecl *Callee, StringRef Suffix,
                           StringRef ReturnName) const {
    const auto *Body = dyn_cast<CompoundStmt>(Callee->getBody());
    if (!Body)
      return "";
    auto BodyText = source(Body);
    auto Begin = getFileOffset(Body->getBeginLoc(), Context.getSourceManager());
    if (!BodyText || !Begin || BodyText->size() < 2)
      return "";

    std::vector<TextReplacement> Changes;
    class BodyVisitor : public RecursiveASTVisitor<BodyVisitor> {
    public:
      BodyVisitor(ASTContext &Context, const FunctionDecl *Callee, unsigned Begin, StringRef Suffix,
                  StringRef ReturnName, bool ReturnsValue,
                  std::vector<TextReplacement> &Changes)
          : Context(Context), Callee(Callee), Begin(Begin), Suffix(Suffix), ReturnName(ReturnName),
            ReturnsValue(ReturnsValue), Changes(Changes) {}
      bool VisitReturnStmt(ReturnStmt *Return) {
        auto Offset = getFileOffset(Return->getBeginLoc(), Context.getSourceManager());
        auto End = getFileOffset(Lexer::getLocForEndOfToken(
            Return->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts()),
            Context.getSourceManager());
        if (!Offset || !End || *End <= *Offset || *Offset < Begin)
          return true;
        std::string Text;
        if (ReturnsValue && Return->getRetValue()) {
          auto Value = getSourceText(Return->getRetValue()->getSourceRange(),
                                     Context.getSourceManager(), Context.getLangOpts());
          if (!Value)
            return true;
          std::string RewrittenValue = *Value;
          for (const ParmVarDecl *Parameter : Callee->parameters()) {
            const std::string Name = Parameter->getNameAsString();
            size_t Pos = 0;
            while ((Pos = RewrittenValue.find(Name, Pos)) != std::string::npos) {
              RewrittenValue.replace(Pos, Name.size(), "__cs_param_" + Suffix.str() + "_" + Name);
              Pos += Suffix.size() + Name.size() + 12;
            }
          }
          Text = ReturnName.str() + " = (" + RewrittenValue + "); ";
        }
        Text = "{ " + Text + "goto __exit_" + Suffix.str() + "; }";
        Changes.push_back({*Offset - Begin, *End - *Offset, std::move(Text)});
        return true;
      }
      bool TraverseReturnStmt(ReturnStmt *Return) { VisitReturnStmt(Return); return true; }
      bool VisitDeclRefExpr(DeclRefExpr *Reference) {
        const auto *Parameter = dyn_cast<ParmVarDecl>(Reference->getDecl());
        if (!Parameter)
          return true;
        auto Offset = getFileOffset(Reference->getBeginLoc(), Context.getSourceManager());
        auto End = getFileOffset(Lexer::getLocForEndOfToken(
            Reference->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts()),
            Context.getSourceManager());
        if (!Offset || !End || *End <= *Offset || *Offset < Begin)
          return true;
        Changes.push_back({*Offset - Begin, *End - *Offset,
                           "__cs_param_" + Suffix.str() + "_" +
                               Parameter->getNameAsString()});
        return true;
      }
    private:
      ASTContext &Context; const FunctionDecl *Callee; unsigned Begin; StringRef Suffix; StringRef ReturnName;
      bool ReturnsValue; std::vector<TextReplacement> &Changes;
    } Visitor(Context, Callee, *Begin, Suffix, ReturnName, !Callee->getReturnType()->isVoidType(),
              Changes);
    Visitor.TraverseStmt(const_cast<Stmt *>(Callee->getBody()));
    // Keep braces to give every inline instance a distinct local scope.
    applyReplacements(*BodyText, std::move(Changes));
    const size_t Closing = BodyText->rfind('}');
    if (Closing == std::string::npos)
      return "";
    BodyText->insert(Closing, "__exit_" + Suffix.str() + ":;\n");
    return *BodyText;
  }

  std::optional<TextReplacement> buildSite(const CallSite &Site) {
    auto Begin = getFileOffset(Site.Statement->getBeginLoc(), Context.getSourceManager());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Site.Statement->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (!Begin || !End || *End <= *Begin)
      return std::nullopt;

    const unsigned Instance = NextInstance[Site.Callee->getCanonicalDecl()]++;
    const std::string Suffix = Site.Callee->getNameAsString() + "_" + std::to_string(Instance);
    const std::string ReturnName = "__cs_retval_" + Suffix;
    std::string Output = "{\n";
    for (unsigned I = 0; I < Site.Callee->getNumParams(); ++I) {
      if (I >= Site.Call->getNumArgs())
        return std::nullopt;
      auto Argument = source(Site.Call->getArg(I));
      if (!Argument)
        return std::nullopt;
      const ParmVarDecl *Parameter = Site.Callee->getParamDecl(I);
      Output += Parameter->getType().getAsString() + " __cs_param_" + Suffix + "_" +
                Parameter->getNameAsString() + " = (" + *Argument + ");\n";
    }
    const bool ReturnsValue = !Site.Callee->getReturnType()->isVoidType();
    if (ReturnsValue)
      Output += Site.Callee->getReturnType().getAsString() + " " + ReturnName + ";\n";
    std::string Body = instanceBody(Site.Callee, Suffix, ReturnName);
    if (Body.empty())
      return std::nullopt;
    Output += Body;
    if (Site.Kind == CallSite::Assignment && ReturnsValue) {
      auto LHS = source(Site.AssignmentLHS);
      if (!LHS)
        return std::nullopt;
      Output += *LHS + " = " + ReturnName + ";\n";
    } else if (Site.Kind == CallSite::Return && ReturnsValue) {
      Output += "return " + ReturnName + ";\n";
    }
    Output += "}";
    return TextReplacement{*Begin, *End - *Begin, std::move(Output)};
  }

  ASTContext &Context;
  std::vector<CallSite> Sites;
  DenseMap<const FunctionDecl *, unsigned> NextInstance;
  std::vector<TextReplacement> Replacements;
};
} // namespace

llvm::StringRef InlinerPass::name() const { return "inliner"; }
llvm::Error InlinerPass::run(const PipelineContext &Context,
                             TransformResult &Result) const {
  InlinerVisitor Visitor(Context.getASTContext());
  Visitor.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  auto Replacements = Visitor.build();
  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Notes.push_back(formatv("phase3: inliner 实例化了 {0} 个直接函数调用",
                                 Visitor.count()).str());
  return Error::success();
}
} // namespace clang::nichecker
