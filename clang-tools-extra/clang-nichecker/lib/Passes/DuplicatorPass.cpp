#include "clang-nichecker/Passes/DuplicatorPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/FormatVariadic.h"

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

const FunctionDecl *threadEntryFunction(const Expr *Expression) {
  Expression = Expression->IgnoreParenImpCasts();
  while (const auto *Cast = dyn_cast<ExplicitCastExpr>(Expression))
    Expression = Cast->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *Address = dyn_cast<UnaryOperator>(Expression)) {
    if (Address->getOpcode() == UO_AddrOf)
      Expression = Address->getSubExpr()->IgnoreParenImpCasts();
  }
  const auto *Reference = dyn_cast<DeclRefExpr>(Expression);
  return Reference ? dyn_cast<FunctionDecl>(Reference->getDecl()) : nullptr;
}

struct ThreadCreation {
  const CallExpr *Call = nullptr;
  const FunctionDecl *Definition = nullptr;
  std::string CopyName;
};

class DuplicatorCollector : public RecursiveASTVisitor<DuplicatorCollector> {
public:
  DuplicatorCollector(ASTContext &Context, unsigned ThreadBound)
      : Context(Context), ThreadBound(ThreadBound) {}

  bool VisitDeclRefExpr(DeclRefExpr *Reference) {
    const auto *Function = dyn_cast<FunctionDecl>(Reference->getDecl());
    if (!Function ||
        !isMainFileLocation(Reference->getLocation(), Context.getSourceManager()))
      return true;
    ++FunctionReferences[Function->getCanonicalDecl()];
    return true;
  }

  bool VisitCallExpr(CallExpr *Call) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (!Callee || Callee->getName() != "pthread_create" ||
        Call->getNumArgs() < 3 ||
        !isMainFileLocation(Call->getBeginLoc(), Context.getSourceManager()))
      return true;
    if (ThreadBound && Creations.size() >= ThreadBound) {
      BoundExceeded = true;
      return true;
    }
    const FunctionDecl *Entry = threadEntryFunction(Call->getArg(2));
    const FunctionDecl *Definition = Entry ? Entry->getDefinition() : nullptr;
    if (!Definition ||
        !isMainFileLocation(Definition->getLocation(), Context.getSourceManager()))
      return true;
    Creations.push_back(ThreadCreation{Call, Definition, ""});
    return true;
  }

  std::vector<ThreadCreation> takeCreations() { return std::move(Creations); }
  const DenseMap<const FunctionDecl *, unsigned> &references() const {
    return FunctionReferences;
  }
  bool boundExceeded() const { return BoundExceeded; }

private:
  ASTContext &Context;
  unsigned ThreadBound;
  std::vector<ThreadCreation> Creations;
  DenseMap<const FunctionDecl *, unsigned> FunctionReferences;
  bool BoundExceeded = false;
};

std::optional<std::string> cloneFunction(const FunctionDecl *Function,
                                         StringRef CopyName,
                                         const PipelineContext &Context,
                                         bool IsPrototype) {
  auto Text = getSourceText(Function->getSourceRange(), Context.getSourceManager(),
                            Context.getLangOpts());
  auto Begin = getFileOffset(Function->getBeginLoc(), Context.getSourceManager());
  auto NameOffset = getFileOffset(Function->getLocation(), Context.getSourceManager());
  auto NameLength = getTokenLength(Function->getLocation(),
                                   Context.getSourceManager(), Context.getLangOpts());
  if (!Text || !Begin || !NameOffset || !NameLength || *NameOffset < *Begin)
    return std::nullopt;
  const unsigned RelativeOffset = *NameOffset - *Begin;
  if (RelativeOffset + *NameLength > Text->size())
    return std::nullopt;
  Text->replace(RelativeOffset, *NameLength, CopyName.str());
  if (IsPrototype)
    *Text += ';';
  return Text;
}

std::optional<TextReplacement>
replaceThreadStart(const ThreadCreation &Creation,
                   const PipelineContext &Context) {
  const Expr *Start = Creation.Call->getArg(2);
  auto Begin = getFileOffset(Start->getBeginLoc(), Context.getSourceManager());
  SourceLocation EndLoc = Lexer::getLocForEndOfToken(
      Start->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
  auto End = getFileOffset(EndLoc, Context.getSourceManager());
  if (!Begin || !End || *End <= *Begin)
    return std::nullopt;
  return TextReplacement{*Begin, *End - *Begin, Creation.CopyName};
}

} // namespace

llvm::StringRef DuplicatorPass::name() const { return "duplicator"; }

llvm::Error DuplicatorPass::run(const PipelineContext &Context,
                                TransformResult &Result) const {
  DuplicatorCollector Collector(Context.getASTContext(), Context.Options.Threads);
  Collector.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  if (Collector.boundExceeded())
    return createStringError(inconvertibleErrorCode(),
                             "duplicator 超过 --threads 指定的线程创建上界");

  std::vector<ThreadCreation> Creations = Collector.takeCreations();
  if (Creations.empty()) {
    Result.Notes.push_back("phase4: duplicator 未发现可复制的 pthread_create 入口");
    return Error::success();
  }

  DenseMap<const FunctionDecl *, std::vector<std::string>> Copies;
  for (ThreadCreation &Creation : Creations) {
    std::vector<std::string> &Names = Copies[Creation.Definition];
    Creation.CopyName = Creation.Definition->getNameAsString() + "_" +
                        std::to_string(Names.size());
    Names.push_back(Creation.CopyName);
  }

  std::vector<TextReplacement> Replacements;
  for (const ThreadCreation &Creation : Creations) {
    if (auto Replacement = replaceThreadStart(Creation, Context))
      Replacements.push_back(std::move(*Replacement));
  }

  for (Decl *DeclNode : Context.getASTContext().getTranslationUnitDecl()->decls()) {
    const auto *Function = dyn_cast<FunctionDecl>(DeclNode);
    if (!Function || !isMainFileLocation(Function->getLocation(),
                                         Context.getSourceManager()))
      continue;
    const FunctionDecl *Definition = Function->getDefinition();
    if (!Definition)
      continue;
    auto CopiesIt = Copies.find(Definition);
    if (CopiesIt == Copies.end())
      continue;

    const bool IsDefinition = Function->doesThisDeclarationHaveABody();
    const unsigned References =
        Collector.references().lookup(Definition->getCanonicalDecl());
    const bool KeepOriginal = References > CopiesIt->second.size();
    std::string Replacement;
    for (const std::string &CopyName : CopiesIt->second) {
      auto Clone = cloneFunction(Function, CopyName, Context, !IsDefinition);
      if (!Clone)
        return createStringError(inconvertibleErrorCode(),
                                 "duplicator 无法复制线程函数声明或定义");
      Replacement += *Clone;
      Replacement += '\n';
    }

    if (KeepOriginal) {
      auto Original = getSourceText(Function->getSourceRange(),
                                    Context.getSourceManager(), Context.getLangOpts());
      if (!Original)
        return createStringError(inconvertibleErrorCode(),
                                 "duplicator 无法读取原线程函数定义");
      Replacement += *Original;
    }

    auto Begin = getFileOffset(Function->getBeginLoc(), Context.getSourceManager());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Function->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (!Begin || !End || *End <= *Begin)
      return createStringError(inconvertibleErrorCode(),
                               "duplicator 无法定位线程函数替换范围");
    Replacements.push_back(
        TextReplacement{*Begin, *End - *Begin, std::move(Replacement)});
  }

  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Summary.ThreadEntryFunctions.clear();
  for (const ThreadCreation &Creation : Creations)
    Result.Summary.ThreadEntryFunctions.push_back(Creation.CopyName);
  Result.Notes.push_back(
      formatv("phase4: duplicator 将 {0} 个 pthread_create 入口复制为独立线程函数",
              Creations.size())
          .str());
  return Error::success();
}

} // namespace clang::nichecker
