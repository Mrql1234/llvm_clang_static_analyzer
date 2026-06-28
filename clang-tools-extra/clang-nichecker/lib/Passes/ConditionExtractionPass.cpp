#include "clang-nichecker/Passes/ConditionExtractionPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;

namespace clang::nichecker {

class ConditionExtractorVisitor
    : public RecursiveASTVisitor<ConditionExtractorVisitor> {
public:
  explicit ConditionExtractorVisitor(ASTContext &Context,
                                     llvm::StringRef SourceText)
      : Context(Context), SM(Context.getSourceManager()),
        LangOpts(Context.getLangOpts()), SourceText(SourceText) {}

  bool VisitIfStmt(IfStmt *Stmt) {
    return extractIfCondition(Stmt);
  }

  bool VisitWhileStmt(WhileStmt *Stmt) {
    return extractWhileCondition(Stmt);
  }

  bool VisitForStmt(ForStmt *Stmt) {
    return extractForCondition(Stmt);
  }

  std::vector<TextReplacement> takeReplacements() {
    return std::move(Replacements);
  }

  size_t extractedIfCount() const { return IfCount; }
  size_t extractedWhileCount() const { return WhileCount; }
  size_t extractedForCount() const { return ForCount; }

private:
  class LocalReferenceFinder
      : public RecursiveASTVisitor<LocalReferenceFinder> {
  public:
    bool VisitDeclRefExpr(DeclRefExpr *Expr) {
      if (isa<ParmVarDecl>(Expr->getDecl())) {
        Found = true;
        return false;
      }

      if (const auto *Var = dyn_cast<VarDecl>(Expr->getDecl())) {
        if (Var->isLocalVarDecl()) {
          Found = true;
          return false;
        }
      }

      return true;
    }

    bool VisitBreakStmt(BreakStmt *) {
      FoundComplexControlFlow = true;
      return false;
    }

    bool VisitContinueStmt(ContinueStmt *) {
      FoundComplexControlFlow = true;
      return false;
    }

    bool VisitGotoStmt(GotoStmt *) {
      FoundComplexControlFlow = true;
      return false;
    }

    bool foundLocalReference() const { return Found; }
    bool foundComplexControlFlow() const { return FoundComplexControlFlow; }

  private:
    bool Found = false;
    bool FoundComplexControlFlow = false;
  };

  bool containsFunctionScopedReference(const Expr *Expression) const {
    if (!Expression)
      return false;

    LocalReferenceFinder Finder;
    Finder.TraverseStmt(const_cast<Expr *>(Expression));
    return Finder.foundLocalReference();
  }

  bool containsComplexControlFlow(const Stmt *Statement) const {
    if (!Statement)
      return false;

    LocalReferenceFinder Finder;
    Finder.TraverseStmt(const_cast<Stmt *>(Statement));
    return Finder.foundComplexControlFlow();
  }

  std::optional<std::string> getConditionText(const Expr *Condition) const {
    if (!Condition)
      return std::nullopt;
    return getSourceText(Condition->getSourceRange(), SM, LangOpts);
  }

  std::optional<unsigned> getConditionBeginOffset(const Expr *Condition) const {
    if (!Condition)
      return std::nullopt;
    return getFileOffset(Condition->getBeginLoc(), SM);
  }

  std::optional<unsigned> getConditionEndOffset(const Expr *Condition) const {
    if (!Condition)
      return std::nullopt;
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Condition->getEndLoc(), 0, SM, LangOpts);
    return getFileOffset(EndLoc, SM);
  }

  std::string boolKeyword() const { return LangOpts.CPlusPlus ? "bool" : "_Bool"; }

  bool extractIfCondition(IfStmt *Stmt) {
    const Expr *Condition = Stmt->getCond();
    if (!Condition || !isMainFileLocation(Stmt->getIfLoc(), SM))
      return true;

    std::optional<std::string> CondText = getConditionText(Condition);
    std::optional<unsigned> CondOffset = getConditionBeginOffset(Condition);
    std::optional<unsigned> CondEndOffset = getConditionEndOffset(Condition);
    std::optional<unsigned> IfOffset = getFileOffset(Stmt->getIfLoc(), SM);
    if (!CondText || !CondOffset || !CondEndOffset || !IfOffset ||
        *CondOffset > *CondEndOffset) {
      return true;
    }

    if (containsFunctionScopedReference(Condition))
      return true;

    std::string TempName = llvm::formatv("__cs_tmp_if_cond_{0}", IfCount).str();
    std::string Indent = getLineIndent(SourceText, *IfOffset);
    unsigned InsertionOffset = getLineStartOffset(SourceText, *IfOffset);

    Replacements.push_back(
        TextReplacement{*CondOffset, *CondEndOffset - *CondOffset, TempName});
    Replacements.push_back(
        TextReplacement{InsertionOffset, 0,
                        llvm::formatv("{0}{1} {2} = ({3});\n", Indent,
                                      boolKeyword(), TempName, *CondText)
                            .str()});
    ++IfCount;
    return true;
  }

  bool extractWhileCondition(WhileStmt *Stmt) {
    const Expr *Condition = Stmt->getCond();
    if (!Condition || !isMainFileLocation(Stmt->getWhileLoc(), SM))
      return true;
    if (!isa<CompoundStmt>(Stmt->getBody()))
      return true;
    if (containsFunctionScopedReference(Condition) ||
        containsComplexControlFlow(Stmt->getBody())) {
      return true;
    }

    std::optional<std::string> CondText = getConditionText(Condition);
    std::optional<unsigned> CondOffset = getConditionBeginOffset(Condition);
    std::optional<unsigned> CondEndOffset = getConditionEndOffset(Condition);
    std::optional<unsigned> WhileOffset = getFileOffset(Stmt->getWhileLoc(), SM);
    if (!CondText || !CondOffset || !CondEndOffset || !WhileOffset ||
        *CondOffset > *CondEndOffset) {
      return true;
    }

    const auto *Body = cast<CompoundStmt>(Stmt->getBody());
    std::optional<unsigned> BodyCloseOffset = getFileOffset(Body->getRBracLoc(), SM);
    if (!BodyCloseOffset)
      return true;

    std::string TempName =
        llvm::formatv("__cs_tmp_while_cond_{0}", WhileCount).str();
    std::string Indent = getLineIndent(SourceText, *WhileOffset);
    unsigned InsertionOffset = getLineStartOffset(SourceText, *WhileOffset);
    std::string BodyIndent = getLineIndent(SourceText, *BodyCloseOffset);

    Replacements.push_back(
        TextReplacement{*CondOffset, *CondEndOffset - *CondOffset, TempName});
    Replacements.push_back(
        TextReplacement{InsertionOffset, 0,
                        llvm::formatv("{0}{1} {2} = ({3});\n", Indent,
                                      boolKeyword(), TempName, *CondText)
                            .str()});
    Replacements.push_back(
        TextReplacement{*BodyCloseOffset, 0,
                        llvm::formatv("{0}{1} = ({2});\n", BodyIndent, TempName,
                                      *CondText)
                            .str()});
    ++WhileCount;
    return true;
  }

  bool extractForCondition(ForStmt *Stmt) {
    const Expr *Condition = Stmt->getCond();
    const Expr *Next = Stmt->getInc();
    if (!Condition || !Next || !isMainFileLocation(Stmt->getForLoc(), SM))
      return true;
    if (containsFunctionScopedReference(Condition))
      return true;

    std::optional<std::string> CondText = getConditionText(Condition);
    std::optional<unsigned> CondOffset = getConditionBeginOffset(Condition);
    std::optional<unsigned> CondEndOffset = getConditionEndOffset(Condition);
    std::optional<unsigned> ForOffset = getFileOffset(Stmt->getForLoc(), SM);
    std::optional<std::string> NextText =
        getSourceText(Next->getSourceRange(), SM, LangOpts);
    std::optional<unsigned> NextOffset = getFileOffset(Next->getBeginLoc(), SM);
    std::optional<unsigned> NextEndOffset = getFileOffset(
        Lexer::getLocForEndOfToken(Next->getEndLoc(), 0, SM, LangOpts), SM);
    if (!CondText || !CondOffset || !CondEndOffset || !ForOffset || !NextText ||
        !NextOffset || !NextEndOffset || *CondOffset > *CondEndOffset ||
        *NextOffset > *NextEndOffset) {
      return true;
    }

    std::string TempName = llvm::formatv("__cs_tmp_for_cond_{0}", ForCount).str();
    std::string Indent = getLineIndent(SourceText, *ForOffset);
    unsigned InsertionOffset = getLineStartOffset(SourceText, *ForOffset);

    Replacements.push_back(
        TextReplacement{*CondOffset, *CondEndOffset - *CondOffset, TempName});
    Replacements.push_back(
        TextReplacement{*NextOffset, *NextEndOffset - *NextOffset,
                        llvm::formatv("{0}, {1} = ({2})", *NextText, TempName,
                                      *CondText)
                            .str()});
    Replacements.push_back(
        TextReplacement{InsertionOffset, 0,
                        llvm::formatv("{0}{1} {2} = ({3});\n", Indent,
                                      boolKeyword(), TempName, *CondText)
                            .str()});
    ++ForCount;
    return true;
  }

  ASTContext &Context;
  SourceManager &SM;
  const LangOptions &LangOpts;
  llvm::StringRef SourceText;
  std::vector<TextReplacement> Replacements;
  size_t IfCount = 0;
  size_t WhileCount = 0;
  size_t ForCount = 0;
};

llvm::StringRef ConditionExtractionPass::name() const {
  return "condition-extraction";
}

llvm::Error ConditionExtractionPass::run(const PipelineContext &Context,
                                         TransformResult &Result) const {
  if (Result.Summary.Kind == ProgramKind::InterruptDriven) {
    Result.Notes.push_back(
        "phase4: 中断输入暂时跳过条件抽取，避免与 main 降级重写产生重叠替换");
    return llvm::Error::success();
  }

  ASTContext &AST = Context.getASTContext();
  ConditionExtractorVisitor Extractor(AST, Context.CurrentSource);
  Extractor.TraverseDecl(AST.getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Extractor.takeReplacements();
  if (Replacements.empty()) {
    Result.Notes.push_back("phase4: 未发现需要抽取的 if/while/for 条件");
    return llvm::Error::success();
  }

  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      llvm::formatv("phase4: 条件抽取已完成 if={0}, while={1}, for={2}",
                    Extractor.extractedIfCount(),
                    Extractor.extractedWhileCount(),
                    Extractor.extractedForCount())
          .str());
  return llvm::Error::success();
}

} // namespace clang::nichecker
