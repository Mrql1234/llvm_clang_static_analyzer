#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_BACKEND_CBMCDRIVERPASS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_BACKEND_CBMCDRIVERPASS_H

#include "clang-nichecker/Passes/PipelinePass.h"

namespace clang::nichecker {

class CBMCDriverPass : public PipelinePass {
public:
  llvm::StringRef name() const override;
  llvm::Error run(const PipelineContext &Context,
                  TransformResult &Result) const override;
};

} // namespace clang::nichecker

#endif
