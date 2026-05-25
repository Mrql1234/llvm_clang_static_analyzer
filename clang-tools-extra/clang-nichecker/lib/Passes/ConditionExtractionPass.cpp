#include "clang-nichecker/Passes/ConditionExtractionPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;

namespace clang::nichecker {

class IfConditionExtractor : public RecursiveASTVisitor<IfConditionExtractor> {
public:
  explicit IfConditionExtractor(ASTContext &Context, llvm::StringRef SourceText)
      : Context(Context), SM(Context.getSourceManager()),
        LangOpts(Context.getLangOpts()), SourceText(SourceText) {}

  bool VisitIfStmt(IfStmt *Stmt) {
    if (!Stmt->getCond())
      return true;
    if (!isMainFileLocation(Stmt->getIfLoc(), SM))
      return true;

    std::optional<std::string> CondText =
        getSourceText(Stmt->getCond()->getSourceRange(), SM, LangOpts);
    std::optional<unsigned> CondOffset =
        getFileOffset(Stmt->getCond()->getBeginLoc(), SM);
    std::optional<unsigned> CondEndOffset = getFileOffset(
        Lexer::getLocForEndOfToken(Stmt->getCond()->getEndLoc(), 0, SM, LangOpts),
        SM);
    std::optional<unsigned> IfOffset = getFileOffset(Stmt->getIfLoc(), SM);
    if (!CondText || !CondOffset || !CondEndOffset || !IfOffset ||
        *CondOffset > *CondEndOffset)
      return true;

    if (containsFunctionScopedReference(Stmt->getCond()))
      return true;

    std::string TempName =
        llvm::formatv("__cs_tmp_if_cond_{0}", ExtractedConditionCount).str();
    std::string BoolKeyword = LangOpts.CPlusPlus ? "bool" : "_Bool";
    std::string Indent = getLineIndent(SourceText, *IfOffset);
    unsigned InsertionOffset = getLineStartOffset(SourceText, *IfOffset);

    Replacements.push_back(
        TextReplacement{*CondOffset, *CondEndOffset - *CondOffset, TempName});
    Replacements.push_back(TextReplacement{
        InsertionOffset, 0,
        llvm::formatv("{0}{1} {2} = ({3});\n", Indent, BoolKeyword, TempName,
                      *CondText)
            .str()});
    ++ExtractedConditionCount;
    return true;
  }

  std::vector<TextReplacement> takeReplacements() {
    return std::move(Replacements);
  }

  size_t extractedConditionCount() const { return ExtractedConditionCount; }

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

    bool found() const { return Found; }

  private:
    bool Found = false;
  };

  bool containsFunctionScopedReference(const Expr *Expression) const {
    if (!Expression)
      return false;

    LocalReferenceFinder Finder;
    Finder.TraverseStmt(const_cast<Expr *>(Expression));
    return Finder.found();
  }

  ASTContext &Context;
  SourceManager &SM;
  const LangOptions &LangOpts;
  llvm::StringRef SourceText;
  std::vector<TextReplacement> Replacements;
  size_t ExtractedConditionCount = 0;
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

  ASTContext &AST = Context.CI.getASTContext();
  IfConditionExtractor Extractor(AST, Context.OriginalSource);
  Extractor.TraverseDecl(AST.getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Extractor.takeReplacements();
  if (Replacements.empty()) {
    Result.Notes.push_back("phase4: 未发现需要抽取的 if 条件");
    return llvm::Error::success();
  }

  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      llvm::formatv("phase4: 已抽取 {0} 个 if 条件到临时变量",
                    Extractor.extractedConditionCount())
          .str());
  return llvm::Error::success();
}

} // namespace clang::nichecker
