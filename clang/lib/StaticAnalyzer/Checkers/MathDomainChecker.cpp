//== MathDomainChecker.cpp - Math function domain checker --------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Checks for math function domain errors: sqrt of a negative value, and
// asin/acos called with an argument outside [-1, 1].
//
// The Clang Static Analyzer's constraint solver does not symbolicate
// floating-point values (SymbolManager::canSymbolicate returns false for
// floats), so this checker evaluates arguments as compile-time float
// constants via the AST constant evaluator.  It catches literal violations
// like sqrt(-1.0) and asin(2.0) but cannot perform path-sensitive reasoning
// on symbolic float variables.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {

class MathDomainChecker : public Checker<check::PreCall> {
  BugType BT_Sqrt{this, "Invalid sqrt argument"};
  BugType BT_InvTrig{this, "Invalid inverse trigonometric argument"};

  const CallDescription Sqrt{CDM::CLibrary, {"sqrt"}, 1};
  const CallDescription Sqrtf{CDM::CLibrary, {"sqrtf"}, 1};
  const CallDescription Sqrtl{CDM::CLibrary, {"sqrtl"}, 1};

  const CallDescription Asin{CDM::CLibrary, {"asin"}, 1};
  const CallDescription Asinf{CDM::CLibrary, {"asinf"}, 1};
  const CallDescription Acos{CDM::CLibrary, {"acos"}, 1};
  const CallDescription Acosf{CDM::CLibrary, {"acosf"}, 1};

  void checkSqrtArg(const CallEvent &Call, CheckerContext &C) const;
  void checkInvTrigArg(const CallEvent &Call, CheckerContext &C,
                       StringRef FnName) const;

  void reportBug(const BugType &BT, StringRef Msg, CheckerContext &C,
                 const Expr *ArgExpr) const;

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
};

} // anonymous namespace

void MathDomainChecker::checkPreCall(const CallEvent &Call,
                                     CheckerContext &C) const {
  if (Sqrt.matches(Call) || Sqrtf.matches(Call) || Sqrtl.matches(Call)) {
    checkSqrtArg(Call, C);
    return;
  }

  if (Asin.matches(Call) || Asinf.matches(Call)) {
    checkInvTrigArg(Call, C, "asin");
    return;
  }

  if (Acos.matches(Call) || Acosf.matches(Call)) {
    checkInvTrigArg(Call, C, "acos");
    return;
  }
}

void MathDomainChecker::checkSqrtArg(const CallEvent &Call,
                                      CheckerContext &C) const {
  const Expr *ArgExpr = Call.getArgExpr(0);
  if (!ArgExpr)
    return;

  llvm::APFloat Val(0.0);
  if (!ArgExpr->EvaluateAsFloat(Val, C.getASTContext()))
    return;

  if (Val.isNegative() && !Val.isZero())
    reportBug(BT_Sqrt, "Argument to sqrt is negative", C, ArgExpr);
}

void MathDomainChecker::checkInvTrigArg(const CallEvent &Call,
                                         CheckerContext &C,
                                         StringRef FnName) const {
  const Expr *ArgExpr = Call.getArgExpr(0);
  if (!ArgExpr)
    return;

  llvm::APFloat Val(0.0);
  if (!ArgExpr->EvaluateAsFloat(Val, C.getASTContext()))
    return;

  const llvm::fltSemantics &Sem = Val.getSemantics();
  llvm::APFloat One(Sem, 1);
  llvm::APFloat NegOne = One;
  NegOne.changeSign();

  if (Val.compare(One) == llvm::APFloat::cmpGreaterThan ||
      Val.compare(NegOne) == llvm::APFloat::cmpLessThan) {
    SmallString<64> Buf;
    llvm::raw_svector_ostream OS(Buf);
    OS << "Argument to " << FnName << " is out of the range [-1, 1]";
    reportBug(BT_InvTrig, OS.str(), C, ArgExpr);
  }
}

void MathDomainChecker::reportBug(const BugType &BT, StringRef Msg,
                                   CheckerContext &C,
                                   const Expr *ArgExpr) const {
  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;

  auto R = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);
  bugreporter::trackExpressionValue(N, ArgExpr, *R);
  C.emitReport(std::move(R));
}

void ento::registerMathDomainChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<MathDomainChecker>();
}

bool ento::shouldRegisterMathDomainChecker(const CheckerManager &Mgr) {
  return true;
}
