#include "clang-nichecker/Driver/PipelineBuilder.h"
#include "clang-nichecker/Backend/CBMCDriverPass.h"
#include "clang-nichecker/Passes/ConditionExtractionPass.h"
#include "clang-nichecker/Passes/CounterexamplePass.h"
#include "clang-nichecker/Passes/FeederPass.h"
#include "clang-nichecker/Passes/FeederSeqProgramPass.h"
#include "clang-nichecker/Passes/InstrumenterPass.h"
#include "clang-nichecker/Passes/InterruptLoweringPass.h"
#include "clang-nichecker/Passes/LegacyModulePass.h"
#include "clang-nichecker/Passes/LabelInsertionPass.h"
#include "clang-nichecker/Passes/LazySequentializationPass.h"
#include "clang-nichecker/Passes/LoopUnrollPass.h"
#include "clang-nichecker/Passes/MapperPass.h"
#include "clang-nichecker/Passes/ProgramClassifierPass.h"
#include "clang-nichecker/Passes/ReplaceGotoPass.h"
#include "clang-nichecker/Passes/SequentializationPass.h"
#include "clang-nichecker/Passes/SlicePass.h"
#include "clang-nichecker/Passes/SliceSeqProgramPass.h"
#include "clang-nichecker/Passes/SourceEmissionPass.h"
#include "clang-nichecker/Passes/VariableRenamingPass.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <functional>

using namespace llvm;

namespace clang::nichecker {

namespace {

using PassFactory = std::function<std::unique_ptr<PipelinePass>()>;

PassFactory makeLegacyFactory(StringRef Name, StringRef Note) {
  return [Name = Name.str(), Note = Note.str()] {
    return std::make_unique<LegacyModulePass>(Name, Note);
  };
}

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
  Registry["slice"] = [] { return std::make_unique<SlicePass>(); };
  Registry["slice_seqprogram"] = [] {
    return std::make_unique<SliceSeqProgramPass>();
  };
  Registry["loop-unroll"] = [] { return std::make_unique<LoopUnrollPass>(); };
  Registry["sequentialization"] = [] {
    return std::make_unique<SequentializationPass>();
  };
  Registry["lazyseq"] = [] {
    return std::make_unique<LazySequentializationPass>();
  };
  Registry["source-emission"] = [] {
    return std::make_unique<SourceEmissionPass>();
  };
  Registry["feeder"] = [] { return std::make_unique<FeederPass>(); };
  Registry["feeder_seqprogram"] = [] {
    return std::make_unique<FeederSeqProgramPass>();
  };
  Registry["instrumenter"] = [] {
    return std::make_unique<InstrumenterPass>();
  };
  Registry["replacegoto"] = [] { return std::make_unique<ReplaceGotoPass>(); };
  Registry["mapper"] = [] { return std::make_unique<MapperPass>(); };
  Registry["cex"] = [] { return std::make_unique<CounterexamplePass>(); };
  Registry["cbmc-driver"] = [] { return std::make_unique<CBMCDriverPass>(); };

  Registry["conditionextractor"] = Registry["condition-extraction"];
  Registry["varnames"] = Registry["variable-renaming"];
  Registry["unroller"] = Registry["loop-unroll"];
  Registry["insertLabel"] = Registry["label-insertion"];

  Registry["workarounds"] =
      makeLegacyFactory("workarounds", "legacy placeholder: workarounds");
  Registry["functiontracker"] =
      makeLegacyFactory("functiontracker", "legacy placeholder: functiontracker");
  Registry["preinstrumenter"] =
      makeLegacyFactory("preinstrumenter", "legacy placeholder: preinstrumenter");
  Registry["spinlock"] =
      makeLegacyFactory("spinlock", "legacy placeholder: spinlock");
  Registry["preinliner"] =
      makeLegacyFactory("preinliner", "legacy placeholder: preinliner");
  Registry["inliner"] =
      makeLegacyFactory("inliner", "legacy placeholder: inliner");
  Registry["switchtransformer"] = makeLegacyFactory(
      "switchtransformer", "legacy placeholder: switchtransformer");
  Registry["dowhileconverter"] = makeLegacyFactory(
      "dowhileconverter", "legacy placeholder: dowhileconverter");
  Registry["LoopAbstraction"] = makeLegacyFactory(
      "LoopAbstraction", "legacy placeholder: LoopAbstraction");
  Registry["selfop"] = makeLegacyFactory("selfop", "legacy placeholder: selfop");
  Registry["constants"] =
      makeLegacyFactory("constants", "legacy placeholder: constants");
  Registry["duplicator"] =
      makeLegacyFactory("duplicator", "legacy placeholder: duplicator");
  Registry["condwaitconverter"] = makeLegacyFactory(
      "condwaitconverter", "legacy placeholder: condwaitconverter");

  Registry["stddeclinjector"] = makeLegacyFactory(
      "stddeclinjector", "legacy placeholder: stddeclinjector");
  Registry["SqrtInputAssert"] = makeLegacyFactory(
      "SqrtInputAssert", "legacy placeholder: SqrtInputAssert");
  Registry["BufferBoundsAssert"] = makeLegacyFactory(
      "BufferBoundsAssert", "legacy placeholder: BufferBoundsAssert");
  Registry["OverlapMemoryCopyAssert"] = makeLegacyFactory(
      "OverlapMemoryCopyAssert", "legacy placeholder: OverlapMemoryCopyAssert");
  Registry["FloatEqWarning"] = makeLegacyFactory(
      "FloatEqWarning", "legacy placeholder: FloatEqWarning");
  Registry["UninitializedVarWarning"] = makeLegacyFactory(
      "UninitializedVarWarning", "legacy placeholder: UninitializedVarWarning");
  Registry["FloatOpWarning"] = makeLegacyFactory(
      "FloatOpWarning", "legacy placeholder: FloatOpWarning");
  Registry["infinitloopcheck"] = makeLegacyFactory(
      "infinitloopcheck", "legacy placeholder: infinitloopcheck");
  Registry["indentfixer"] =
      makeLegacyFactory("indentfixer", "legacy placeholder: indentfixer");
  Registry["functioncheck"] =
      makeLegacyFactory("functioncheck", "legacy placeholder: functioncheck");
  Registry["MainWrapper"] =
      makeLegacyFactory("MainWrapper", "legacy placeholder: MainWrapper");
  Registry["seq"] = Registry["sequentialization"];
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

std::vector<std::string> lazyProfileNames(const PipelineOptions &Options) {
  std::vector<std::string> Names = {
      "program-classifier", "workarounds",     "functiontracker",
      "preinstrumenter",    "spinlock",        "preinliner",
      "inliner",            "slice",           "switchtransformer",
      "dowhileconverter",   "LoopAbstraction", "conditionextractor",
      "varnames",           "unroller",        "selfop",
      "constants",          "duplicator",      "condwaitconverter",
      "insertLabel",        "lazyseq",         "instrumenter",
      "replacegoto",        "mapper",          "feeder",
      "cex",                "source-emission"};
  if (Options.EnableCBMC)
    Names.push_back("cbmc-driver");
  return Names;
}

std::vector<std::string> shenfeiProfileNames(const PipelineOptions &Options) {
  std::vector<std::string> Names = {
      "program-classifier",     "stddeclinjector",        "preinliner",
      "inliner",                "slice_seqprogram",       "SqrtInputAssert",
      "BufferBoundsAssert",     "OverlapMemoryCopyAssert","FloatEqWarning",
      "UninitializedVarWarning","FloatOpWarning",         "infinitloopcheck",
      "indentfixer",            "functioncheck",          "LoopAbstraction",
      "MainWrapper",            "seq",                    "feeder_seqprogram",
      "source-emission"};
  if (Options.EnableCBMC)
    Names.push_back("cbmc-driver");
  return Names;
}

std::vector<std::string> profilePipelineNames(const PipelineOptions &Options) {
  if (Options.PipelineProfile.empty() || Options.PipelineProfile == "default")
    return defaultPipelineNames(Options);
  if (Options.PipelineProfile == "lazy")
    return lazyProfileNames(Options);
  if (Options.PipelineProfile == "shenfei")
    return shenfeiProfileNames(Options);
  return {};
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
                                       ? profilePipelineNames(Options)
                                       : parsePipelineSpec(Options.PipelineSpec);
  if (Names.empty()) {
    return createStringError(inconvertibleErrorCode(),
                             "unknown clang-nichecker profile: %s",
                             Options.PipelineProfile.c_str());
  }

  PassPipeline Pipeline;
  for (const std::string &Name : Names) {
    auto It = Registry.find(Name);
    if (It == Registry.end()) {
      return createStringError(inconvertibleErrorCode(),
                               "unknown clang-nichecker pass: %s",
                               Name.c_str());
    }
    Pipeline.push_back(It->second());
  }

  return Pipeline;
}

} // namespace clang::nichecker
