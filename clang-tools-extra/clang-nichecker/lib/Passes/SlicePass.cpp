#include "clang-nichecker/Passes/SlicePass.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"

#include <string>
#include <vector>

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

bool shouldRunSliceJar(const PipelineOptions &Options,
                       const TransformResult &Result) {
  if (Result.Summary.Kind == ProgramKind::Sequential)
    return false;
  if (Options.PipelineProfile == "lazy")
    return true;
  return StringRef(Options.PipelineSpec).contains("slice");
}

std::string collectGlobalVariables(ASTContext &AST) {
  std::vector<std::string> Names;
  for (Decl *D : AST.getTranslationUnitDecl()->decls()) {
    const auto *VD = dyn_cast<VarDecl>(D);
    if (!VD || !VD->hasGlobalStorage() || VD->isStaticDataMember())
      continue;
    if (!VD->getName().empty())
      Names.push_back(VD->getNameAsString());
  }
  return joinList(Names);
}

std::string buildSliceDataJson(const PipelineContext &Context,
                               const TransformResult &Result) {
  json::Object Root;
  Root["var"] = collectGlobalVariables(Context.getASTContext());
  Root["age"] = 30;
  Root["mode"] = Result.Summary.Kind == ProgramKind::InterruptDriven ? "isr"
                  : Result.Summary.Kind == ProgramKind::MultiThreaded  ? "mt"
                                                                      : "";
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
        "phase4: slice 当前仅在非顺序程序的 lazy/显式 slice 链路启用，其余场景保持源码不变");
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
