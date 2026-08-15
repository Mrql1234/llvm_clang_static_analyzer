#include "clang-nichecker/Passes/CondWaitConverterPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

#include <map>

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

class CondWaitConverterVisitor
    : public RecursiveASTVisitor<CondWaitConverterVisitor> {
public:
  explicit CondWaitConverterVisitor(ASTContext &Context) : Context(Context) {}

  bool VisitCallExpr(CallExpr *Call) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    if (!Callee || !isMainFileLocation(Call->getBeginLoc(),
                                       Context.getSourceManager()))
      return true;

    StringRef Name = Callee->getName();
    if (Name == "pthread_cond_wait" || Name == "pthread_cond_timedwait")
      splitConditionWait(Call);
    else if (Name == "pthread_barrier_wait")
      splitBarrierWait(Call);
    return true;
  }

  std::vector<TextReplacement> takeReplacements() {
    for (const auto &[Offset, Prefixes] : InsertedPrefixes) {
      std::string Text;
      for (const std::string &Prefix : Prefixes)
        Text += Prefix;
      Replacements.push_back(TextReplacement{Offset, 0, std::move(Text)});
    }
    return std::move(Replacements);
  }

  unsigned convertedCalls() const { return ConvertedCalls; }

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

  std::optional<std::string> argumentText(const CallExpr *Call,
                                          unsigned Index) const {
    if (Index >= Call->getNumArgs())
      return std::nullopt;
    return getSourceText(Call->getArg(Index)->getSourceRange(),
                         Context.getSourceManager(), Context.getLangOpts());
  }

  void splitConditionWait(CallExpr *Call) {
    auto Condition = argumentText(Call, 0);
    auto Mutex = argumentText(Call, 1);
    if (!Condition || !Mutex)
      return;
    splitCall(Call, "pthread_cond_wait_1(" + *Condition + "," + *Mutex + ")",
              "pthread_cond_wait_2(" + *Condition + "," + *Mutex + ")");
  }

  void splitBarrierWait(CallExpr *Call) {
    auto Barrier = argumentText(Call, 0);
    if (!Barrier)
      return;
    splitCall(Call, "pthread_barrier_wait_1(" + *Barrier + ")",
              "pthread_barrier_wait_2(" + *Barrier + ")");
  }

  void splitCall(const CallExpr *Call, const std::string &First,
                 const std::string &Second) {
    const Stmt *Statement = containingCompoundStatement(Call);
    if (!Statement)
      return;
    auto CallBegin = getFileOffset(Call->getBeginLoc(), Context.getSourceManager());
    SourceLocation CallEndLoc = Lexer::getLocForEndOfToken(
        Call->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto CallEnd = getFileOffset(CallEndLoc, Context.getSourceManager());
    auto StatementBegin =
        getFileOffset(Statement->getBeginLoc(), Context.getSourceManager());
    if (!CallBegin || !CallEnd || !StatementBegin || *CallEnd <= *CallBegin)
      return;

    if (Statement == Call) {
      Replacements.push_back(
          TextReplacement{*CallBegin, *CallEnd - *CallBegin, First + "; " + Second});
    } else {
      Replacements.push_back(
          TextReplacement{*CallBegin, *CallEnd - *CallBegin, Second});
      InsertedPrefixes[*StatementBegin].push_back(First + "; ");
    }
    ++ConvertedCalls;
  }

  ASTContext &Context;
  std::vector<TextReplacement> Replacements;
  std::map<unsigned, std::vector<std::string>> InsertedPrefixes;
  unsigned ConvertedCalls = 0;
};

} // namespace

llvm::StringRef CondWaitConverterPass::name() const {
  return "condwaitconverter";
}

llvm::Error CondWaitConverterPass::run(const PipelineContext &Context,
                                       TransformResult &Result) const {
  CondWaitConverterVisitor Visitor(Context.getASTContext());
  Visitor.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Visitor.takeReplacements();
  if (!Replacements.empty())
    Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                      Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      formatv("phase3: condwaitconverter 拆分了 {0} 个等待调用",
              Visitor.convertedCalls())
          .str());
  return Error::success();
}

} // namespace clang::nichecker
