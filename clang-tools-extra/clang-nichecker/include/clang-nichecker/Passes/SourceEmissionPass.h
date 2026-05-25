#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_SOURCEEMISSIONPASS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_SOURCEEMISSIONPASS_H

#include "clang-nichecker/Passes/PipelinePass.h"

namespace clang::nichecker {

class SourceEmissionPass : public PipelinePass {
public:
  llvm::StringRef name() const override;
  llvm::Error run(const PipelineContext &Context,
                  TransformResult &Result) const override;
};

} // namespace clang::nichecker

#endif
