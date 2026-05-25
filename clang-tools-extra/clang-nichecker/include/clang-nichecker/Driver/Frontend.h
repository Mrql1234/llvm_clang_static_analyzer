#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_DRIVER_FRONTEND_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_NICHECKER_DRIVER_FRONTEND_H

#include "clang-nichecker/Support/Types.h"
#include "clang/Tooling/Tooling.h"

#include <memory>

namespace clang::nichecker {

std::unique_ptr<clang::tooling::FrontendActionFactory>
createActionFactory(const PipelineOptions &Options);

} // namespace clang::nichecker

#endif
