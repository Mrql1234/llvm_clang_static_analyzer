//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MathDomainGuardCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

namespace {

enum class MathFuncKind { Sqrt, InvTrig };

struct GuardInfo {
  bool IsGuarded = false;
  bool IsReverseGuard = false;
};

/// Walk up the AST from the call expression. If we find an IfStmt whose
/// then-branch (or else-branch) contains the call, inspect the condition
/// to see if it guards the argument.
///
/// Recognised patterns (for sqrt, argVar is the parameter to sqrt):
///   if (argVar >= 0) { sqrt(argVar); }       -> positive guard
///   if (argVar > 0)  { sqrt(argVar); }       -> positive guard
///   if (argVar < 0)  { sqrt(argVar); }       -> REVERSE guard (violation)
///   if (argVar <= some_neg) { sqrt(argVar); } -> REVERSE guard
///
/// For asin/acos the domain is [-1, 1]; we only check simple patterns.

static const VarDecl *extractReferencedVar(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return dyn_cast<VarDecl>(DRE->getDecl());
  return nullptr;
}

static bool isFabsCall(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    if (const auto *FD = CE->getDirectCallee()) {
      StringRef Name = FD->getName();
      return Name == "fabs" || Name == "fabsf" || Name == "fabsl" ||
             Name == "abs";
    }
  }
  return false;
}

/// Check whether a binary condition compares ArgVar against a numeric
/// constant and whether that condition is a positive or reverse guard.
static GuardInfo analyzeCondition(const BinaryOperator *BO,
                                  const VarDecl *ArgVar, MathFuncKind Kind,
                                  bool InThenBranch) {
  GuardInfo GI;
  const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

  const VarDecl *LVar = extractReferencedVar(LHS);
  const VarDecl *RVar = extractReferencedVar(RHS);

  bool ArgOnLeft = (LVar == ArgVar);
  bool ArgOnRight = (RVar == ArgVar);
  if (!ArgOnLeft && !ArgOnRight)
    return GI;

  BinaryOperator::Opcode Op = BO->getOpcode();
  // Normalise so that arg is on the left: arg OP const
  if (ArgOnRight) {
    switch (Op) {
    case BO_LT: Op = BO_GT; break;
    case BO_GT: Op = BO_LT; break;
    case BO_LE: Op = BO_GE; break;
    case BO_GE: Op = BO_LE; break;
    default: break;
    }
  }

  // If we are in the else-branch, invert the condition.
  if (!InThenBranch) {
    switch (Op) {
    case BO_LT: Op = BO_GE; break;
    case BO_GE: Op = BO_LT; break;
    case BO_GT: Op = BO_LE; break;
    case BO_LE: Op = BO_GT; break;
    case BO_EQ: Op = BO_NE; break;
    case BO_NE: Op = BO_EQ; break;
    default: break;
    }
  }

  // For sqrt: positive guard means arg >= 0 or arg > 0 (or > any non-neg).
  // Reverse guard means arg < 0 or arg <= some_negative.
  if (Kind == MathFuncKind::Sqrt) {
    if (Op == BO_GE || Op == BO_GT) {
      GI.IsGuarded = true;
    } else if (Op == BO_LT || Op == BO_LE) {
      GI.IsReverseGuard = true;
    }
  } else {
    // InvTrig: domain is [-1, 1].  Partial validation is hard to detect
    // from a single comparison; treat >= -1 or <= 1 as partial guard.
    if (Op == BO_GE || Op == BO_LE) {
      GI.IsGuarded = true;
    } else if (Op == BO_LT || Op == BO_GT) {
      GI.IsReverseGuard = true;
    }
  }
  return GI;
}

/// Walk parents looking for an enclosing IfStmt whose then/else branch
/// contains the call.  Returns a GuardInfo.
static GuardInfo findGuard(const CallExpr *Call, const VarDecl *ArgVar,
                           MathFuncKind Kind, ASTContext &Ctx) {
  GuardInfo Result;
  auto Parents = Ctx.getParents(*Call);

  // Walk up to 8 levels to find an enclosing IfStmt.
  const DynTypedNode *Current = nullptr;
  SmallVector<DynTypedNode, 8> Ancestors;
  for (const auto &P : Parents)
    Ancestors.push_back(P);

  for (unsigned Depth = 0; Depth < 8 && !Ancestors.empty(); ++Depth) {
    for (const auto &Anc : Ancestors) {
      if (const auto *If = Anc.get<IfStmt>()) {
        const Expr *Cond = If->getCond();
        if (!Cond)
          return Result;

        Cond = Cond->IgnoreParenImpCasts();

        // Determine if call is in then or else branch.
        bool InThenBranch = true;
        if (If->getElse()) {
          // Check if our call is inside the else branch.
          // Simple heuristic: compare source locations.
          SourceLocation CallLoc = Call->getBeginLoc();
          SourceLocation ElseLoc = If->getElse()->getBeginLoc();
          SourceLocation ThenEnd = If->getThen()->getEndLoc();
          if (CallLoc >= ElseLoc)
            InThenBranch = false;
        }

        if (const auto *BO = dyn_cast<BinaryOperator>(Cond)) {
          // Handle && (logical and): both sides must be checked, but
          // for simplicity treat each side independently.
          if (BO->getOpcode() == BO_LAnd) {
            if (const auto *LOp = dyn_cast<BinaryOperator>(
                    BO->getLHS()->IgnoreParenImpCasts())) {
              GuardInfo LG = analyzeCondition(LOp, ArgVar, Kind, InThenBranch);
              if (LG.IsGuarded)
                return LG;
            }
            if (const auto *ROp = dyn_cast<BinaryOperator>(
                    BO->getRHS()->IgnoreParenImpCasts())) {
              GuardInfo RG = analyzeCondition(ROp, ArgVar, Kind, InThenBranch);
              if (RG.IsGuarded)
                return RG;
            }
            return Result;
          }
          return analyzeCondition(BO, ArgVar, Kind, InThenBranch);
        }

        // Handle negated condition: if (!(x < 0)) { sqrt(x); }
        if (const auto *UO = dyn_cast<UnaryOperator>(Cond)) {
          if (UO->getOpcode() == UO_LNot) {
            if (const auto *Inner = dyn_cast<BinaryOperator>(
                    UO->getSubExpr()->IgnoreParenImpCasts())) {
              GuardInfo NG = analyzeCondition(Inner, ArgVar, Kind,
                                             !InThenBranch);
              return NG;
            }
          }
        }

        return Result;
      }
    }

    // Move up one level.
    SmallVector<DynTypedNode, 8> NextAncestors;
    for (const auto &Anc : Ancestors)
      for (const auto &P : Ctx.getParents(Anc))
        NextAncestors.push_back(P);
    Ancestors = std::move(NextAncestors);
  }
  return Result;
}

static StringRef funcKindName(MathFuncKind K) {
  return K == MathFuncKind::Sqrt ? "sqrt" : "asin/acos";
}

} // anonymous namespace

void MathDomainGuardCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      callExpr(callee(functionDecl(hasAnyName(
                   "sqrt", "sqrtf", "sqrtl", "asin", "asinf", "asinl", "acos",
                   "acosf", "acosl"))),
               hasArgument(0, expr().bind("arg")))
          .bind("call"),
      this);
}

void MathDomainGuardCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Call = Result.Nodes.getNodeAs<CallExpr>("call");
  const auto *Arg = Result.Nodes.getNodeAs<Expr>("arg");
  if (!Call || !Arg)
    return;

  if (Call->getBeginLoc().isMacroID())
    return;

  // Skip compile-time constants — alpha.security.MathDomain handles those.
  llvm::APFloat ConstVal(0.0);
  if (Arg->EvaluateAsFloat(ConstVal, *Result.Context))
    return;

  // If the argument is fabs(x), it is always non-negative — skip.
  if (isFabsCall(Arg))
    return;

  const auto *FD = Call->getDirectCallee();
  if (!FD)
    return;
  StringRef FnName = FD->getName();

  MathFuncKind Kind;
  if (FnName.starts_with("sqrt"))
    Kind = MathFuncKind::Sqrt;
  else
    Kind = MathFuncKind::InvTrig;

  // Try to extract the referenced variable.
  const VarDecl *ArgVar = extractReferencedVar(Arg);

  if (ArgVar) {
    GuardInfo GI = findGuard(Call, ArgVar, Kind, *Result.Context);

    if (GI.IsGuarded)
      return;

    if (GI.IsReverseGuard) {
      diag(Call->getBeginLoc(),
           "calling %0 inside a branch where the argument is "
           "guaranteed to violate the domain constraint")
          << FnName;
      return;
    }
  }

  // No guard found (or argument is a complex expression).
  if (Kind == MathFuncKind::Sqrt) {
    diag(Call->getBeginLoc(),
         "argument to %0 is not validated to be non-negative before calling")
        << FnName;
  } else {
    diag(Call->getBeginLoc(),
         "argument to %0 is not validated to be in [-1, 1] before calling")
        << FnName;
  }
}

} // namespace clang::tidy::bugprone
