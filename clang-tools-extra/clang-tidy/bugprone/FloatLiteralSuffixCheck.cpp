//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FloatLiteralSuffixCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

void FloatLiteralSuffixCheck::registerMatchers(MatchFinder *Finder) {
  // Match float literals (of double type, i.e. no 'f' suffix) that appear
  // inside an implicit cast to float.
  Finder->addMatcher(
      implicitCastExpr(
          hasImplicitDestinationType(asString("float")),
          hasSourceExpression(
              floatLiteral(hasType(asString("double"))).bind("lit")))
          .bind("cast"),
      this);
}

void FloatLiteralSuffixCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Lit = Result.Nodes.getNodeAs<FloatingLiteral>("lit");
  if (!Lit)
    return;

  if (Lit->getBeginLoc().isMacroID())
    return;

  // Exclude zero values (0.0) — precision is identical.
  if (Lit->getValue().isZero())
    return;

  diag(Lit->getBeginLoc(),
       "floating-point literal without 'f' suffix has type 'double' in a "
       "'float' context; add 'f' suffix to avoid implicit conversion")
      << FixItHint::CreateInsertion(Lit->getEndLoc(), "f");
}

} // namespace clang::tidy::bugprone
