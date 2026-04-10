//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FloatPrecisionPromotionCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

void FloatPrecisionPromotionCheck::registerMatchers(MatchFinder *Finder) {
  // Match binary arithmetic operators where the result is double and at least
  // one operand originates from a float type (implicitly promoted).
  Finder->addMatcher(
      binaryOperator(
          anyOf(hasOperatorName("+"), hasOperatorName("-"),
                hasOperatorName("*"), hasOperatorName("/")),
          hasType(asString("double")),
          anyOf(hasLHS(ignoringImpCasts(hasType(asString("float")))),
                hasRHS(ignoringImpCasts(hasType(asString("float"))))))
          .bind("binop"),
      this);
}

void FloatPrecisionPromotionCheck::check(
    const MatchFinder::MatchResult &Result) {
  const auto *BO = Result.Nodes.getNodeAs<BinaryOperator>("binop");
  if (!BO)
    return;

  if (BO->getBeginLoc().isMacroID())
    return;

  // Skip if inside a variadic function call (printf, etc.) where float→double
  // promotion is standard C behavior.
  for (const auto &Parent : Result.Context->getParents(*BO)) {
    if (const auto *Call = Parent.get<CallExpr>()) {
      if (const auto *FD = Call->getDirectCallee()) {
        if (FD->isVariadic())
          return;
      }
    }
  }

  // Skip if either operand has an explicit cast.
  if (isa<ExplicitCastExpr>(BO->getLHS()->IgnoreParens()) ||
      isa<ExplicitCastExpr>(BO->getRHS()->IgnoreParens()))
    return;

  diag(BO->getOperatorLoc(),
       "float operand implicitly promoted to double in arithmetic; "
       "consider using float-precision constants (e.g. 1.0f) or "
       "an explicit cast to avoid unintended double-precision computation");
}

} // namespace clang::tidy::bugprone
