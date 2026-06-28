#include "clang-nichecker/Analysis/ProgramAnalyzer.h"
#include "clang-nichecker/Support/SourceUtils.h"

using namespace clang;

namespace clang::nichecker {

static bool containsName(const std::vector<std::string> &Names,
                         llvm::StringRef Name) {
  return llvm::is_contained(Names, Name.str());
}

static const FunctionDecl *findMainFunction(ASTContext &Context) {
  SourceManager &SM = Context.getSourceManager();
  for (Decl *D : Context.getTranslationUnitDecl()->decls()) {
    const auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD || !FD->isMain() || !FD->hasBody())
      continue;
    if (isMainFileLocation(FD->getLocation(), SM))
      return FD;
  }
  return nullptr;
}

ProgramAnalyzer::ProgramAnalyzer(ASTContext &Context)
    : Context(Context), SM(Context.getSourceManager()) {}

bool ProgramAnalyzer::VisitFunctionDecl(FunctionDecl *FD) {
  if (FD->isMain() && FD->hasBody() && isMainFileLocation(FD->getLocation(), SM))
    Summary.MainFunction = FD;

  if (!FD->hasBody())
    return true;

  llvm::StringRef Name = FD->getName();
  if (startsWithISR(Name) && !containsName(Summary.InterruptFunctions, Name))
    Summary.InterruptFunctions.push_back(Name.str());
  return true;
}

bool ProgramAnalyzer::VisitCallExpr(CallExpr *Call) {
  const FunctionDecl *DirectCallee = Call->getDirectCallee();
  if (!DirectCallee)
    return true;

  llvm::StringRef Name = DirectCallee->getName();
  if (Name == "pthread_create") {
    Summary.UsesPthreadCreate = true;
    if (Call->getNumArgs() >= 3) {
      if (const auto *ArgRef =
              dyn_cast<DeclRefExpr>(Call->getArg(2)->IgnoreParenImpCasts())) {
        if (const auto *FD = dyn_cast<FunctionDecl>(ArgRef->getDecl())) {
          if (!containsName(Summary.ThreadEntryFunctions, FD->getName()))
            Summary.ThreadEntryFunctions.push_back(FD->getName().str());
        }
      }
    }
  } else if (Name == "enable_isr") {
    Summary.UsesEnableISR = true;
  } else if (Name == "disable_isr") {
    Summary.UsesDisableISR = true;
  }

  return true;
}

ProgramSummary ProgramAnalyzer::finalize() {
  if (!Summary.InterruptFunctions.empty() || Summary.UsesEnableISR ||
      Summary.UsesDisableISR) {
    Summary.Kind = ProgramKind::InterruptDriven;
  } else if (Summary.UsesPthreadCreate) {
    Summary.Kind = ProgramKind::MultiThreaded;
  } else {
    Summary.Kind = ProgramKind::Sequential;
  }

  return Summary;
}

ProgramSummary analyzeProgram(ASTContext &Context) {
  ProgramAnalyzer Analyzer(Context);
  Analyzer.TraverseDecl(Context.getTranslationUnitDecl());
  return Analyzer.finalize();
}

ProgramSummary refreshSummaryForCurrentAST(ASTContext &Context,
                                           const ProgramSummary &Previous) {
  ProgramSummary Refreshed = Previous;
  Refreshed.MainFunction = findMainFunction(Context);
  return Refreshed;
}

} // namespace clang::nichecker
