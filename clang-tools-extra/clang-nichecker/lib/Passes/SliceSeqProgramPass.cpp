#include "clang-nichecker/Passes/SliceSeqProgramPass.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"

#include <string>

using namespace llvm;

namespace clang::nichecker {

namespace {

bool shouldRunSliceSeqProgramJar(const PipelineOptions &Options,
                                 const TransformResult &Result) {
  if (Result.Summary.Kind != ProgramKind::Sequential)
    return false;
  if (Options.PipelineProfile == "shenfei")
    return true;
  return StringRef(Options.PipelineSpec).contains("slice_seqprogram");
}

std::string detectEntryFunctionName(const TransformResult &Result) {
  if (Result.Summary.MainFunction &&
      !Result.Summary.MainFunction->getName().empty()) {
    return Result.Summary.MainFunction->getNameAsString();
  }
  return "main";
}

std::string buildSliceSeqProgramDataJson(const TransformResult &Result) {
  json::Object Root;
  Root["entryFunc"] = detectEntryFunctionName(Result);
  Root["boundCheck"] = false;
  Root["divZeroCheck"] = false;
  Root["age"] = 30;
  return formatv("{0:2}", json::Value(std::move(Root))).str();
}

} // namespace

llvm::StringRef SliceSeqProgramPass::name() const { return "slice_seqprogram"; }

llvm::Error SliceSeqProgramPass::run(const PipelineContext &Context,
                                     TransformResult &Result) const {
  if (!shouldRunSliceSeqProgramJar(Context.Options, Result)) {
    Result.Notes.push_back(
        "phase4: slice_seqprogram 当前仅在顺序程序的 shenfei/显式 slice_seqprogram 链路启用，其余场景保持源码不变");
    return Error::success();
  }

  LegacyJarInvocationConfig Config;
  Config.PassName = "slice-seqprogram";
  Config.JarName = "testSlice_seqProgramSlice.jar";
  Config.InputPrefix = "_inlined";
  Config.OutputPrefix = "slice__inlined";
  Config.OutputBaseName =
      std::string(sys::path::filename(Context.Options.InputPath));

  Expected<LegacyJarInvocationResult> Invocation =
      runLegacyJarTransform(Context, Result, Config,
                            buildSliceSeqProgramDataJson(Result));
  if (!Invocation) {
    Result.Notes.push_back(
        formatv("phase4: slice_seqprogram 未能调用 legacy jar，已跳过；原因: {0}",
                toString(Invocation.takeError()))
            .str());
    return Error::success();
  }

  Result.PendingReplacements.clear();
  Result.Source = Invocation->TransformedSource;
  Result.Notes.push_back(formatv(
                             "phase4: slice_seqprogram 已通过 legacy jar 生成输出，目录: {0}",
                             Invocation->WorkDir)
                             .str());
  return Error::success();
}

} // namespace clang::nichecker
