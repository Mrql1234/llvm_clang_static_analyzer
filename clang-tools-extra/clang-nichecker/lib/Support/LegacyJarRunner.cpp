#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"

#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

using namespace clang;
using namespace llvm;

namespace clang::nichecker {

namespace {

class WorkingDirectoryGuard {
public:
  WorkingDirectoryGuard() : Valid(!sys::fs::current_path(Original)) {}

  Error changeTo(StringRef Path) {
    if (std::error_code EC = sys::fs::set_current_path(Path))
      return errorCodeToError(EC);
    return Error::success();
  }

  ~WorkingDirectoryGuard() {
    if (Valid)
      sys::fs::set_current_path(Original);
  }

private:
  SmallString<256> Original;
  bool Valid = false;
};

std::vector<std::string> buildSearchRoots() {
  SmallString<256> CurrentDir;
  if (sys::fs::current_path(CurrentDir))
    return {};

  std::vector<std::string> Roots;
  SmallString<256> Cursor(CurrentDir);
  while (true) {
    Roots.push_back(std::string(Cursor));
    SmallString<256> Parent(Cursor);
    sys::path::parent_path(Parent);
    if (Parent == Cursor || Parent.empty())
      break;
    Cursor = Parent;
  }
  return Roots;
}

std::optional<std::string> locateLegacyCSeqDir() {
  for (const std::string &Root : buildSearchRoots()) {
    SmallString<256> Candidate(Root);
    sys::path::append(Candidate, "nichecker", "Cseq");
    if (sys::fs::is_directory(Candidate))
      return std::string(Candidate);
  }
  return std::nullopt;
}

std::optional<std::string> locateJavaExecutable() {
  if (ErrorOr<std::string> Java = sys::findProgramByName("java"))
    return *Java;
  return std::nullopt;
}

std::string readFileIfPresent(StringRef Path) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer = MemoryBuffer::getFile(Path);
  if (!Buffer)
    return "";
  return std::string(Buffer.get()->getBuffer());
}

std::string makePrefixedPath(StringRef Directory, StringRef Prefix,
                             StringRef BaseName) {
  SmallString<256> Path(Directory);
  sys::path::append(Path, (Prefix + BaseName).str());
  return std::string(Path);
}

bool isLineComment(StringRef Source, size_t Pos) {
  return Pos + 1 < Source.size() && Source[Pos] == '/' && Source[Pos + 1] == '/';
}

bool isBlockComment(StringRef Source, size_t Pos) {
  return Pos + 1 < Source.size() && Source[Pos] == '/' && Source[Pos + 1] == '*';
}

size_t skipHorizontalWhitespace(StringRef Source, size_t Pos) {
  while (Pos < Source.size() &&
         (Source[Pos] == ' ' || Source[Pos] == '\t' || Source[Pos] == '\r' ||
          Source[Pos] == '\n')) {
    ++Pos;
  }
  return Pos;
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

std::string materializeLegacyInputSource(const PipelineContext &Context,
                                         const TransformResult &Result,
                                         const LegacyJarInvocationConfig &Config) {
  std::string Source = materializeSource(Context, Result);
  if (Config.LegacyEntryFunction.empty())
    return Source;

  if (findFunctionLikeIdentifier(Source, Config.LegacyEntryFunction) !=
      StringRef::npos) {
    return Source;
  }

  std::vector<std::string> Candidates;
  if (Result.Summary.MainFunction &&
      !Result.Summary.MainFunction->getName().empty()) {
    Candidates.push_back(Result.Summary.MainFunction->getNameAsString());
  }
  Candidates.push_back("main");
  Candidates.push_back("main_task");
  Candidates.push_back("main_task_0");

  for (const std::string &Candidate : Candidates) {
    if (Candidate == Config.LegacyEntryFunction)
      continue;
    size_t Pos = findFunctionLikeIdentifier(Source, Candidate);
    if (Pos == StringRef::npos)
      continue;
    Source.replace(Pos, Candidate.size(), Config.LegacyEntryFunction);
    return Source;
  }
  return Source;
}

std::string buildSourceWithoutFunctionDefinitions(StringRef Source) {
  std::string Output;
  size_t ItemStart = 0;
  size_t KeepStart = 0;
  unsigned BraceDepth = 0;
  unsigned ParenDepth = 0;
  bool SawTopLevelClosingParen = false;
  bool InFunctionDefinition = false;
  unsigned FunctionBraceDepth = 0;

  for (size_t I = 0; I < Source.size(); ++I) {
    if (isLineComment(Source, I)) {
      I += 2;
      while (I < Source.size() && Source[I] != '\n')
        ++I;
      continue;
    }
    if (isBlockComment(Source, I)) {
      I += 2;
      while (I + 1 < Source.size() &&
             !(Source[I] == '*' && Source[I + 1] == '/')) {
        ++I;
      }
      if (I + 1 < Source.size())
        ++I;
      continue;
    }

    char C = Source[I];
    if (C == '"' || C == '\'') {
      char Quote = C;
      ++I;
      while (I < Source.size()) {
        if (Source[I] == '\\') {
          ++I;
        } else if (Source[I] == Quote) {
          break;
        }
        ++I;
      }
      continue;
    }

    if (BraceDepth == 0 && ParenDepth == 0 &&
        (C == ';' || (C == '}' && I + 1 < Source.size()))) {
      ItemStart = skipHorizontalWhitespace(Source, I + 1);
      SawTopLevelClosingParen = false;
      continue;
    }

    if (C == '(') {
      ++ParenDepth;
      continue;
    }
    if (C == ')') {
      if (ParenDepth > 0)
        --ParenDepth;
      if (BraceDepth == 0 && ParenDepth == 0)
        SawTopLevelClosingParen = true;
      continue;
    }

    if (C == '{') {
      if (!InFunctionDefinition && BraceDepth == 0 && ParenDepth == 0 &&
          SawTopLevelClosingParen) {
        Output.append(Source.substr(KeepStart, ItemStart - KeepStart));
        InFunctionDefinition = true;
        FunctionBraceDepth = 1;
        ++BraceDepth;
        continue;
      }

      if (InFunctionDefinition) {
        ++FunctionBraceDepth;
        ++BraceDepth;
        continue;
      }

      ++BraceDepth;
      continue;
    }
    if (C == '}') {
      if (InFunctionDefinition) {
        if (FunctionBraceDepth > 0)
          --FunctionBraceDepth;
        if (BraceDepth > 0)
          --BraceDepth;
        if (FunctionBraceDepth == 0) {
          InFunctionDefinition = false;
          KeepStart = skipHorizontalWhitespace(Source, I + 1);
          ItemStart = KeepStart;
          SawTopLevelClosingParen = false;
        }
        continue;
      }

      if (BraceDepth > 0)
        --BraceDepth;
      continue;
    }
  }

  if (KeepStart < Source.size())
    Output.append(Source.substr(KeepStart));
  return Output;
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
  sys::path::append(Prefix, "clang-nichecker-legacy");

  SmallString<256> WorkDir;
  if (std::error_code EC = sys::fs::createUniqueDirectory(Prefix, WorkDir))
    return errorCodeToError(EC);
  return WorkDir;
}

} // namespace

std::string materializeSource(const PipelineContext &Context,
                              const TransformResult &Result) {
  if (!Result.Source.empty())
    return Result.Source;

  std::string Source = Context.CurrentSource.str();
  applyReplacements(Source, Result.PendingReplacements);
  return Source;
}

Expected<LegacyJarInvocationResult>
runLegacyJarTransform(const PipelineContext &Context,
                      const TransformResult &Result,
                      const LegacyJarInvocationConfig &Config,
                      StringRef DataJson) {
  std::optional<std::string> CSeqDir = locateLegacyCSeqDir();
  if (!CSeqDir) {
    return createStringError(inconvertibleErrorCode(),
                             "未找到 nichecker/Cseq 目录");
  }

  std::optional<std::string> Java = locateJavaExecutable();
  if (!Java) {
    return createStringError(inconvertibleErrorCode(),
                             "当前系统 PATH 中未找到可执行的 java");
  }

  SmallString<256> JarPath(*CSeqDir);
  sys::path::append(JarPath, Config.JarName);
  if (!sys::fs::exists(JarPath)) {
    return createStringError(inconvertibleErrorCode(),
                             "未找到 legacy jar: %s", JarPath.c_str());
  }

  Expected<SmallString<256>> WorkDirOrErr =
      createWorkDir(Context.Options.OutputPath);
  if (!WorkDirOrErr)
    return WorkDirOrErr.takeError();

  SmallString<256> WorkDir = *WorkDirOrErr;
  StringRef BaseName = sys::path::filename(Context.Options.InputPath);
  std::string InputFile =
      makePrefixedPath(WorkDir, Config.InputPrefix, BaseName);
  StringRef OutputBaseName =
      Config.OutputBaseName.empty() ? sys::path::filename(InputFile)
                                    : StringRef(Config.OutputBaseName);
  std::string OutputFile =
      makePrefixedPath(WorkDir, Config.OutputPrefix, OutputBaseName);
  std::string StdoutLog =
      makePrefixedPath(WorkDir, Config.PassName + ".", "stdout.log");
  std::string StderrLog =
      makePrefixedPath(WorkDir, Config.PassName + ".", "stderr.log");
  std::string DataJsonPath = makePrefixedPath(WorkDir, "", "data.json");

  std::string Source = materializeLegacyInputSource(Context, Result, Config);
  if (Error Err = writeFile(InputFile, Source))
    return std::move(Err);
  if (Error Err = writeFile(DataJsonPath, DataJson))
    return std::move(Err);

  std::vector<std::string> StorageArgs = {Java.value(), "-jar",
                                          std::string(JarPath), InputFile};
  SmallVector<StringRef, 8> Args;
  for (const std::string &Arg : StorageArgs)
    Args.push_back(Arg);

  std::array<std::optional<StringRef>, 3> Redirects = {
      std::nullopt,
      StringRef(StdoutLog),
      StringRef(StderrLog),
  };

  WorkingDirectoryGuard CWDGuard;
  if (Error Err = CWDGuard.changeTo(WorkDir))
    return std::move(Err);

  std::string ErrMsg;
  bool ExecutionFailed = false;
  int ExitCode = sys::ExecuteAndWait(Java.value(), Args, std::nullopt,
                                     Redirects, 0, 0, &ErrMsg,
                                     &ExecutionFailed);
  if (ExecutionFailed || ExitCode != 0) {
    std::string StdoutText = readFileIfPresent(StdoutLog);
    std::string StderrText = readFileIfPresent(StderrLog);
    return createStringError(
        inconvertibleErrorCode(),
        "legacy jar 执行失败(pass=%s, exit=%d): %s\nstdout:\n%s\nstderr:\n%s",
        Config.PassName.c_str(), ExitCode,
        ErrMsg.empty() ? "unknown error" : ErrMsg.c_str(), StdoutText.c_str(),
        StderrText.c_str());
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> OutputBuffer =
      MemoryBuffer::getFile(OutputFile);
  if (!OutputBuffer) {
    return createStringError(inconvertibleErrorCode(),
                             "legacy jar 未生成输出文件: %s",
                             OutputFile.c_str());
  }

  LegacyJarInvocationResult InvocationResult;
  InvocationResult.WorkDir = std::string(WorkDir);
  InvocationResult.InputFile = InputFile;
  InvocationResult.OutputFile = OutputFile;
  InvocationResult.StdoutLog = StdoutLog;
  InvocationResult.StderrLog = StderrLog;

  if (Config.OutputContainsOnlyFunctions) {
    std::string Prelude = buildSourceWithoutFunctionDefinitions(Source);
    InvocationResult.TransformedSource =
        Prelude + std::string(OutputBuffer.get()->getBuffer());
  } else {
    InvocationResult.TransformedSource =
        std::string(OutputBuffer.get()->getBuffer());
  }
  return InvocationResult;
}

} // namespace clang::nichecker
