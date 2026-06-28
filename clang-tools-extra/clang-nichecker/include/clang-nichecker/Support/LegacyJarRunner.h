#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_SUPPORT_LEGACYJARRUNNER_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_SUPPORT_LEGACYJARRUNNER_H

#include "clang-nichecker/Support/Types.h"
#include "llvm/Support/Error.h"

#include <string>

namespace clang::nichecker {

struct LegacyJarInvocationConfig {
  std::string PassName;
  std::string JarName;
  std::string InputPrefix;
  std::string OutputPrefix;
  std::string OutputBaseName;
  std::string LegacyEntryFunction;
  bool OutputContainsOnlyFunctions = true;
};

struct LegacyJarInvocationResult {
  std::string TransformedSource;
  std::string WorkDir;
  std::string InputFile;
  std::string OutputFile;
  std::string StdoutLog;
  std::string StderrLog;
};

std::string materializeSource(const PipelineContext &Context,
                              const TransformResult &Result);

llvm::Expected<LegacyJarInvocationResult>
runLegacyJarTransform(const PipelineContext &Context,
                      const TransformResult &Result,
                      const LegacyJarInvocationConfig &Config,
                      llvm::StringRef DataJson);

} // namespace clang::nichecker

#endif
