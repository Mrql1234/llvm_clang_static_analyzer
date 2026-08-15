#include "clang-nichecker/Passes/DoWhileConverterPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

class DoWhileVisitor : public RecursiveASTVisitor<DoWhileVisitor> {
public:
  explicit DoWhileVisitor(ASTContext &Context) : Context(Context) {}

  bool VisitDoStmt(DoStmt *Stmt) {
    if (!isMainFileLocation(Stmt->getDoLoc(), Context.getSourceManager()))
      return true;
    return replaceDoWhile(Stmt);
  }

  bool VisitForStmt(ForStmt *Stmt) {
    if (!isMainFileLocation(Stmt->getForLoc(), Context.getSourceManager()) ||
        Stmt->getInit() || Stmt->getInc())
      return true;
    auto Begin = getFileOffset(Stmt->getBeginLoc(), Context.getSourceManager());
    auto Body = getSourceText(Stmt->getBody()->getSourceRange(),
                              Context.getSourceManager(), Context.getLangOpts());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Stmt->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (!Begin || !End || !Body || *End <= *Begin || overlaps(*Begin, *End))
      return true;
    std::string Cond = "1";
    if (Stmt->getCond()) {
      auto CondText = getSourceText(Stmt->getCond()->getSourceRange(),
                                    Context.getSourceManager(), Context.getLangOpts());
      if (!CondText)
        return true;
      Cond = *CondText;
    }
    Replacements.push_back(TextReplacement{*Begin, *End - *Begin,
                                            "while (" + Cond + ") " + *Body});
    ++ForCount;
    return true;
  }

  std::vector<TextReplacement> takeReplacements() { return std::move(Replacements); }
  unsigned doWhileCount() const { return DoWhileCount; }
  unsigned forCount() const { return ForCount; }

private:
  bool replaceDoWhile(DoStmt *Stmt) {
    auto Begin = getFileOffset(Stmt->getBeginLoc(), Context.getSourceManager());
    auto Body = getSourceText(Stmt->getBody()->getSourceRange(),
                              Context.getSourceManager(), Context.getLangOpts());
    auto Cond = getSourceText(Stmt->getCond()->getSourceRange(),
                              Context.getSourceManager(), Context.getLangOpts());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Stmt->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (!Begin || !End || !Body || !Cond || *End <= *Begin || overlaps(*Begin, *End))
      return true;
    const std::string Variable = "__cs_dowhile_onetime_" + std::to_string(++DoWhileCount);
    std::string Replacement = "{ int " + Variable + "; for (" + Variable +
                              " = 0; " + Variable + " < 1; " + Variable +
                              "++) " + *Body + " while (" + *Cond + ") " + *Body + " }";
    Replacements.push_back(TextReplacement{*Begin, *End - *Begin, std::move(Replacement)});
    return true;
  }

  bool overlaps(unsigned Begin, unsigned End) const {
    for (const TextReplacement &Existing : Replacements) {
      const unsigned ExistingEnd = Existing.Offset + Existing.Length;
      if (!(ExistingEnd <= Begin || End <= Existing.Offset))
        return true;
    }
    return false;
  }

  ASTContext &Context;
  std::vector<TextReplacement> Replacements;
  unsigned DoWhileCount = 0;
  unsigned ForCount = 0;
};

} // namespace

llvm::StringRef DoWhileConverterPass::name() const { return "dowhileconverter"; }

llvm::Error DoWhileConverterPass::run(const PipelineContext &Context,
                                      TransformResult &Result) const {
  DoWhileVisitor Visitor(Context.getASTContext());
  Visitor.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Visitor.takeReplacements();
  if (!Replacements.empty())
    Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                      Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      formatv("phase3: dowhileconverter 改写 do-while={0}, for={1}",
              Visitor.doWhileCount(), Visitor.forCount()).str());
  return Error::success();
}

} // namespace clang::nichecker
