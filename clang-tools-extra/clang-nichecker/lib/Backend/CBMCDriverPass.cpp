#include "clang-nichecker/Backend/CBMCDriverPass.h"
#include "clang-nichecker/Backend/CBMCRunner.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;

namespace clang::nichecker {

llvm::StringRef CBMCDriverPass::name() const { return "cbmc-driver"; }

llvm::Error CBMCDriverPass::run(const PipelineContext &Context,
                                TransformResult &Result) const {
  if (!Context.Options.EnableCBMC)
    return Error::success();

  std::string Source = materializeSource(Context, Result);
  if (Error Err = writeFile(Context.Options.OutputPath, Source))
    return Err;

  CBMCRunConfig Config;
  Config.SourceFilePath = Context.Options.OutputPath;
  Config.Backend = Context.Options.Backend;
  Config.Unwind = Context.Options.Unwind;
  Config.BoundsCheck = true;
  Config.DivByZeroCheck = true;
  Config.PointerCheck = true;
  Config.ConversionCheck = true;
  Config.Trace = true;
  Config.NoLibrary = true;
  Config.ObjectBits = 10;

  Expected<CBMCRunResult> Run =
      runCBMC(Config, Context.Options.OutputPath + ".cbmc");
  if (!Run) {
    Result.Notes.push_back(
        formatv("phase6: 未能调用 CBMC，已跳过后端验证；原因: {0}",
                toString(Run.takeError()))
            .str());
    return Error::success();
  }

  if (Run->ExecutionFailed || Run->ExitCode < 0) {
    Result.Notes.push_back(
        formatv("phase6: CBMC 调用失败，exit={0}，错误信息: {1}，日志: {2}",
                Run->ExitCode,
                Run->ErrorMessage.empty() ? "未知错误" : Run->ErrorMessage,
                Run->CombinedLogPath)
            .str());
    return Error::success();
  }

  Result.Notes.push_back(
      formatv("phase6: 已调用 CBMC，结果 {0}，exit={1}，日志: {2}",
              verificationOutcomeToString(Run->Outcome), Run->ExitCode,
              Run->CombinedLogPath)
          .str());
  Result.BackendOutput = std::move(Run->CombinedOutput);
  Result.BackendLogPath = Run->CombinedLogPath;
  Result.BackendOutcome = verificationOutcomeToString(Run->Outcome).str();
  return Error::success();
}

} // namespace clang::nichecker
