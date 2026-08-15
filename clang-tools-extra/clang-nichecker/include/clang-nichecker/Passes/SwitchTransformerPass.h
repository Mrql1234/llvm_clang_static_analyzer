#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_SWITCHTRANSFORMERPASS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_SWITCHTRANSFORMERPASS_H

#include "clang-nichecker/Passes/PipelinePass.h"

namespace clang::nichecker {
class SwitchTransformerPass : public PipelinePass {
public:
  llvm::StringRef name() const override;
  llvm::Error run(const PipelineContext &, TransformResult &) const override;
};
} // namespace clang::nichecker
#endif
