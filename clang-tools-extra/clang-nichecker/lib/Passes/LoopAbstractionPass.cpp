#include "clang-nichecker/Passes/LoopAbstractionPass.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/FormatVariadic.h"

#include <algorithm>

using namespace clang;
using namespace llvm;

namespace clang::nichecker {
namespace {

enum class UpdateKind { Add, Sub, Reset, Unknown };

struct Update {
  UpdateKind Kind;
  std::string Value;
  const Expr *Expression = nullptr;
};

struct VariableInfo {
  const VarDecl *Decl = nullptr;
  bool Read = false;
  std::vector<Update> Updates;
};

class DeclReferenceFinder : public RecursiveASTVisitor<DeclReferenceFinder> {
public:
  explicit DeclReferenceFinder(const ValueDecl *Target) : Target(Target) {}

  bool VisitDeclRefExpr(DeclRefExpr *Reference) {
    Found |= Reference->getDecl()->getCanonicalDecl() ==
             Target->getCanonicalDecl();
    return !Found;
  }

  bool found() const { return Found; }

private:
  const ValueDecl *Target;
  bool Found = false;
};

class LoopVariableCollector : public RecursiveASTVisitor<LoopVariableCollector> {
public:
  explicit LoopVariableCollector(ASTContext &Context) : Context(Context) {}

  bool VisitDeclRefExpr(DeclRefExpr *Reference) {
    const auto *Decl = dyn_cast<VarDecl>(Reference->getDecl());
    if (!Decl)
      return true;
    VariableInfo &Info = Variables[Decl->getCanonicalDecl()];
    Info.Decl = Decl->getCanonicalDecl();
    if (!isWriteReference(Reference))
      Info.Read = true;
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *Operator) {
    if (!Operator->isAssignmentOp())
      return true;
    const VarDecl *Decl = writtenVariable(Operator->getLHS());
    if (!Decl)
      return true;

    VariableInfo &Info = Variables[Decl->getCanonicalDecl()];
    Info.Decl = Decl->getCanonicalDecl();
    Info.Updates.push_back(classifyAssignment(Decl, Operator));
    // A compound assignment reads its left-hand value before writing it.
    if (Operator->getOpcode() != BO_Assign)
      Info.Read = true;
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *Operator) {
    if (!Operator->isIncrementDecrementOp())
      return true;
    const VarDecl *Decl = writtenVariable(Operator->getSubExpr());
    if (!Decl)
      return true;
    VariableInfo &Info = Variables[Decl->getCanonicalDecl()];
    Info.Decl = Decl->getCanonicalDecl();
    // ++x/x++ and their decrement counterparts are read-modify-write.
    Info.Read = true;
    Info.Updates.push_back(
        {Operator->isIncrementOp() ? UpdateKind::Add : UpdateKind::Sub, "1", nullptr});
    return true;
  }

  std::vector<VariableInfo> variablesBefore(SourceLocation LoopBegin,
                                            const VarDecl *ForInitDecl) const {
    std::vector<VariableInfo> Result;
    for (const auto &[Decl, Info] : Variables) {
      if (!Info.Read || Info.Updates.empty() || !Info.Decl ||
          !Info.Decl->getType()->isIntegerType() ||
          (Info.Decl != ForInitDecl &&
           !Context.getSourceManager().isBeforeInTranslationUnit(
               Info.Decl->getBeginLoc(), LoopBegin)))
        continue;
      Result.push_back(Info);
    }
    llvm::sort(Result, [&](const VariableInfo &LHS, const VariableInfo &RHS) {
      return Context.getSourceManager().isBeforeInTranslationUnit(
          LHS.Decl->getBeginLoc(), RHS.Decl->getBeginLoc());
    });
    return Result;
  }

  unsigned outputCount() const {
    unsigned Count = 0;
    for (const auto &[Decl, Info] : Variables)
      Count += !Info.Updates.empty();
    return Count;
  }

private:
  const VarDecl *writtenVariable(const Expr *Expression) const {
    Expression = Expression->IgnoreParenImpCasts();
    const auto *Reference = dyn_cast<DeclRefExpr>(Expression);
    const auto *Decl = Reference ? dyn_cast<VarDecl>(Reference->getDecl()) : nullptr;
    return Decl ? Decl->getCanonicalDecl() : nullptr;
  }

  bool isWriteReference(const DeclRefExpr *Reference) const {
    const Stmt *Current = Reference;
    while (Current) {
      auto Parents = Context.getParents(*Current);
      if (Parents.empty())
        return false;
      if (const auto *Operator = Parents[0].get<BinaryOperator>()) {
        if (Operator->isAssignmentOp())
          return Operator->getLHS()->IgnoreParenImpCasts() ==
                 Reference->IgnoreParenImpCasts();
        return false;
      }
      if (const auto *Operator = Parents[0].get<UnaryOperator>())
        return Operator->isIncrementDecrementOp() &&
               Operator->getSubExpr()->IgnoreParenImpCasts() ==
                   Reference->IgnoreParenImpCasts();
      if (const auto *Parent = Parents[0].get<Stmt>()) {
        Current = Parent;
        continue;
      }
      return false;
    }
    return false;
  }

  bool references(const Expr *Expression, const VarDecl *Decl) const {
    DeclReferenceFinder Finder(Decl);
    Finder.TraverseStmt(const_cast<Expr *>(Expression));
    return Finder.found();
  }

  std::string text(const Expr *Expression) const {
    auto Text = getSourceText(Expression->getSourceRange(),
                              Context.getSourceManager(), Context.getLangOpts());
    return Text ? *Text : "";
  }

  Update classifyAssignment(const VarDecl *Decl,
                            const BinaryOperator *Operator) const {
    if (Operator->getOpcode() == BO_AddAssign)
      return {UpdateKind::Add, text(Operator->getRHS()), Operator->getRHS()};
    if (Operator->getOpcode() == BO_SubAssign)
      return {UpdateKind::Sub, text(Operator->getRHS()), Operator->getRHS()};
    if (Operator->getOpcode() != BO_Assign)
      return {UpdateKind::Unknown, "", nullptr};

    const Expr *Right = Operator->getRHS()->IgnoreParenImpCasts();
    if (!references(Right, Decl))
      return {UpdateKind::Reset, text(Right), Right};
    const auto *Binary = dyn_cast<BinaryOperator>(Right);
    if (!Binary)
      return {UpdateKind::Unknown, ""};
    if (Binary->getOpcode() == BO_Add) {
      if (references(Binary->getLHS(), Decl))
        return {UpdateKind::Add, text(Binary->getRHS()), Binary->getRHS()};
      if (references(Binary->getRHS(), Decl))
        return {UpdateKind::Add, text(Binary->getLHS()), Binary->getLHS()};
    }
    if (Binary->getOpcode() == BO_Sub && references(Binary->getLHS(), Decl))
      return {UpdateKind::Sub, text(Binary->getRHS()), Binary->getRHS()};
    return {UpdateKind::Unknown, "", nullptr};
  }

  ASTContext &Context;
  DenseMap<const VarDecl *, VariableInfo> Variables;
};

class LoopAbstractionVisitor
    : public RecursiveASTVisitor<LoopAbstractionVisitor> {
public:
  explicit LoopAbstractionVisitor(ASTContext &Context) : Context(Context) {}

  bool VisitWhileStmt(WhileStmt *Loop) { transform(Loop, nullptr); return true; }
  bool VisitForStmt(ForStmt *Loop) { transform(Loop, Loop); return true; }

  std::vector<TextReplacement> takeReplacements() {
    return std::move(Replacements);
  }
  unsigned transformed() const { return Transformed; }
  unsigned skipped() const { return Skipped; }

private:
  bool isNestedLoop(const Stmt *Loop) const {
    const Stmt *Current = Loop;
    while (Current) {
      auto Parents = Context.getParents(*Current);
      if (Parents.empty())
        return false;
      const Stmt *Parent = Parents[0].get<Stmt>();
      if (!Parent)
        return false;
      if (Parent != Loop && (isa<WhileStmt>(Parent) || isa<ForStmt>(Parent)))
        return true;
      Current = Parent;
    }
    return false;
  }

  std::string text(const Stmt *Statement) const {
    auto Text = getSourceText(Statement->getSourceRange(),
                              Context.getSourceManager(), Context.getLangOpts());
    return Text ? *Text : "";
  }

  std::string text(const Expr *Expression) const {
    auto Text = getSourceText(Expression->getSourceRange(),
                              Context.getSourceManager(), Context.getLangOpts());
    return Text ? *Text : "";
  }

  std::string statementText(const Stmt *Statement) const {
    std::string Result = text(Statement);
    if (Result.empty())
      return Result;
    if ((isa<Expr>(Statement) || isa<DeclStmt>(Statement)) &&
        !StringRef(Result).rtrim().ends_with(";"))
      Result += ';';
    return Result;
  }

  bool containsAssert(const Stmt *Statement) const {
    class AssertFinder : public RecursiveASTVisitor<AssertFinder> {
    public:
      bool VisitCallExpr(CallExpr *Call) {
        const FunctionDecl *Callee = Call->getDirectCallee();
        Found |= Callee && Callee->getName() == "assert";
        return !Found;
      }
      bool Found = false;
    } Finder;
    Finder.TraverseStmt(const_cast<Stmt *>(Statement));
    return Finder.Found;
  }

  static void replaceAll(std::string &Source, StringRef From, StringRef To) {
    size_t Offset = 0;
    while ((Offset = Source.find(From, Offset)) != std::string::npos) {
      Source.replace(Offset, From.size(), To);
      Offset += To.size();
    }
  }

  bool referencesAny(const Update &Update,
                     const std::vector<VariableInfo> &Variables) const {
    if (!Update.Expression)
      return false;
    for (const VariableInfo &Variable : Variables) {
      DeclReferenceFinder Finder(Variable.Decl);
      Finder.TraverseStmt(const_cast<Expr *>(Update.Expression));
      if (Finder.found())
        return true;
    }
    return false;
  }

  void emitAcceleratedValue(std::string &Out, const VariableInfo &Variable,
                            const std::vector<VariableInfo> &Variables,
                            unsigned LoopId, unsigned VariableId,
                            StringRef BlockSteps) const {
    const std::string Name = Variable.Decl->getNameAsString();
    const std::string Type = Variable.Decl->getType().getAsString();
    const std::string Saved = "__cs_loop_" + std::to_string(LoopId) +
                              "_saved_" + std::to_string(VariableId);
    const std::string Choice = "__cs_loop_" + std::to_string(LoopId) +
                               "_choice_" + std::to_string(VariableId);
    std::vector<Update> Resets;
    std::vector<Update> Recurrences;
    bool Unknown = false;
    for (const Update &Item : Variable.Updates) {
      if (Item.Kind == UpdateKind::Reset && !referencesAny(Item, Variables))
        Resets.push_back(Item);
      else if ((Item.Kind == UpdateKind::Add || Item.Kind == UpdateKind::Sub) &&
               !referencesAny(Item, Variables))
        Recurrences.push_back(Item);
      else
        Unknown = true;
    }
    if (Unknown) {
      Out += Name + " = (" + Type + ")nondet_int();\n";
      return;
    }
    if (!Resets.empty()) {
      const std::string Values = "__cs_loop_" + std::to_string(LoopId) +
                                 "_values_" + std::to_string(VariableId);
      Out += Type + " " + Values + "[] = {" + Saved;
      for (const Update &Reset : Resets)
        Out += ", " + Reset.Value;
      Out += "};\nint " + Choice + " = nondet_int();\n";
      Out += "__CPROVER_assume(" + Choice + " >= 0 && " + Choice + " <= " +
             std::to_string(Resets.size()) + ");\n";
      Out += Name + " = " + Values + "[" + Choice + "];\n";
    } else {
      Out += Name + " = " + Saved + ";\n";
    }
    for (size_t Index = 0; Index < Recurrences.size(); ++Index) {
      Out += Name + (Recurrences[Index].Kind == UpdateKind::Add ? " += (" : " -= (") +
             Recurrences[Index].Value + ") * " + BlockSteps.str() + ";\n";
    }
  }

  void transform(const Stmt *Loop, const ForStmt *ForLoop) {
    if (isNestedLoop(Loop) || !isMainFileLocation(Loop->getBeginLoc(),
                                                   Context.getSourceManager()))
      return;
    const Expr *Condition = ForLoop ? ForLoop->getCond()
                                    : cast<WhileStmt>(Loop)->getCond();
    if (!Condition || text(Condition).empty() ||
        StringRef(text(Condition)).trim() == "1") {
      ++Skipped;
      return;
    }
    const Stmt *Body = ForLoop ? ForLoop->getBody() : cast<WhileStmt>(Loop)->getBody();
    auto Begin = getFileOffset(Loop->getBeginLoc(), Context.getSourceManager());
    SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        Loop->getEndLoc(), 0, Context.getSourceManager(), Context.getLangOpts());
    auto End = getFileOffset(EndLoc, Context.getSourceManager());
    if (!Begin || !End || *End <= *Begin || !Body) {
      ++Skipped;
      return;
    }

    LoopVariableCollector Collector(Context);
    Collector.TraverseStmt(const_cast<Stmt *>(Body));
    if (ForLoop && ForLoop->getInc())
      Collector.TraverseStmt(const_cast<Expr *>(ForLoop->getInc()));
    const VarDecl *ForInitDecl = nullptr;
    if (ForLoop)
      if (const auto *Init = dyn_cast<DeclStmt>(ForLoop->getInit()))
        if (Init->isSingleDecl())
          ForInitDecl = dyn_cast<VarDecl>(Init->getSingleDecl());
    std::vector<VariableInfo> Variables =
        Collector.variablesBefore(Loop->getBeginLoc(), ForInitDecl);
    const unsigned OutputCount = Collector.outputCount();
    const unsigned LoopId = ++LoopNumber;
    const std::string ConditionText = text(Condition);
    std::string BodyText = statementText(Body);
    if (BodyText.empty()) {
      ++Skipped;
      return;
    }
    std::string Increment;
    if (ForLoop && ForLoop->getInc())
      Increment = statementText(ForLoop->getInc());

    std::string Out = "{\n";
    if (ForLoop && ForLoop->getInit())
      Out += statementText(ForLoop->getInit()) + "\n";
    for (size_t Index = 0; Index < Variables.size(); ++Index) {
      Out += Variables[Index].Decl->getType().getAsString() + " __cs_loop_" +
             std::to_string(LoopId) + "_saved_" + std::to_string(Index) + ";\n";
    }
    const std::string BlockSteps =
        "__cs_loop_" + std::to_string(LoopId) + "_block_1";
    bool UsesBlockSteps = false;
    for (const VariableInfo &Variable : Variables)
      for (const Update &Update : Variable.Updates)
        UsesBlockSteps |= Update.Kind == UpdateKind::Add ||
                          Update.Kind == UpdateKind::Sub;
    if (UsesBlockSteps)
      Out += "int " + BlockSteps + ";\n";

    const bool Induction = containsAssert(Body);
    auto EmitBody = [&](bool AsAssume) {
      std::string Printed = BodyText;
      if (AsAssume)
        replaceAll(Printed, "assert(", "__CPROVER_assume(");
      Out += Printed + "\n";
      if (!Increment.empty())
        Out += Increment + "\n";
    };
    if (Induction) {
      Out += "if (" + ConditionText + ")\n";
      EmitBody(false);
    }
    Out += "for (int __cs_loop_iter_" + std::to_string(LoopId) + " = 0; "
           "__cs_loop_iter_" + std::to_string(LoopId) + " < " +
           std::to_string(OutputCount) + " && (" + ConditionText +
           "); ++__cs_loop_iter_" + std::to_string(LoopId) + ") {\n";
    for (size_t Index = 0; Index < Variables.size(); ++Index)
      Out += "__cs_loop_" + std::to_string(LoopId) + "_saved_" +
             std::to_string(Index) + " = " +
             Variables[Index].Decl->getNameAsString() + ";\n";
    if (UsesBlockSteps) {
      Out += BlockSteps + " = nondet_int();\n";
      Out += "__CPROVER_assume(" + BlockSteps + " >= 0);\n";
    }
    for (size_t Index = 0; Index < Variables.size(); ++Index)
      emitAcceleratedValue(Out, Variables[Index], Variables, LoopId, Index,
                           BlockSteps);
    Out += "__CPROVER_assume(" + ConditionText + ");\n";
    EmitBody(Induction);
    Out += "}\n";
    if (Induction) {
      Out += "if (" + ConditionText + ")\n";
      EmitBody(false);
    }
    Out += "__CPROVER_assume(!(" + ConditionText + "));\n}";
    Replacements.push_back(TextReplacement{*Begin, *End - *Begin, std::move(Out)});
    ++Transformed;
  }

  ASTContext &Context;
  std::vector<TextReplacement> Replacements;
  unsigned LoopNumber = 0;
  unsigned Transformed = 0;
  unsigned Skipped = 0;
};

} // namespace

llvm::StringRef LoopAbstractionPass::name() const { return "LoopAbstraction"; }

llvm::Error LoopAbstractionPass::run(const PipelineContext &Context,
                                     TransformResult &Result) const {
  if (!Context.Options.EnableLoopAbstraction) {
    Result.Notes.push_back("phase3: LoopAbstraction 未启用（与 Python 默认配置一致）");
    return Error::success();
  }
  LoopAbstractionVisitor Visitor(Context.getASTContext());
  Visitor.TraverseDecl(Context.getASTContext().getTranslationUnitDecl());
  std::vector<TextReplacement> Replacements = Visitor.takeReplacements();
  Result.PendingReplacements.insert(Result.PendingReplacements.end(),
                                    Replacements.begin(), Replacements.end());
  Result.Notes.push_back(
      formatv("phase3: LoopAbstraction 摘要化循环={0}，跳过 while(1)/不支持循环={1}",
              Visitor.transformed(), Visitor.skipped())
          .str());
  return Error::success();
}

} // namespace clang::nichecker
