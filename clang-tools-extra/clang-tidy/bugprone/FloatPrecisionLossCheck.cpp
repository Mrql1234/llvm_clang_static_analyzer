//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FloatPrecisionLossCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

void FloatPrecisionLossCheck::registerMatchers(MatchFinder *Finder) {
  // Match implicit casts from double to float (including in variable
  // initialization and assignment).
  Finder->addMatcher(
      implicitCastExpr(hasImplicitDestinationType(asString("float")),
                       hasSourceExpression(hasType(asString("double"))))
          .bind("cast"),
      this);
}

void FloatPrecisionLossCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Cast = Result.Nodes.getNodeAs<ImplicitCastExpr>("cast");
  if (!Cast)
    return;

  if (Cast->getBeginLoc().isMacroID())
    return;

  // Exclude if there is an explicit cast wrapping this (user expressed intent).
  for (const auto &Parent : Result.Context->getParents(*Cast)) {
    if (Parent.get<ExplicitCastExpr>())
      return;
  }

  // Exclude zero constants (0.0) — precision loss is irrelevant.
  const Expr *Src = Cast->getSubExpr()->IgnoreParenImpCasts();
  if (const auto *FL = dyn_cast<FloatingLiteral>(Src)) {
    if (FL->getValue().isZero())
      return;
  }

  diag(Cast->getBeginLoc(),
       "implicit conversion from 'double' to 'float' may cause precision "
       "loss; consider using an explicit static_cast or a float-precision "
       "expression");
}

} // namespace clang::tidy::bugprone
