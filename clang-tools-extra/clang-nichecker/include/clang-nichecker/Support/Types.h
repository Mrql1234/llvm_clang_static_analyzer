#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_SUPPORT_TYPES_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_SUPPORT_TYPES_H

#include "clang/AST/Decl.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/ADT/StringRef.h"

#include <string>
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
};

struct PipelineContext {
  CompilerInstance &CI;
  const PipelineOptions &Options;
  llvm::StringRef OriginalSource;
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
