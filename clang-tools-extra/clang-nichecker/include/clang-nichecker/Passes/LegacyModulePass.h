#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_LEGACYMODULEPASS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_LEGACYMODULEPASS_H

#include "clang-nichecker/Passes/PipelinePass.h"

#include <string>

namespace clang::nichecker {

class LegacyModulePass : public PipelinePass {
public:
  LegacyModulePass(std::string PassName, std::string Note);

  llvm::StringRef name() const override;
  llvm::Error run(const PipelineContext &Context,
                  TransformResult &Result) const override;

private:
  std::string PassName;
  std::string Note;
};

} // namespace clang::nichecker

#endif
