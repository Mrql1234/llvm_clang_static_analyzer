#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_SUPPORT_TYPES_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_SUPPORT_TYPES_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/ADT/StringRef.h"

#include <string>
#include <map>
#include <vector>

namespace clang::nichecker {

enum class ProgramKind {
  Sequential,
  MultiThreaded,
  InterruptDriven,
};

struct ProgramSummary {
  ProgramKind Kind = ProgramKind::Sequential;
  bool UsesPthreadCreate = false;
  bool UsesEnableISR = false;
  bool UsesDisableISR = false;
  const FunctionDecl *MainFunction = nullptr;
  std::vector<std::string> InterruptFunctions;
  std::vector<std::string> ThreadEntryFunctions;
};

struct PipelineOptions {
  std::string InputPath;
  std::string OutputPath;
  std::string PipelineSpec;
  std::string PipelineProfile;
  unsigned Unwind = 1;
  unsigned Rounds = 1;
  unsigned UnwindWhile = 1;
  unsigned UnwindFor = 1;
  unsigned Contexts = 0;
  unsigned Cores = 1;
  unsigned Threads = 0;
  std::string Backend = "cbmc";
  std::string SliceVariable;
  std::string SliceMode;
  bool ReuseDimacs = false;
  bool EnableLegacySliceJar = false;
  bool EnableLegacyLabelJar = false;
  bool EnableLoopAbstraction = false;
  bool PrintAnalysis = false;
  bool EnableCBMC = false;
};

struct TextReplacement {
  unsigned Offset = 0;
  unsigned Length = 0;
  std::string Text;
};

struct TransformResult {
  ProgramSummary Summary;
  std::string Source;
  std::vector<std::string> Notes;
  std::vector<TextReplacement> PendingReplacements;
  std::vector<std::string> BackendAssumptions;
  std::string BackendOutput;
  std::string BackendLogPath;
  std::string BackendOutcome;
  std::map<unsigned, std::string> SourceLineFunctions;
  unsigned EntryLine = 0;
  bool RequiresASTReparse = true;
};

class TranslationUnitHandle {
public:
  explicit TranslationUnitHandle(CompilerInstance &CI) : CI(&CI) {}
  explicit TranslationUnitHandle(ASTUnit &Unit) : Unit(&Unit) {}

  ASTContext &getASTContext() const {
    return CI ? CI->getASTContext() : Unit->getASTContext();
  }

  SourceManager &getSourceManager() const {
    return CI ? CI->getSourceManager() : Unit->getSourceManager();
  }

  const LangOptions &getLangOpts() const {
    return CI ? CI->getLangOpts() : Unit->getLangOpts();
  }

private:
  CompilerInstance *CI = nullptr;
  ASTUnit *Unit = nullptr;
};

struct PipelineContext {
  const TranslationUnitHandle &TU;
  const PipelineOptions &Options;
  llvm::StringRef CurrentSource;

  ASTContext &getASTContext() const { return TU.getASTContext(); }
  SourceManager &getSourceManager() const { return TU.getSourceManager(); }
  const LangOptions &getLangOpts() const { return TU.getLangOpts(); }
};

inline llvm::StringRef toString(ProgramKind Kind) {
  switch (Kind) {
  case ProgramKind::Sequential:
    return "sequential";
  case ProgramKind::MultiThreaded:
    return "multithreaded";
  case ProgramKind::InterruptDriven:
    return "interrupt-driven";
  }
  llvm_unreachable("unknown program kind");
}

std::string joinList(const std::vector<std::string> &Items);
std::string joinNotes(const std::vector<std::string> &Notes);

} // namespace clang::nichecker

#endif
