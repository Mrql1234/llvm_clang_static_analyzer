#include "clang-nichecker/Passes/LabelInsertionPass.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang-nichecker/Support/SourceUtils.h"
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

bool shouldRunLabelJar(const PipelineOptions &Options,
                       const TransformResult &Result) {
  if (Result.Summary.Kind == ProgramKind::Sequential)
    return false;
  return Options.EnableLegacyLabelJar &&
         (Options.PipelineProfile == "lazy" ||
          StringRef(Options.PipelineSpec).contains("insertLabel") ||
          StringRef(Options.PipelineSpec).contains("label-insertion"));
}

std::vector<std::string> collectFunctionNames(ASTContext &AST) {
  std::vector<std::string> Names;
  for (Decl *D : AST.getTranslationUnitDecl()->decls()) {
    const auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD || !FD->hasBody())
      continue;
    if (!FD->getName().empty())
      Names.push_back(FD->getNameAsString());
  }
  return Names;
}

std::string buildLabelDataJson(const PipelineContext &Context,
                               const TransformResult &Result) {
  json::Object Root;
  Root["var"] = Context.Options.SliceVariable;
  Root["age"] = 30;
  Root["mode"] = Context.Options.SliceMode;
  Root["main_task_0"] = 0;

  int Priority = 1;
  for (const std::string &Name : collectFunctionNames(Context.getASTContext())) {
    if (startsWithISR(Name))
      Root[Name] = Priority++;
  }
  return formatv("{0:2}", json::Value(std::move(Root))).str();
}

} // namespace

llvm::StringRef LabelInsertionPass::name() const { return "label-insertion"; }

llvm::Error LabelInsertionPass::run(const PipelineContext &Context,
                                    TransformResult &Result) const {
  if (!shouldRunLabelJar(Context.Options, Result)) {
    Result.Notes.push_back(
        "phase4: label-insertion 按 Python 默认 labelReduc=false 跳过 legacy jar，保持源码不变");
    return Error::success();
  }

  LegacyJarInvocationConfig Config;
  Config.PassName = "label-insertion";
  Config.JarName = "testLabelReduc.jar";
  Config.InputPrefix = "istLab_";
  Config.OutputPrefix = "label_";
  Config.LegacyEntryFunction = "main_task_0";

  Expected<LegacyJarInvocationResult> Invocation =
      runLegacyJarTransform(Context, Result, Config,
                            buildLabelDataJson(Context, Result));
  if (!Invocation) {
    Result.Notes.push_back(
        formatv("phase4: label-insertion 未能调用 legacy jar，已跳过；原因: {0}",
                toString(Invocation.takeError()))
            .str());
    return Error::success();
  }

  Result.PendingReplacements.clear();
  Result.Source = Invocation->TransformedSource;
  Result.Notes.push_back(
      formatv("phase4: label-insertion 已通过 legacy jar 生成输出，目录: {0}",
              Invocation->WorkDir)
          .str());
  return Error::success();
}

} // namespace clang::nichecker
