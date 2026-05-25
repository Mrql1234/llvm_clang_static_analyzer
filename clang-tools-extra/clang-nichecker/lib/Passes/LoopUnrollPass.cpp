#include "clang-nichecker/Passes/LoopUnrollPass.h"

namespace clang::nichecker {

llvm::StringRef LoopUnrollPass::name() const { return "loop-unroll"; }

llvm::Error LoopUnrollPass::run(const PipelineContext &Context,
                                TransformResult &Result) const {
  (void)Context;
  Result.Notes.push_back(
      "phase4: loop-unroll 模块骨架已接入，当前保持源码不变");
  return llvm::Error::success();
}

} // namespace clang::nichecker
