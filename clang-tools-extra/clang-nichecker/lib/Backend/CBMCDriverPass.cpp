#include "clang-nichecker/Backend/CBMCDriverPass.h"

namespace clang::nichecker {

llvm::StringRef CBMCDriverPass::name() const { return "cbmc-driver"; }

llvm::Error CBMCDriverPass::run(const PipelineContext &Context,
                                TransformResult &Result) const {
  (void)Result;
  if (!Context.Options.EnableCBMC)
    return llvm::Error::success();

  Result.Notes.push_back(
      "phase6: cbmc-driver 模块骨架已接入，当前尚未调用 CBMC");
  return llvm::Error::success();
}

} // namespace clang::nichecker
