#include "clang-nichecker/Passes/SlicePass.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"

#include <string>

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

bool shouldRunSliceJar(const PipelineOptions &Options,
                       const TransformResult &Result) {
  if (Result.Summary.Kind == ProgramKind::Sequential)
    return false;
  return Options.EnableLegacySliceJar &&
         (Options.PipelineProfile == "lazy" ||
          StringRef(Options.PipelineSpec).contains("slice"));
}

std::string buildSliceDataJson(const PipelineContext &Context,
                               const TransformResult &Result) {
  json::Object Root;
  Root["var"] = Context.Options.SliceVariable;
  Root["age"] = 30;
  Root["mode"] = Context.Options.SliceMode;
  Root["main_task"] = 0;

  int Priority = 1;
  for (const std::string &Name : Result.Summary.InterruptFunctions)
    Root[Name] = Priority++;
  return formatv("{0:2}", json::Value(std::move(Root))).str();
}

} // namespace

llvm::StringRef SlicePass::name() const { return "slice"; }

llvm::Error SlicePass::run(const PipelineContext &Context,
                           TransformResult &Result) const {
  if (!shouldRunSliceJar(Context.Options, Result)) {
    Result.Notes.push_back(
        "phase4: slice 按 Python 默认 proSlice=false 跳过 legacy jar，保持源码不变");
    return Error::success();
  }

  LegacyJarInvocationConfig Config;
  Config.PassName = "slice";
  Config.JarName = "testSlice.jar";
  Config.InputPrefix = "istLab_";
  Config.OutputPrefix = "slice_seq_";
  Config.LegacyEntryFunction = "main_task";

  Expected<LegacyJarInvocationResult> Invocation =
      runLegacyJarTransform(Context, Result, Config,
                            buildSliceDataJson(Context, Result));
  if (!Invocation) {
    Result.Notes.push_back(
        formatv("phase4: slice 未能调用 legacy jar，已跳过；原因: {0}",
                toString(Invocation.takeError()))
            .str());
    return Error::success();
  }

  Result.PendingReplacements.clear();
  Result.Source = Invocation->TransformedSource;
  Result.Notes.push_back(formatv(
                             "phase4: slice 已通过 legacy jar 生成输出，目录: {0}",
                             Invocation->WorkDir)
                             .str());
  return Error::success();
}

} // namespace clang::nichecker
