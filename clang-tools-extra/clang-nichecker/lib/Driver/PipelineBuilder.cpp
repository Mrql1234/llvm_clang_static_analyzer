#include "clang-nichecker/Driver/PipelineBuilder.h"
#include "clang-nichecker/Backend/CBMCDriverPass.h"
#include "clang-nichecker/Passes/ConditionExtractionPass.h"
#include "clang-nichecker/Passes/InterruptLoweringPass.h"
#include "clang-nichecker/Passes/LabelInsertionPass.h"
#include "clang-nichecker/Passes/LoopUnrollPass.h"
#include "clang-nichecker/Passes/ProgramClassifierPass.h"
#include "clang-nichecker/Passes/SequentializationPass.h"
#include "clang-nichecker/Passes/SourceEmissionPass.h"
#include "clang-nichecker/Passes/VariableRenamingPass.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <functional>

using namespace llvm;

namespace clang::nichecker {

namespace {

using PassFactory = std::function<std::unique_ptr<PipelinePass>()>;

StringMap<PassFactory> buildRegistry() {
  StringMap<PassFactory> Registry;
  Registry["program-classifier"] = [] {
    return std::make_unique<ProgramClassifierPass>();
  };
  Registry["interrupt-lowering"] = [] {
    return std::make_unique<InterruptLoweringPass>();
  };
  Registry["condition-extraction"] = [] {
    return std::make_unique<ConditionExtractionPass>();
  };
  Registry["variable-renaming"] = [] {
    return std::make_unique<VariableRenamingPass>();
  };
  Registry["label-insertion"] = [] {
    return std::make_unique<LabelInsertionPass>();
  };
  Registry["loop-unroll"] = [] { return std::make_unique<LoopUnrollPass>(); };
  Registry["sequentialization"] = [] {
    return std::make_unique<SequentializationPass>();
  };
  Registry["source-emission"] = [] {
    return std::make_unique<SourceEmissionPass>();
  };
  Registry["cbmc-driver"] = [] { return std::make_unique<CBMCDriverPass>(); };
  return Registry;
}

std::vector<std::string> defaultPipelineNames(const PipelineOptions &Options) {
  std::vector<std::string> Names = {"program-classifier",
                                    "interrupt-lowering",
                                    "condition-extraction",
                                    "variable-renaming",
                                    "label-insertion",
                                    "loop-unroll",
                                    "sequentialization",
                                    "source-emission"};
  if (Options.EnableCBMC)
    Names.push_back("cbmc-driver");
  return Names;
}

std::vector<std::string> parsePipelineSpec(StringRef Spec) {
  std::vector<std::string> Names;
  SmallVector<StringRef, 8> Parts;
  Spec.split(Parts, ',', -1, false);
  for (StringRef Part : Parts) {
    StringRef Trimmed = Part.trim();
    if (!Trimmed.empty())
      Names.push_back(Trimmed.str());
  }
  return Names;
}

} // namespace

Expected<PassPipeline> buildPipeline(const PipelineOptions &Options) {
  StringMap<PassFactory> Registry = buildRegistry();
  std::vector<std::string> Names = Options.PipelineSpec.empty()
                                       ? defaultPipelineNames(Options)
                                       : parsePipelineSpec(Options.PipelineSpec);

  PassPipeline Pipeline;
  for (const std::string &Name : Names) {
    auto It = Registry.find(Name);
    if (It == Registry.end()) {
      return createStringError(inconvertibleErrorCode(),
                               "unknown clang-nichecker pass: %s", Name.c_str());
    }
    Pipeline.push_back(It->second());
  }

  return Pipeline;
}

} // namespace clang::nichecker
