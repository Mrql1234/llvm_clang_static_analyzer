#include "clang-nichecker/Passes/LabelInsertionPass.h"

namespace clang::nichecker {

llvm::StringRef LabelInsertionPass::name() const { return "label-insertion"; }

llvm::Error LabelInsertionPass::run(const PipelineContext &Context,
                                    TransformResult &Result) const {
  (void)Context;
  Result.Notes.push_back(
      "phase4: label-insertion 模块骨架已接入，当前保持源码不变");
  return llvm::Error::success();
}

} // namespace clang::nichecker
