//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UnboundedRecursionCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CallGraph.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

namespace {

// Check if a function body contains a conditional return statement that
// precedes any recursive call.  This is a heuristic for "has termination
// condition".
class TerminationAnalyzer : public RecursiveASTVisitor<TerminationAnalyzer> {
public:
  bool HasConditionalReturn = false;
  bool HasExternalCallInCondition = false;

  bool VisitIfStmt(IfStmt *IS) {
    if (containsReturn(IS->getThen()) ||
        (IS->getElse() && containsReturn(IS->getElse()))) {
      HasConditionalReturn = true;
      // Check whether the condition depends on an external function call.
      if (containsCallExpr(IS->getCond()))
        HasExternalCallInCondition = true;
    }
    return true;
  }

  bool VisitSwitchStmt(SwitchStmt *SS) {
    HasConditionalReturn = true;
    return true;
  }

private:
  static bool containsReturn(const Stmt *S) {
    if (!S)
      return false;
    if (isa<ReturnStmt>(S))
      return true;
    for (const auto *Child : S->children()) {
      if (Child && containsReturn(Child))
        return true;
    }
    return false;
  }

  static bool containsCallExpr(const Expr *E) {
    if (!E)
      return false;
    if (isa<CallExpr>(E))
      return true;
    for (const auto *Child : E->children()) {
      if (const auto *CE = dyn_cast_or_null<Expr>(Child)) {
        if (containsCallExpr(CE))
          return true;
      }
    }
    return false;
  }
};

} // namespace

void UnboundedRecursionCheck::registerMatchers(MatchFinder *Finder) {
  // We match the entire translation unit to build the call graph once.
  Finder->addMatcher(translationUnitDecl().bind("tu"), this);
}

void UnboundedRecursionCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *TU = Result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
  if (!TU)
    return;

  // Build the call graph for the translation unit.
  CallGraph CG;
  CG.addToCallGraph(const_cast<TranslationUnitDecl *>(TU));

  // Track which functions have already been reported to avoid duplicates.
  llvm::SmallPtrSet<const Decl *, 16> Reported;

  // Iterate over strongly connected components (SCCs) of the call graph.
  for (llvm::scc_iterator<CallGraph *> I = llvm::scc_begin(&CG),
                                       E = llvm::scc_end(&CG);
       I != E; ++I) {
    const auto &SCC = *I;

    // Only interested in non-trivial SCCs (actual recursion cycles).
    if (SCC.size() < 2 && !I.hasCycle())
      continue;

    // Collect FunctionDecls in this SCC.
    llvm::SmallVector<const FunctionDecl *, 4> FuncsInSCC;
    for (const auto *Node : SCC) {
      if (const auto *FD = dyn_cast_or_null<FunctionDecl>(Node->getDecl())) {
        if (FD->hasBody())
          FuncsInSCC.push_back(FD);
      }
    }

    if (FuncsInSCC.empty())
      continue;

    bool IsIndirect = FuncsInSCC.size() > 1;

    for (const auto *FD : FuncsInSCC) {
      if (Reported.count(FD))
        continue;
      Reported.insert(FD);

      TerminationAnalyzer Analyzer;
      Analyzer.TraverseStmt(const_cast<Stmt *>(FD->getBody()));

      if (!Analyzer.HasConditionalReturn) {
        // No termination condition at all.
        if (IsIndirect) {
          std::string Cycle;
          for (const auto *F : FuncsInSCC) {
            if (!Cycle.empty())
              Cycle += " -> ";
            Cycle += F->getNameAsString();
          }
          diag(FD->getLocation(),
               "function '%0' is part of an indirect recursion cycle "
               "(%1) with no termination condition")
              << FD->getName() << Cycle;
        } else {
          diag(FD->getLocation(),
               "function '%0' calls itself recursively with no "
               "termination condition; this will cause infinite "
               "recursion and stack overflow")
              << FD->getName();
        }
      } else if (Analyzer.HasExternalCallInCondition) {
        diag(FD->getLocation(),
             "recursion termination condition in '%0' depends on an "
             "external function call; verify that the callee eventually "
             "triggers termination")
            << FD->getName();
      }
      // If HasConditionalReturn && !HasExternalCallInCondition → proper
      // recursion with parameter-based termination, don't report.
    }
  }
}

} // namespace clang::tidy::bugprone
