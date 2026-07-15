#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_BACKEND_CBMCRUNNER_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_BACKEND_CBMCRUNNER_H

#include "llvm/Support/Error.h"

#include <optional>
#include <string>
#include <vector>

namespace clang::nichecker {

enum class VerificationOutcome {
  Safe,
  Unsafe,
  Unknown,
};

struct CBMCRunConfig {
  std::string SourceFilePath;
  std::string EntryFunction;
  std::string Backend = "cbmc";
  std::string DimacsOutputPath;
  std::vector<std::string> ExtraArgs;
  unsigned Unwind = 1;
  unsigned Depth = 0;
  bool BoundsCheck = true;
  bool DivByZeroCheck = true;
  bool PointerCheck = true;
  bool ConversionCheck = true;
  bool Trace = true;
  bool NoLibrary = true;
  unsigned ObjectBits = 10;
};

struct CBMCRunResult {
  std::string Executable;
  std::string SourceFilePath;
  std::string StdoutLogPath;
  std::string StderrLogPath;
  std::string CombinedLogPath;
  std::string CombinedOutput;
  std::string ErrorMessage;
  int ExitCode = 0;
  bool ExecutionFailed = false;
  VerificationOutcome Outcome = VerificationOutcome::Unknown;
};

std::optional<std::string> locateCBMCExecutable();
std::optional<std::string> locateCBMCExecutable(llvm::StringRef Backend);

llvm::Expected<CBMCRunResult> runCBMC(const CBMCRunConfig &Config,
                                      llvm::StringRef LogBasePath);

llvm::StringRef verificationOutcomeToString(VerificationOutcome Outcome);

} // namespace clang::nichecker

#endif
