//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_FLOATPRECISIONPROMOTIONCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_FLOATPRECISIONPROMOTIONCHECK_H

#include "../ClangTidyCheck.h"

namespace clang::tidy::bugprone {

/// Detects float variables participating in double-precision arithmetic,
/// which causes implicit precision promotion and may indicate unintended
/// mixed-precision computation.
class FloatPrecisionPromotionCheck : public ClangTidyCheck {
public:
  FloatPrecisionPromotionCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
};

} // namespace clang::tidy::bugprone

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_FLOATPRECISIONPROMOTIONCHECK_H
