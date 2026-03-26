//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_MATHDOMAINGUARDCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_MATHDOMAINGUARDCHECK_H

#include "../ClangTidyCheck.h"

namespace clang::tidy::bugprone {

/// Checks that arguments to math domain functions (sqrt, asin, acos) are
/// validated before the call.  Detects unguarded calls and calls inside
/// branches that guarantee a domain violation (e.g. calling sqrt(x) inside
/// an `if (x < 0)` block).
///
/// Complements the CSA-based alpha.security.MathDomain checker which only
/// catches compile-time constant violations.
///
/// For the user-facing documentation see:
/// https://clang.llvm.org/extra/clang-tidy/checks/bugprone/math-domain-guard.html
class MathDomainGuardCheck : public ClangTidyCheck {
public:
  MathDomainGuardCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
};

} // namespace clang::tidy::bugprone

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_MATHDOMAINGUARDCHECK_H
