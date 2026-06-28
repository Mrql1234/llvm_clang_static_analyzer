#include "clang-nichecker/Passes/LoopUnrollPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;

namespace clang::nichecker {

class LoopUnrollVisitor : public RecursiveASTVisitor<LoopUnrollVisitor> {
public:
  explicit LoopUnrollVisitor(ASTContext &Context,
                             const std::vector<TextReplacement> &Pending)
      : Context(Context), SM(Context.getSourceManager()),
        LangOpts(Context.getLangOpts()), Pending(Pending) {}

  bool VisitWhileStmt(WhileStmt *Stmt) { return unrollWhile(Stmt); }
  bool VisitForStmt(ForStmt *Stmt) { return unrollFor(Stmt); }

  std::vector<TextReplacement> takeReplacements() {
    return std::move(Replacements);
  }

  size_t unrolledWhileCount() const { return WhileCount; }
  size_t unrolledForCount() const { return ForCount; }

private:
  class ControlFlowFinder : public RecursiveASTVisitor<ControlFlowFinder> {
  public:
    bool VisitBreakStmt(BreakStmt *) {
      Found = true;
      return false;
    }
    bool VisitContinueStmt(ContinueStmt *) {
      Found = true;
      return false;
    }
    bool VisitGotoStmt(GotoStmt *) {
      Found = true;
      return false;
    }
    bool VisitLabelStmt(LabelStmt *) {
      Found = true;
      return false;
    }

    bool found() const { return Found; }

  private:
    bool Found = false;
  };

  bool hasComplexControlFlow(const Stmt *Statement) const {
    if (!Statement)
      return false;
    ControlFlowFinder Finder;
    Finder.TraverseStmt(const_cast<Stmt *>(Statement));
    return Finder.found();
  }

  bool overlapsPending(unsigned Begin, unsigned End) const {
    for (const TextReplacement &Replacement : Pending) {
      unsigned RepBegin = Replacement.Offset;
      unsigned RepEnd = Replacement.Offset + Replacement.Length;
      if (Replacement.Length == 0) {
        if (RepBegin >= Begin && RepBegin <= End)
          return true;
        continue;
      }
      if (!(RepEnd <= Begin || RepBegin >= End))
        return true;
    }
    return false;
  }

  std::optional<unsigned> getStmtBeginOffset(const Stmt *Statement) const {
    if (!Statement)
      return std::nullopt;
    return getFileOffset(Statement->getBeginLoc(), SM);
  }

  std::optional<unsigned> getStmtEndOffset(const Stmt *Statement) const {
    if (!Statement)
      return std::nullopt;
    SourceLocation EndLoc =
        Lexer::getLocForEndOfToken(Statement->getEndLoc(), 0, SM, LangOpts);
    return getFileOffset(EndLoc, SM);
  }

  std::optional<std::string> getStmtText(const Stmt *Statement) const {
    if (!Statement)
      return std::nullopt;
    return getSourceText(Statement->getSourceRange(), SM, LangOpts);
  }

  std::string buildWhileNest(llvm::StringRef CondText, llvm::StringRef BodyText,
                             unsigned Bound, llvm::StringRef Indent) const {
    if (Bound == 0)
      return "{}";

    std::string Text = (Indent + "{\n").str();
    std::string CurrentIndent = (Indent + "  ").str();
    for (unsigned I = 0; I < Bound; ++I) {
      Text += llvm::formatv("{0}if ({1}) {{\n", CurrentIndent, CondText).str();
      Text += llvm::formatv("{0}{1}\n", CurrentIndent + "  ", BodyText).str();
      CurrentIndent += "  ";
    }
    for (unsigned I = 0; I < Bound; ++I) {
      CurrentIndent.resize(CurrentIndent.size() - 2);
      Text += CurrentIndent + "}\n";
    }
    Text += (Indent + "}").str();
    return Text;
  }

  std::string buildForNest(llvm::StringRef InitText, llvm::StringRef CondText,
                           llvm::StringRef NextText, llvm::StringRef BodyText,
                           unsigned Bound, llvm::StringRef Indent) const {
    std::string Text = (Indent + "{\n").str();
    std::string BodyIndent = (Indent + "  ").str();
    if (!InitText.empty())
      Text += llvm::formatv("{0}{1};\n", BodyIndent, InitText).str();

    std::string CurrentIndent = BodyIndent;
    for (unsigned I = 0; I < Bound; ++I) {
      std::string EffectiveCond = CondText.empty() ? "1" : CondText.str();
      Text += llvm::formatv("{0}if ({1}) {{\n", CurrentIndent, EffectiveCond).str();
      Text += llvm::formatv("{0}{1}\n", CurrentIndent + "  ", BodyText).str();
      if (!NextText.empty())
        Text += llvm::formatv("{0}{1};\n", CurrentIndent + "  ", NextText).str();
      CurrentIndent += "  ";
    }
    for (unsigned I = 0; I < Bound; ++I) {
      CurrentIndent.resize(CurrentIndent.size() - 2);
      Text += CurrentIndent + "}\n";
    }
    Text += (Indent + "}").str();
    return Text;
  }

  bool unrollWhile(WhileStmt *Stmt) {
    if (!isMainFileLocation(Stmt->getWhileLoc(), SM))
      return true;
    if (hasComplexControlFlow(Stmt->getBody()))
      return true;

    std::optional<unsigned> Begin = getStmtBeginOffset(Stmt);
    std::optional<unsigned> End = getStmtEndOffset(Stmt);
    std::optional<std::string> CondText = getStmtText(Stmt->getCond());
    std::optional<std::string> BodyText = getStmtText(Stmt->getBody());
    if (!Begin || !End || !CondText || !BodyText || *Begin >= *End)
      return true;
    if (overlapsPending(*Begin, *End))
      return true;

    unsigned Bound = 0;
    Bound = UnwindWhile;
    if (Bound == 0)
      return true;

    std::string Indent = getLineIndent(SM.getBufferData(SM.getMainFileID()), *Begin);
    std::string Replacement = buildWhileNest(*CondText, *BodyText, Bound, Indent);
    Replacements.push_back(TextReplacement{*Begin, *End - *Begin, Replacement});
    ++WhileCount;
    return true;
  }

  bool unrollFor(ForStmt *Stmt) {
    if (!isMainFileLocation(Stmt->getForLoc(), SM))
      return true;
    if (hasComplexControlFlow(Stmt->getBody()))
      return true;

    std::optional<unsigned> Begin = getStmtBeginOffset(Stmt);
    std::optional<unsigned> End = getStmtEndOffset(Stmt);
    std::optional<std::string> InitText = getStmtText(Stmt->getInit());
    std::optional<std::string> CondText = getStmtText(Stmt->getCond());
    std::optional<std::string> NextText = getStmtText(Stmt->getInc());
    std::optional<std::string> BodyText = getStmtText(Stmt->getBody());
    if (!Begin || !End || !BodyText || *Begin >= *End)
      return true;
    if (overlapsPending(*Begin, *End))
      return true;

    unsigned Bound = UnwindFor;
    if (Bound == 0)
      return true;

    std::string Indent = getLineIndent(
        SM.getBufferData(SM.getMainFileID()), *Begin);
    std::string Replacement = buildForNest(
        InitText ? llvm::StringRef(*InitText) : llvm::StringRef(),
        CondText ? llvm::StringRef(*CondText) : llvm::StringRef(),
        NextText ? llvm::StringRef(*NextText) : llvm::StringRef(),
        *BodyText, Bound, Indent);
    Replacements.push_back(TextReplacement{*Begin, *End - *Begin, Replacement});
    ++ForCount;
    return true;
  }

public:
  unsigned UnwindWhile = 0;
  unsigned UnwindFor = 0;

private:
  ASTContext &Context;
  SourceManager &SM;
  const LangOptions &LangOpts;
  const std::vector<TextReplacement> &Pending;
  std::vector<TextReplacement> Replacements;
  size_t WhileCount = 0;
  size_t ForCount = 0;
};

llvm::StringRef LoopUnrollPass::name() const { return "loop-unroll"; }

llvm::Error LoopUnrollPass::run(const PipelineContext &Context,
                                TransformResult &Result) const {
  ASTContext &AST = Context.getASTContext();
  LoopUnrollVisitor Visitor(AST, Result.PendingReplacements);
  Visitor.UnwindWhile = Context.Options.UnwindWhile;
  Visitor.UnwindFor = Context.Options.UnwindFor;
  Visitor.TraverseDecl(AST.getTranslationUnitDecl());

  std::vector<TextReplacement> Replacements = Visitor.takeReplacements();
  if (Replacements.empty()) {
    Result.Notes.push_back("phase4: loop-unroll 第一版未命中可安全展开的 while/for");
    return llvm::Error::success();
  }

  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      llvm::formatv("phase4: loop-unroll 第一版已完成 while={0}, for={1}",
                    Visitor.unrolledWhileCount(), Visitor.unrolledForCount())
          .str());
  return llvm::Error::success();
}

} // namespace clang::nichecker
