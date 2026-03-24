//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LargeStackVariableCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

LargeStackVariableCheck::LargeStackVariableCheck(StringRef Name,
                                                 ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context),
      Threshold(Options.get("LargeStackVariableThreshold", 1048576U)) {}

void LargeStackVariableCheck::storeOptions(
    ClangTidyOptions::OptionMap &Opts) {
  Options.store(Opts, "LargeStackVariableThreshold", Threshold);
}

void LargeStackVariableCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(varDecl(hasLocalStorage()).bind("var"), this);
  Finder->addMatcher(
      callExpr(callee(functionDecl(hasAnyName("alloca", "__builtin_alloca",
                                              "__builtin_alloca_with_align"))))
          .bind("alloca"),
      this);
}

void LargeStackVariableCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("alloca")) {
    diag(Call->getBeginLoc(),
         "use of alloca for dynamic stack allocation is potentially unsafe");
    return;
  }

  const auto *VD = Result.Nodes.getNodeAs<VarDecl>("var");
  if (!VD)
    return;

  const ASTContext &Ctx = *Result.Context;
  QualType Ty = VD->getType();

  if (Ty->isVariableArrayType()) {
    diag(VD->getLocation(),
         "variable-length array '%0' has unpredictable stack usage")
        << VD->getName();
    return;
  }

  if (Ty->isIncompleteType())
    return;

  const auto &TI = Ctx.getTypeInfo(Ty);
  uint64_t SizeInBytes = TI.Width / 8;

  if (SizeInBytes >= Threshold) {
    diag(VD->getLocation(),
         "local variable '%0' uses %1 bytes of stack space, exceeding "
         "threshold of %2 bytes")
        << VD->getName() << SizeInBytes << Threshold;
  }
}

} // namespace clang::tidy::bugprone
