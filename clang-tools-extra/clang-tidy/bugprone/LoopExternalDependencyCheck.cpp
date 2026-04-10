//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LoopExternalDependencyCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

namespace {

// Collect all DeclRefExprs in an expression.
class DeclRefCollector : public RecursiveASTVisitor<DeclRefCollector> {
public:
  llvm::SmallVector<const VarDecl *, 4> VarDecls;
  bool HasCallExpr = false;

  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      VarDecls.push_back(VD);
    return true;
  }

  bool VisitCallExpr(CallExpr *) {
    HasCallExpr = true;
    return true;
  }
};

// Check if a variable is volatile-qualified.
bool isVolatile(const VarDecl *VD) {
  return VD->getType().isVolatileQualified();
}

// Check whether a variable is directly modified (assigned to) inside a
// statement, excluding modifications via function calls.
class DirectModificationChecker
    : public RecursiveASTVisitor<DirectModificationChecker> {
public:
  const VarDecl *Target;
  bool Found = false;

  explicit DirectModificationChecker(const VarDecl *VD) : Target(VD) {}

  bool VisitBinaryOperator(BinaryOperator *BO) {
    if (!BO->isAssignmentOp())
      return true;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParens())) {
      if (DRE->getDecl() == Target)
        Found = true;
    }
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *UO) {
    if (!UO->isIncrementDecrementOp())
      return true;
    if (const auto *DRE =
            dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParens())) {
      if (DRE->getDecl() == Target)
        Found = true;
    }
    return true;
  }
};

// Check whether a loop body contains any exit path (break at current loop
// level, return, goto, or throw).  Does not descend into nested loops/switches
// for break detection — a break inside a nested loop does not exit the outer
// loop.
class ExitPathChecker : public RecursiveASTVisitor<ExitPathChecker> {
public:
  bool HasExit = false;

  bool TraverseWhileStmt(WhileStmt *) {
    return true; // skip nested loops
  }
  bool TraverseForStmt(ForStmt *) {
    return true;
  }
  bool TraverseDoStmt(DoStmt *) {
    return true;
  }
  bool TraverseSwitchStmt(SwitchStmt *) {
    return true; // break inside switch doesn't exit the loop
  }

  bool VisitBreakStmt(BreakStmt *) {
    HasExit = true;
    return false;
  }
  bool VisitReturnStmt(ReturnStmt *) {
    HasExit = true;
    return false;
  }
  bool VisitGotoStmt(GotoStmt *) {
    HasExit = true;
    return false;
  }
  bool VisitCXXThrowExpr(CXXThrowExpr *) {
    HasExit = true;
    return false;
  }
};

} // namespace

void LoopExternalDependencyCheck::registerMatchers(MatchFinder *Finder) {
  // Match while loops with a condition expression.
  Finder->addMatcher(
      whileStmt(hasCondition(expr().bind("cond")), hasBody(stmt().bind("body")))
          .bind("loop"),
      this);

  // Match do-while loops.
  Finder->addMatcher(
      doStmt(hasCondition(expr().bind("cond")), hasBody(stmt().bind("body")))
          .bind("loop"),
      this);

  // Match for loops with a condition.
  Finder->addMatcher(
      forStmt(hasCondition(expr().bind("cond")), hasBody(stmt().bind("body")))
          .bind("loop"),
      this);

  // Match for loops without a condition — for(;;).
  Finder->addMatcher(
      forStmt(unless(hasCondition(expr())), hasBody(stmt().bind("body")))
          .bind("loop"),
      this);
}

void LoopExternalDependencyCheck::check(
    const MatchFinder::MatchResult &Result) {
  const auto *Cond = Result.Nodes.getNodeAs<Expr>("cond"); // null for for(;;)
  const auto *Body = Result.Nodes.getNodeAs<Stmt>("body");
  const auto *Loop = Result.Nodes.getNodeAs<Stmt>("loop");

  if (!Body || !Loop)
    return;

  if (Loop->getBeginLoc().isMacroID())
    return;

  // --- Constant-true / no-condition detection ---
  // Covers while(true), while(1), for(;;), do{}while(true) with no exit path.
  bool IsConstantTrue = false;
  if (!Cond) {
    IsConstantTrue = true; // for(;;)
  } else {
    bool ConstVal;
    if (Cond->EvaluateAsBooleanCondition(ConstVal, *Result.Context) && ConstVal)
      IsConstantTrue = true;
  }

  if (IsConstantTrue) {
    ExitPathChecker EPC;
    EPC.TraverseStmt(const_cast<Stmt *>(Body));
    if (!EPC.HasExit) {
      diag(Loop->getBeginLoc(),
           "loop condition is always true and the loop body has no reachable "
           "exit path (break/return/throw); this is an infinite loop");
    }
    return; // constant-true with exits is an intentional pattern, don't warn
  }

  if (!Cond)
    return;

  // Analyze the condition expression.
  DeclRefCollector CondCollector;
  CondCollector.TraverseStmt(const_cast<Expr *>(Cond));

  // Case A: The condition itself is purely a function call (e.g. while(getStatus())).
  if (CondCollector.HasCallExpr && CondCollector.VarDecls.empty()) {
    diag(Loop->getBeginLoc(),
         "loop exit condition depends on external function call; "
         "consider verifying that the callee eventually returns a "
         "terminating value to avoid a potential infinite loop");
    return;
  }

  // Case B: The condition references variables — check if any are volatile
  // (hardware/threading scenario, don't warn).
  if (CondCollector.VarDecls.empty())
    return;

  for (const auto *VD : CondCollector.VarDecls) {
    if (isVolatile(VD))
      return;
  }

  // Check if condition variables are directly modified in the loop body
  // (or in the for-loop increment expression).  If they are, this is a
  // normal loop — don't warn.
  for (const auto *VD : CondCollector.VarDecls) {
    DirectModificationChecker Checker(VD);
    Checker.TraverseStmt(const_cast<Stmt *>(Body));
    if (!Checker.Found) {
      // Also check the for-loop increment expression.
      if (const auto *FS = dyn_cast<ForStmt>(Loop)) {
        if (const auto *Inc = FS->getInc()) {
          DirectModificationChecker IncChecker(VD);
          IncChecker.TraverseStmt(const_cast<Expr *>(Inc));
          if (IncChecker.Found)
            Checker.Found = true;
        }
      }
    }
    if (Checker.Found)
      return;
  }

  // Condition variables are NOT directly modified.  Check if the loop body
  // contains function calls that might modify them externally.
  DeclRefCollector BodyCollector;
  BodyCollector.TraverseStmt(const_cast<Stmt *>(Body));

  if (BodyCollector.HasCallExpr) {
    diag(Loop->getBeginLoc(),
         "loop exit condition depends on variable(s) not directly modified "
         "in the loop body; modification may rely on external function "
         "calls — consider verifying loop termination");
  }
}

} // namespace clang::tidy::bugprone
