#include "clang-nichecker/Driver/Frontend.h"
#include "clang-nichecker/Driver/PipelineBuilder.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang-nichecker/Support/Types.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/FrontendActions.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

namespace clang::nichecker {

class ClangNICheckerConsumer : public ASTConsumer {
public:
  ClangNICheckerConsumer(CompilerInstance &CI, PipelineOptions Options)
      : CI(CI), Options(std::move(Options)) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    (void)Context;
    llvm::StringRef Source =
        CI.getSourceManager().getBufferData(CI.getSourceManager().getMainFileID());
    PipelineContext PipelineContext{CI, Options, Source};
    TransformResult Result;

    auto ExpectedPipeline = buildPipeline(Options);
    if (!ExpectedPipeline) {
      llvm::errs() << "[clang-nichecker] pipeline build failed: "
                   << llvm::toString(ExpectedPipeline.takeError()) << "\n";
      return;
    }

    PassPipeline Pipeline = std::move(ExpectedPipeline.get());
    for (const auto &Pass : Pipeline) {
      if (llvm::Error Err = Pass->run(PipelineContext, Result)) {
        llvm::errs() << "[clang-nichecker] pass failed (" << Pass->name()
                     << "): " << llvm::toString(std::move(Err)) << "\n";
        return;
      }
    }

    if (Options.PrintAnalysis) {
      llvm::errs() << "[clang-nichecker] input: " << Options.InputPath << "\n";
      llvm::errs() << "[clang-nichecker] kind: "
                   << toString(Result.Summary.Kind) << "\n";
      llvm::errs() << "[clang-nichecker] thread entries: "
                   << joinList(Result.Summary.ThreadEntryFunctions) << "\n";
      llvm::errs() << "[clang-nichecker] interrupt functions: "
                   << joinList(Result.Summary.InterruptFunctions) << "\n";
      llvm::errs() << "[clang-nichecker] uses enable_isr: "
                   << (Result.Summary.UsesEnableISR ? "true" : "false") << "\n";
      llvm::errs() << "[clang-nichecker] uses disable_isr: "
                   << (Result.Summary.UsesDisableISR ? "true" : "false") << "\n";
      llvm::errs() << "[clang-nichecker] has main definition: "
                   << (Result.Summary.MainFunction ? "true" : "false") << "\n";
      llvm::errs() << "[clang-nichecker] main file: "
                   << CI.getSourceManager().getFilename(
                          CI.getSourceManager().getLocForStartOfFile(
                              CI.getSourceManager().getMainFileID()))
                   << "\n";
      llvm::errs() << "[clang-nichecker] pipeline: "
                   << (Options.PipelineSpec.empty() ? "<default>"
                                                   : Options.PipelineSpec)
                   << "\n";
      llvm::errs() << "[clang-nichecker] notes: " << joinNotes(Result.Notes)
                   << "\n";
    }

    if (llvm::Error Err = writeFile(Options.OutputPath, Result.Source)) {
      llvm::errs() << "[clang-nichecker] 写入输出文件失败: "
                   << llvm::toString(std::move(Err)) << "\n";
      return;
    }

    llvm::errs() << "[clang-nichecker] generated output file: "
                 << Options.OutputPath << "\n";
  }

private:
  CompilerInstance &CI;
  PipelineOptions Options;
};

class ClangNICheckerAction : public ASTFrontendAction {
public:
  explicit ClangNICheckerAction(PipelineOptions Options)
      : Options(std::move(Options)) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef InFile) override {
    PipelineOptions Effective = Options;
    Effective.InputPath = std::string(InFile);
    if (Effective.OutputPath.empty())
      Effective.OutputPath = defaultOutputPath(InFile);
    return std::make_unique<ClangNICheckerConsumer>(CI, std::move(Effective));
  }

private:
  PipelineOptions Options;
};

class ClangNICheckerActionFactory : public tooling::FrontendActionFactory {
public:
  explicit ClangNICheckerActionFactory(PipelineOptions Options)
      : Options(std::move(Options)) {}

  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<ClangNICheckerAction>(Options);
  }

private:
  PipelineOptions Options;
};

std::unique_ptr<tooling::FrontendActionFactory>
createActionFactory(const PipelineOptions &Options) {
  return std::make_unique<ClangNICheckerActionFactory>(Options);
}

} // namespace clang::nichecker
