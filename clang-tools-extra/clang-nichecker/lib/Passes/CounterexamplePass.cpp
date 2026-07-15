#include "clang-nichecker/Passes/CounterexamplePass.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;

namespace clang::nichecker {

llvm::StringRef CounterexamplePass::name() const { return "cex"; }

llvm::Error CounterexamplePass::run(const PipelineContext &Context,
                                   TransformResult &Result) const {
  (void)Context;
  Result.RequiresASTReparse = false;
  if (Result.BackendOutput.empty()) {
    Result.Notes.push_back(
        "phase6: cex 未收到 CBMC 输出，跳过反例文本转换");
    return Error::success();
  }

  if (Result.BackendOutcome == "UNSAFE") {
    Result.Notes.push_back(
        formatv("phase6: cex 检测到不安全结果；原始 CBMC 轨迹位于 {0}",
                Result.BackendLogPath)
            .str());
  } else {
    Result.Notes.push_back(
        formatv("phase6: cex 后端结果为 {0}，无需生成反例；日志位于 {1}",
                Result.BackendOutcome, Result.BackendLogPath)
            .str());
  }
  return Error::success();
}

} // namespace clang::nichecker
