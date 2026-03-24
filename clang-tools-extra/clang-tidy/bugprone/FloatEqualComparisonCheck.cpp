//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FloatEqualComparisonCheck.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

void FloatEqualComparisonCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      binaryOperator(anyOf(hasOperatorName("=="), hasOperatorName("!=")),
                     hasEitherOperand(hasType(realFloatingPointType())))
          .bind("binop"),
      this);
}

void FloatEqualComparisonCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *BO = Result.Nodes.getNodeAs<BinaryOperator>("binop");
  if (!BO)
    return;

  if (BO->getBeginLoc().isMacroID())
    return;

  // Exclude NaN detection pattern: x != x
  const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

  if (BO->getOpcode() == BO_NE) {
    if (const auto *LDRE = dyn_cast<DeclRefExpr>(LHS)) {
      if (const auto *RDRE = dyn_cast<DeclRefExpr>(RHS)) {
        if (LDRE->getDecl() == RDRE->getDecl())
          return;
      }
    }
  }

  StringRef OpStr = BO->getOpcodeStr();
  diag(BO->getOperatorLoc(),
       "comparing floating-point values with '%0' is unreliable due to "
       "precision loss; consider using a tolerance-based comparison instead")
      << OpStr;
}

} // namespace clang::tidy::bugprone
