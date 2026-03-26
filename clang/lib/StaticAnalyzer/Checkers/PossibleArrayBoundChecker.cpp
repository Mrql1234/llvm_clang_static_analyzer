//== PossibleArrayBoundChecker.cpp - Possible out-of-bounds ------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Warns when an array subscript index *may* be out of bounds — i.e. the
// constraint solver cannot prove the index is within [0, extent).  This
// complements security.ArrayBound which only fires on *proven* violations.
//
// Intended for safety-critical projects where every array access must be
// preceded by a proven bounds check.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {

class PossibleArrayBoundChecker
    : public Checker<check::PostStmt<ArraySubscriptExpr>> {
  BugType BT{this, "Possible out-of-bound access"};

  void reportBug(StringRef Msg, CheckerContext &C,
                 const ArraySubscriptExpr *ASE) const;

public:
  void checkPostStmt(const ArraySubscriptExpr *ASE, CheckerContext &C) const;
};

} // anonymous namespace

void PossibleArrayBoundChecker::reportBug(StringRef Msg, CheckerContext &C,
                                          const ArraySubscriptExpr *ASE) const {
  if (ExplodedNode *N = C.generateNonFatalErrorNode()) {
    auto R = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);
    bugreporter::trackExpressionValue(N, ASE->getIdx(), *R);
    C.emitReport(std::move(R));
  }
}

void PossibleArrayBoundChecker::checkPostStmt(const ArraySubscriptExpr *ASE,
                                              CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  SValBuilder &SVB = C.getSValBuilder();

  SVal IdxVal = C.getSVal(ASE->getIdx());
  auto IdxDV = IdxVal.getAs<DefinedSVal>();
  if (!IdxDV)
    return;

  // --- Lower bound: index >= 0 ---
  QualType IdxTy = ASE->getIdx()->getType();
  if (IdxTy->isSignedIntegerType()) {
    SVal Zero = SVB.makeZeroVal(IdxTy);
    auto ZeroDV = Zero.getAs<DefinedSVal>();
    if (ZeroDV) {
      SVal IsNeg = SVB.evalBinOp(State, BO_LT, *IdxDV, *ZeroDV,
                                 SVB.getConditionType());
      if (auto IsNegDef = IsNeg.getAs<DefinedSVal>()) {
        auto [stateNeg, stateNonNeg] = State->assume(*IsNegDef);
        if (stateNeg && !stateNonNeg) {
          // Definitely negative — security.ArrayBound handles this.
          return;
        }
        if (stateNeg && stateNonNeg) {
          reportBug("Array index may be negative", C, ASE);
          State = stateNonNeg;
        }
      }
    }
  }

  // --- Upper bound: index < element_count ---
  const MemRegion *BaseRegion = C.getSVal(ASE->getBase()).getAsRegion();
  if (!BaseRegion)
    return;
  const MemRegion *SuperRegion = BaseRegion;
  if (const auto *ER = dyn_cast<ElementRegion>(BaseRegion))
    SuperRegion = ER->getSuperRegion();
  const auto *SR = dyn_cast<SubRegion>(SuperRegion);
  if (!SR)
    return;

  DefinedOrUnknownSVal ExtentVal = getDynamicExtent(State, SR, SVB);
  auto ExtentDV = ExtentVal.getAs<DefinedSVal>();
  if (!ExtentDV)
    return;

  // Compare index directly against element count (extent / elem_size)
  // to keep the constraint solver working on the original symbol.
  QualType ElemTy = ASE->getType();
  CharUnits ElemSize = C.getASTContext().getTypeSizeInChars(ElemTy);
  if (ElemSize.isZero())
    return;

  NonLoc ElemSizeVal =
      SVB.makeIntVal(ElemSize.getQuantity(), SVB.getArrayIndexType())
          .castAs<NonLoc>();
  SVal ElemCount = SVB.evalBinOp(State, BO_Div, *ExtentDV, ElemSizeVal,
                                 SVB.getArrayIndexType());
  auto ElemCountDV = ElemCount.getAs<DefinedSVal>();
  if (!ElemCountDV)
    return;

  auto IdxNL = IdxDV->getAs<NonLoc>();
  if (!IdxNL)
    return;

  SVal IsOverflow = SVB.evalBinOp(State, BO_GE, *IdxNL, *ElemCountDV,
                                  SVB.getConditionType());
  if (auto IsOverDef = IsOverflow.getAs<DefinedSVal>()) {
    auto [stateOver, stateInBound] = State->assume(*IsOverDef);
    if (stateOver && !stateInBound) {
      // Definitely out of bounds — security.ArrayBound handles this.
      return;
    }
    if (stateOver && stateInBound) {
      reportBug("Array index may exceed upper bound", C, ASE);
    }
  }
}

void ento::registerPossibleArrayBoundChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<PossibleArrayBoundChecker>();
}

bool ento::shouldRegisterPossibleArrayBoundChecker(const CheckerManager &) {
  return true;
}
