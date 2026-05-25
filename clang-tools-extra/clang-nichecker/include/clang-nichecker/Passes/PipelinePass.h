#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_PIPELINEPASS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_PASSES_PIPELINEPASS_H

#include "clang-nichecker/Support/Types.h"
#include "llvm/Support/Error.h"

#include <memory>
#include <vector>

namespace clang::nichecker {

class PipelinePass {
public:
  virtual ~PipelinePass() = default;
  virtual llvm::StringRef name() const = 0;
  virtual llvm::Error run(const PipelineContext &Context,
                          TransformResult &Result) const = 0;
};

using PassPipeline = std::vector<std::unique_ptr<PipelinePass>>;

} // namespace clang::nichecker

#endif
