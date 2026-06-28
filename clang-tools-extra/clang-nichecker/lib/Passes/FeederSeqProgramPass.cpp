#include "clang-nichecker/Passes/FeederSeqProgramPass.h"
#include "clang-nichecker/Backend/CBMCRunner.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"

#include <cctype>
#include <string>

using namespace llvm;

namespace clang::nichecker {

namespace {

bool shouldRunFeederSeqProgram(const PipelineOptions &Options,
                               const TransformResult &Result) {
  if (Result.Summary.Kind != ProgramKind::Sequential)
    return false;
  if (Options.PipelineProfile == "shenfei")
    return true;
  return StringRef(Options.PipelineSpec).contains("feeder_seqprogram");
}

bool isIdentifierChar(char C) {
  unsigned char Byte = static_cast<unsigned char>(C);
  return std::isalnum(Byte) || C == '_';
}

size_t findFunctionLikeIdentifier(StringRef Source, StringRef Name) {
  if (Name.empty())
    return StringRef::npos;

  size_t Pos = 0;
  while (true) {
    Pos = Source.find(Name, Pos);
    if (Pos == StringRef::npos)
      return Pos;

    size_t End = Pos + Name.size();
    bool HasIdentifierBoundaryBefore =
        Pos == 0 || !isIdentifierChar(Source[Pos - 1]);
    bool HasIdentifierBoundaryAfter =
        End >= Source.size() || !isIdentifierChar(Source[End]);
    if (!HasIdentifierBoundaryBefore || !HasIdentifierBoundaryAfter) {
      ++Pos;
      continue;
    }

    size_t Cursor = End;
    while (Cursor < Source.size() &&
           std::isspace(static_cast<unsigned char>(Source[Cursor]))) {
      ++Cursor;
    }
    if (Cursor < Source.size() && Source[Cursor] == '(')
      return Pos;
    ++Pos;
  }
}

std::string detectEntryFunction(StringRef Source) {
  if (findFunctionLikeIdentifier(Source, "main") != StringRef::npos)
    return "main";
  if (findFunctionLikeIdentifier(Source, "main_thread") != StringRef::npos)
    return "main_thread";
  return "";
}

Expected<SmallString<256>> createWorkDir(StringRef OutputPath) {
  SmallString<256> OutputDir(OutputPath);
  sys::path::remove_filename(OutputDir);
  if (OutputDir.empty()) {
    if (std::error_code EC = sys::fs::current_path(OutputDir))
      return errorCodeToError(EC);
  }

  if (!sys::fs::exists(OutputDir)) {
    if (std::error_code EC = sys::fs::create_directories(OutputDir))
      return errorCodeToError(EC);
  }

  SmallString<256> Prefix(OutputDir);
  sys::path::append(Prefix, "clang-nichecker-feeder-seqprogram");

  SmallString<256> WorkDir;
  if (std::error_code EC = sys::fs::createUniqueDirectory(Prefix, WorkDir))
    return errorCodeToError(EC);
  return WorkDir;
}

} // namespace

llvm::StringRef FeederSeqProgramPass::name() const {
  return "feeder_seqprogram";
}

llvm::Error FeederSeqProgramPass::run(const PipelineContext &Context,
                                      TransformResult &Result) const {
  if (!shouldRunFeederSeqProgram(Context.Options, Result)) {
    Result.Notes.push_back(
        "phase6: feeder_seqprogram 当前仅在顺序程序的 shenfei/显式 feeder_seqprogram 链路启用，其余场景跳过后端验证");
    return Error::success();
  }

  std::string Source = materializeSource(Context, Result);
  Expected<SmallString<256>> WorkDirOrErr =
      createWorkDir(Context.Options.OutputPath);
  if (!WorkDirOrErr)
    return WorkDirOrErr.takeError();

  SmallString<256> SourcePath(*WorkDirOrErr);
  sys::path::append(SourcePath,
                    ("feeder_seqprogram_" +
                     std::string(sys::path::filename(Context.Options.InputPath))))
      ;
  if (!StringRef(SourcePath).ends_with(".c"))
    SourcePath += ".c";

  if (Error Err = writeFile(SourcePath, Source))
    return std::move(Err);

  SmallString<256> LogBase(*WorkDirOrErr);
  sys::path::append(LogBase, "feeder-seqprogram.cbmc");

  CBMCRunConfig Config;
  Config.SourceFilePath = std::string(SourcePath);
  Config.EntryFunction = detectEntryFunction(Source);
  Config.Unwind = 10;
  Config.BoundsCheck = true;
  Config.DivByZeroCheck = true;
  Config.PointerCheck = true;
  Config.ConversionCheck = true;
  Config.Trace = true;
  Config.NoLibrary = true;
  Config.ObjectBits = 10;

  Expected<CBMCRunResult> Run = runCBMC(Config, LogBase);
  if (!Run) {
    Result.Notes.push_back(
        formatv("phase6: feeder_seqprogram 未能调用 CBMC，已跳过；原因: {0}",
                toString(Run.takeError()))
            .str());
    return Error::success();
  }

  if (Run->ExecutionFailed || Run->ExitCode < 0) {
    Result.Notes.push_back(
        formatv("phase6: feeder_seqprogram 调用 CBMC 失败，exit={0}，错误信息: {1}，日志: {2}",
                Run->ExitCode,
                Run->ErrorMessage.empty() ? "未知错误" : Run->ErrorMessage,
                Run->CombinedLogPath)
            .str());
    return Error::success();
  }

  Result.Notes.push_back(
      formatv("phase6: feeder_seqprogram 已按旧参数风格调用 CBMC，结果 {0}，exit={1}，日志: {2}",
              verificationOutcomeToString(Run->Outcome), Run->ExitCode,
              Run->CombinedLogPath)
          .str());
  return Error::success();
}

} // namespace clang::nichecker
