#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_DRIVER_PIPELINEBUILDER_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_DRIVER_PIPELINEBUILDER_H

#include "clang-nichecker/Passes/PipelinePass.h"
#include "clang-nichecker/Support/Types.h"
#include "llvm/Support/Error.h"

namespace clang::nichecker {

llvm::Expected<PassPipeline> buildPipeline(const PipelineOptions &Options);

} // namespace clang::nichecker

#endif
