#include "clang-nichecker/Passes/VariableRenamingPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;

namespace clang::nichecker {

class ScopedVariableRenameCollector
    : public RecursiveASTVisitor<ScopedVariableRenameCollector> {
public:
  explicit ScopedVariableRenameCollector(ASTContext &Context)
      : Context(Context), SM(Context.getSourceManager()),
        LangOpts(Context.getLangOpts()) {}

  bool VisitParmVarDecl(ParmVarDecl *Decl) {
    if (!Decl->getIdentifier())
      return true;

    const auto *Parent = dyn_cast<FunctionDecl>(Decl->getDeclContext());
    if (!Parent || !Parent->hasBody() ||
        !isMainFileLocation(Parent->getLocation(), SM))
      return true;

    std::string NewName = "__cs_param_" + Parent->getNameAsString() + "_" +
                          Decl->getNameAsString();
    registerDeclRename(Decl, NewName);
    return true;
  }

  bool VisitVarDecl(VarDecl *Decl) {
    if (!Decl->isLocalVarDecl() || !Decl->getIdentifier())
      return true;

    const auto *Parent =
        dyn_cast_or_null<FunctionDecl>(Decl->getParentFunctionOrMethod());
    if (!Parent || !Parent->hasBody() ||
        !isMainFileLocation(Parent->getLocation(), SM))
      return true;

    std::string NewName = "__cs_local_" + Parent->getNameAsString() + "_" +
                          Decl->getNameAsString();
    registerDeclRename(Decl, NewName);
    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *Expr) {
    const auto *Decl = dyn_cast<ValueDecl>(Expr->getDecl());
    if (!Decl)
      return true;

    const auto *CanonicalDecl = cast<ValueDecl>(Decl->getCanonicalDecl());
    auto It = RenameMap.find(CanonicalDecl);
    if (It == RenameMap.end())
      return true;

    addReplacement(Expr->getLocation(), It->second);
    return true;
  }

  std::vector<TextReplacement> takeReplacements() {
    return std::move(Replacements);
  }

  size_t renamedDeclCount() const { return RenamedDeclCount; }

private:
  void registerDeclRename(const ValueDecl *Decl, llvm::StringRef NewName) {
    const auto *CanonicalDecl = cast<ValueDecl>(Decl->getCanonicalDecl());
    if (RenameMap.contains(CanonicalDecl))
      return;

    RenameMap[CanonicalDecl] = NewName.str();
    addReplacement(Decl->getLocation(), NewName);
    ++RenamedDeclCount;
  }

  void addReplacement(SourceLocation Loc, llvm::StringRef NewName) {
    std::optional<unsigned> Offset = getFileOffset(Loc, SM);
    std::optional<unsigned> Length = getTokenLength(Loc, SM, LangOpts);
    if (!Offset || !Length || *Length == 0)
      return;

    Replacements.push_back(TextReplacement{*Offset, *Length, NewName.str()});
  }

  ASTContext &Context;
  SourceManager &SM;
  const LangOptions &LangOpts;
  llvm::DenseMap<const ValueDecl *, std::string> RenameMap;
  std::vector<TextReplacement> Replacements;
  size_t RenamedDeclCount = 0;
};

llvm::StringRef VariableRenamingPass::name() const {
  return "variable-renaming";
}

llvm::Error VariableRenamingPass::run(const PipelineContext &Context,
                                      TransformResult &Result) const {
  if (Result.Summary.Kind == ProgramKind::InterruptDriven) {
    Result.Notes.push_back(
        "phase4: 中断输入暂时跳过局部变量改名，避免与 main 降级重写产生重叠替换");
    return llvm::Error::success();
  }

  ASTContext &AST = Context.getASTContext();
  ScopedVariableRenameCollector Collector(AST);
  Collector.TraverseDecl(AST.getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Collector.takeReplacements();
  if (Replacements.empty()) {
    Result.Notes.push_back("phase4: 未发现需要改名的局部变量或函数参数");
    return llvm::Error::success();
  }

  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      llvm::formatv("phase4: 已完成 {0} 个局部变量/函数参数的作用域改名",
                    Collector.renamedDeclCount())
          .str());
  return llvm::Error::success();
}

} // namespace clang::nichecker
