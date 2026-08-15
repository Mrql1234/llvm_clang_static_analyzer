#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_LOOPABSTRACTIONPASS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_LOOPABSTRACTIONPASS_H

#include "clang-nichecker/Passes/PipelinePass.h"

namespace clang::nichecker {

/// Builds the nondeterministic loop summaries used by the legacy lazy chain.
class LoopAbstractionPass : public PipelinePass {
public:
  llvm::StringRef name() const override;
  llvm::Error run(const PipelineContext &Context,
                  TransformResult &Result) const override;
};

} // namespace clang::nichecker

#endif
