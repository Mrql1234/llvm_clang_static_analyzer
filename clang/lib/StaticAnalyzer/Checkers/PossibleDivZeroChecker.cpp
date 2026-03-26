//== PossibleDivZeroChecker.cpp - Possible division by zero ------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Warns when a divisor *may* be zero — i.e. the constraint solver cannot
// prove it is definitely non-zero.  This complements core.DivideZero which
// only fires when the divisor is *proven* to be zero.
//
// Intended for safety-critical projects (MISRA, CERT) where every division
// must be preceded by a proven non-zero check.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {

class PossibleDivZeroChecker : public Checker<check::PreStmt<BinaryOperator>> {
  BugType BT{this, "Possible division by zero"};

  void reportBug(StringRef Msg, ProgramStateRef StateZero,
                 CheckerContext &C, const Expr *DenomExpr) const;

public:
  void checkPreStmt(const BinaryOperator *B, CheckerContext &C) const;
};

} // anonymous namespace

void PossibleDivZeroChecker::reportBug(StringRef Msg,
                                       ProgramStateRef StateZero,
                                       CheckerContext &C,
                                       const Expr *DenomExpr) const {
  if (ExplodedNode *N = C.generateNonFatalErrorNode(StateZero)) {
    auto R = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);
    bugreporter::trackExpressionValue(N, DenomExpr, *R);
    C.emitReport(std::move(R));
  }
}

void PossibleDivZeroChecker::checkPreStmt(const BinaryOperator *B,
                                          CheckerContext &C) const {
  BinaryOperator::Opcode Op = B->getOpcode();
  if (Op != BO_Div && Op != BO_Rem &&
      Op != BO_DivAssign && Op != BO_RemAssign)
    return;

  if (!B->getRHS()->getType()->isScalarType())
    return;

  if (B->getRHS()->getType()->isFloatingType())
    return;

  SVal Denom = C.getSVal(B->getRHS());
  std::optional<DefinedSVal> DV = Denom.getAs<DefinedSVal>();
  if (!DV)
    return;

  ConstraintManager &CM = C.getConstraintManager();
  ProgramStateRef stateNotZero, stateZero;
  std::tie(stateNotZero, stateZero) = CM.assumeDual(C.getState(), *DV);

  if (!stateNotZero) {
    // Definitely zero — core.DivideZero handles this; skip to avoid duplicate.
    return;
  }

  if (stateNotZero && stateZero) {
    reportBug("Division by a value that may be zero", stateZero, C,
              B->getRHS());
  }

  C.addTransition(stateNotZero);
}

void ento::registerPossibleDivZeroChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<PossibleDivZeroChecker>();
}

bool ento::shouldRegisterPossibleDivZeroChecker(const CheckerManager &) {
  return true;
}
