#include "clang-nichecker/Passes/SwitchTransformerPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace llvm;
namespace clang::nichecker {
namespace {
struct CaseBlock { std::string Condition; bool IsDefault = false; std::vector<const Stmt *> Statements; };

class Visitor : public RecursiveASTVisitor<Visitor> {
public:
  explicit Visitor(ASTContext &C) : C(C) {}
  bool TraverseSwitchStmt(SwitchStmt *S) {
    if (isMainFileLocation(S->getBeginLoc(), C.getSourceManager()))
      transform(S);
    // The generated outer source preserves nested switches; do not create
    // overlapping replacements for them in this pass invocation.
    return true;
  }
  std::vector<TextReplacement> take() { return std::move(R); }
  unsigned count() const { return Count; }
private:
  std::string text(const Stmt *S) const {
    auto T = getSourceText(S->getSourceRange(), C.getSourceManager(), C.getLangOpts());
    return T ? *T : "";
  }
  std::string stmt(const Stmt *S) const {
    std::string T = text(S);
    if (T.empty()) return T;
    if ((isa<Expr>(S) || isa<DeclStmt>(S) || isa<ReturnStmt>(S) || isa<GotoStmt>(S)) &&
        !StringRef(T).rtrim().ends_with(";")) T += ';';
    return T;
  }
  void transform(SwitchStmt *S) {
    const auto *Body = dyn_cast<CompoundStmt>(S->getBody());
    if (!Body) return;
    std::vector<CaseBlock> Blocks;
    CaseBlock *Current = nullptr;
    for (const Stmt *Item : Body->body()) {
      if (const auto *Case = dyn_cast<CaseStmt>(Item)) {
        auto E = getSourceText(Case->getLHS()->getSourceRange(), C.getSourceManager(), C.getLangOpts());
        if (!E) return;
        Blocks.push_back(CaseBlock{*E, false, {Case->getSubStmt()}}); Current = &Blocks.back();
      } else if (const auto *Default = dyn_cast<DefaultStmt>(Item)) {
        Blocks.push_back(CaseBlock{"", true, {Default->getSubStmt()}}); Current = &Blocks.back();
      } else if (Current) Current->Statements.push_back(Item);
    }
    if (Blocks.empty()) return;
    std::string Cond = text(S->getCond()); if (Cond.empty()) return;
    auto Begin = getFileOffset(S->getBeginLoc(), C.getSourceManager());
    auto End = getFileOffset(Lexer::getLocForEndOfToken(S->getEndLoc(), 0, C.getSourceManager(), C.getLangOpts()), C.getSourceManager());
    if (!Begin || !End || *End <= *Begin) return;
    const std::string Id = std::to_string(*Begin), Var = "__cs_switch_cond_" + Id, Exit = "__cs_switch_exit_" + Id;
    std::string Out = "{ static int " + Var + "; " + Var + " = (" + Cond + ");\n";
    std::string CaseChecks;
    for (size_t I=0; I<Blocks.size(); ++I) {
      const std::string Label = "__cs_switch_case_" + Id + "_" + std::to_string(I);
      std::string Check;
      if (Blocks[I].IsDefault) {
        Check = "!(" + CaseChecks + ")";
      } else {
        if (!CaseChecks.empty()) CaseChecks += " || ";
        CaseChecks += Var + " == (" + Blocks[I].Condition + ")";
        Check = Var + " == (" + Blocks[I].Condition + ")";
      }
      Out += "if (" + Check + ") {\n" + Label + ":;\n";
      bool Break = false;
      for (const Stmt *Item : Blocks[I].Statements) {
        if (isa<BreakStmt>(Item)) { Out += "goto " + Exit + ";\n"; Break = true; }
        else Out += stmt(Item) + "\n";
      }
      if (!Break) {
        if (I + 1 < Blocks.size()) Out += "goto __cs_switch_case_" + Id + "_" + std::to_string(I + 1) + ";\n";
        else Out += "goto " + Exit + ";\n";
      }
      Out += "}\n";
    }
    Out += Exit + ":;\n}";
    R.push_back(TextReplacement{*Begin, *End - *Begin, std::move(Out)}); ++Count;
  }
  ASTContext &C; std::vector<TextReplacement> R; unsigned Count = 0;
};
} // namespace
llvm::StringRef SwitchTransformerPass::name() const { return "switchtransformer"; }
llvm::Error SwitchTransformerPass::run(const PipelineContext &Context, TransformResult &Result) const {
  Visitor V(Context.getASTContext()); V.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  auto R = V.take(); Result.PendingReplacements.insert(Result.PendingReplacements.end(), R.begin(), R.end());
  Result.Notes.push_back(formatv("phase3: switchtransformer 改写了 {0} 个 switch", V.count()).str());
  return Error::success();
}
} // namespace clang::nichecker
