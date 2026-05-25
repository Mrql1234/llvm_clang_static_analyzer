#include "clang-nichecker/Passes/SequentializationPass.h"

namespace clang::nichecker {

llvm::StringRef SequentializationPass::name() const {
  return "sequentialization";
}

llvm::Error SequentializationPass::run(const PipelineContext &Context,
                                       TransformResult &Result) const {
  (void)Context;
  Result.Notes.push_back(
      "phase5: sequentialization 模块骨架已接入，当前保持源码不变");
  return llvm::Error::success();
}

} // namespace clang::nichecker
