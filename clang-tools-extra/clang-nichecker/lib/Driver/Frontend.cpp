#include "clang-nichecker/Analysis/ProgramAnalyzer.h"
#include "clang-nichecker/Driver/Frontend.h"
#include "clang-nichecker/Driver/PipelineBuilder.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "clang-nichecker/Support/Types.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

namespace clang::nichecker {

namespace {

llvm::Expected<std::unique_ptr<ASTUnit>>
reparseTranslationUnit(CompilerInstance &BaseCI,
                       const std::shared_ptr<CompilerInvocation> &BaseInvocation,
                       llvm::StringRef CurrentSource) {
  if (BaseInvocation->getFrontendOpts().Inputs.empty()) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "missing frontend input for reparse");
  }

  auto Invocation = std::make_shared<CompilerInvocation>(*BaseInvocation);
  std::string MainFile =
      std::string(Invocation->getFrontendOpts().Inputs[0].getFile());
  if (MainFile.empty()) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "missing main file path for reparse");
  }

  Invocation->getPreprocessorOpts().clearRemappedFiles();
  Invocation->getPreprocessorOpts().RetainRemappedFileBuffers = true;
  Invocation->getPreprocessorOpts().addRemappedFile(
      MainFile, llvm::MemoryBuffer::getMemBufferCopy(CurrentSource, MainFile)
                    .release());

  auto DiagOpts =
      std::make_shared<DiagnosticOptions>(BaseCI.getDiagnosticOpts());
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS =
      BaseCI.getVirtualFileSystemPtr();
  if (!BaseFS)
    BaseFS = llvm::vfs::getRealFileSystem();

  llvm::IntrusiveRefCntPtr<DiagnosticsEngine> BootstrapDiags =
      CompilerInstance::createDiagnostics(*BaseFS, *DiagOpts);
  if (!BootstrapDiags) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to create bootstrap diagnostics");
  }

  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS =
      createVFSFromCompilerInvocation(*Invocation, *BootstrapDiags, BaseFS);
  llvm::IntrusiveRefCntPtr<DiagnosticsEngine> Diags =
      CompilerInstance::createDiagnostics(*VFS, *DiagOpts);
  if (!Diags) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to create reparse diagnostics");
  }

  auto FileMgr = llvm::makeIntrusiveRefCnt<FileManager>(
      Invocation->getFileSystemOpts(), VFS);
  std::unique_ptr<ASTUnit> Reparsed = ASTUnit::LoadFromCompilerInvocation(
      Invocation, BaseCI.getPCHContainerOperations(), DiagOpts, Diags, FileMgr);
  if (!Reparsed) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to build AST from transformed source");
  }
  return Reparsed;
}

} // namespace

class ClangNICheckerConsumer : public ASTConsumer {
public:
  ClangNICheckerConsumer(CompilerInstance &CI, PipelineOptions Options)
      : CI(CI), Options(std::move(Options)) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    (void)Context;
    std::string CurrentSource =
        std::string(CI.getSourceManager().getBufferData(
            CI.getSourceManager().getMainFileID()));
    std::shared_ptr<CompilerInvocation> BaseInvocation =
        std::make_shared<CompilerInvocation>(CI.getInvocation());
    std::unique_ptr<ASTUnit> ReparsedTU;
    TransformResult Result;

    auto ExpectedPipeline = buildPipeline(Options);
    if (!ExpectedPipeline) {
      llvm::errs() << "[clang-nichecker] pipeline build failed: "
                   << llvm::toString(ExpectedPipeline.takeError()) << "\n";
      return;
    }

    PassPipeline Pipeline = std::move(ExpectedPipeline.get());
    for (size_t Index = 0; Index < Pipeline.size(); ++Index) {
      const auto &Pass = Pipeline[Index];
      TranslationUnitHandle ActiveTU =
          ReparsedTU ? TranslationUnitHandle(*ReparsedTU)
                     : TranslationUnitHandle(CI);
      PipelineContext PipelineContext{ActiveTU, Options, CurrentSource};

      if (llvm::Error Err = Pass->run(PipelineContext, Result)) {
        llvm::errs() << "[clang-nichecker] pass failed (" << Pass->name()
                     << "): " << llvm::toString(std::move(Err)) << "\n";
        return;
      }

      const bool IsLastPass = Index + 1 == Pipeline.size();
      if (IsLastPass)
        continue;

      std::string MaterializedSource = materializeSource(PipelineContext, Result);
      const bool SourceChanged = MaterializedSource != CurrentSource;
      CurrentSource = std::move(MaterializedSource);
      Result.Source.clear();
      Result.PendingReplacements.clear();

      if (!SourceChanged)
        continue;

      llvm::Expected<std::unique_ptr<ASTUnit>> ReparsedOrErr =
          reparseTranslationUnit(CI, BaseInvocation, CurrentSource);
      if (!ReparsedOrErr) {
        llvm::errs() << "[clang-nichecker] reparse failed after pass ("
                     << Pass->name()
                     << "): " << llvm::toString(ReparsedOrErr.takeError())
                     << "\n";
        return;
      }
      ReparsedTU = std::move(*ReparsedOrErr);
      Result.Summary =
          refreshSummaryForCurrentAST(ReparsedTU->getASTContext(), Result.Summary);
    }

    TranslationUnitHandle FinalTU =
        ReparsedTU ? TranslationUnitHandle(*ReparsedTU)
                   : TranslationUnitHandle(CI);
    PipelineContext FinalContext{FinalTU, Options, CurrentSource};
    Result.Source = materializeSource(FinalContext, Result);
    Result.PendingReplacements.clear();

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
                   << (!Options.PipelineSpec.empty()
                           ? Options.PipelineSpec
                           : (Options.PipelineProfile.empty() ? "<default>"
                                                              : Options.PipelineProfile))
                   << "\n";
      llvm::errs() << "[clang-nichecker] notes: " << joinNotes(Result.Notes)
                   << "\n";
    }

    if (llvm::Error Err = writeFile(Options.OutputPath, Result.Source)) {
      llvm::errs() << "[clang-nichecker] 鍐欏叆杈撳嚭鏂囦欢澶辫触: "
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
