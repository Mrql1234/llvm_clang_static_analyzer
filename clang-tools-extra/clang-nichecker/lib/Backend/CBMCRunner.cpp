#include "clang-nichecker/Backend/CBMCRunner.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"

#include <array>
#include <string>
#include <vector>

using namespace llvm;

namespace clang::nichecker {

namespace {

std::vector<std::string> buildCBMCSearchRoots() {
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

std::optional<std::string> locateBundledCBMC(StringRef Backend) {
  if (Backend == "cbmc-ext") {
    for (const std::string &Root : buildCBMCSearchRoots()) {
      SmallString<256> Candidate(Root);
      sys::path::append(Candidate, "nichecker", "Cseq", "backends",
                        "cbmc-ext");
      if (sys::fs::exists(Candidate) && !sys::fs::can_execute(Candidate)) {
        if (ErrorOr<sys::fs::perms> Perms = sys::fs::getPermissions(Candidate))
          sys::fs::setPermissions(Candidate, *Perms | sys::fs::owner_exe |
                                                 sys::fs::group_exe |
                                                 sys::fs::all_exe);
      }
      if (sys::fs::can_execute(Candidate))
        return std::string(Candidate);
    }
    return std::nullopt;
  }

  static constexpr const char *RelativeCandidates[] = {
      "nichecker/Cseq/backends/cbmc-5.11",
      "nichecker/Cseq/backends/cbmc",
      "nichecker/Cseq/backends/cbmc_5-10.exe",
      "nichecker/Cseq/backends/cbmc_win.exe",
  };

  for (const std::string &Root : buildCBMCSearchRoots()) {
    for (const char *Relative : RelativeCandidates) {
      SmallString<256> Candidate(Root);
      sys::path::append(Candidate, Relative);
      if (sys::fs::exists(Candidate) && !sys::fs::can_execute(Candidate)) {
        if (ErrorOr<sys::fs::perms> Perms = sys::fs::getPermissions(Candidate)) {
          sys::fs::setPermissions(
              Candidate, *Perms | sys::fs::owner_exe | sys::fs::group_exe |
                             sys::fs::all_exe);
        }
      }
      if (sys::fs::can_execute(Candidate))
        return std::string(Candidate);
    }
  }
  return std::nullopt;
}

std::string readFileIfPresent(StringRef Path) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer = MemoryBuffer::getFile(Path);
  if (!Buffer)
    return "";
  return std::string(Buffer.get()->getBuffer());
}

std::string buildCombinedLog(StringRef StdoutText, StringRef StderrText) {
  std::string Log;
  raw_string_ostream OS(Log);
  if (!StdoutText.empty())
    OS << StdoutText;
  if (!StderrText.empty()) {
    if (!StdoutText.empty() && !StdoutText.ends_with("\n"))
      OS << "\n";
    OS << "\n[stderr]\n" << StderrText;
  }
  return OS.str();
}

std::vector<std::string> buildCBMCArgs(StringRef Executable,
                                       const CBMCRunConfig &Config) {
  std::vector<std::string> Args;
  const bool IsCBMCExt = Config.Backend == "cbmc-ext";
  Args.push_back(Executable.str());
  Args.push_back("--unwind");
  Args.push_back(std::to_string(Config.Unwind ? Config.Unwind : 1));
  // The bundled cbmc-ext 5.4 only accepts the core BMC options used by the
  // Python mapper; newer CBMC checks are added only for normal cbmc runs.
  if (!IsCBMCExt && Config.BoundsCheck)
    Args.push_back("--bounds-check");
  if (!IsCBMCExt && Config.DivByZeroCheck)
    Args.push_back("--div-by-zero-check");
  if (!IsCBMCExt && Config.PointerCheck)
    Args.push_back("--pointer-check");
  if (!IsCBMCExt && Config.ConversionCheck)
    Args.push_back("--conversion-check");
  if (!IsCBMCExt && Config.Trace)
    Args.push_back("--trace");
  if (!IsCBMCExt && Config.NoLibrary)
    Args.push_back("--no-library");
  if (!Config.EntryFunction.empty() && Config.EntryFunction != "main") {
    Args.push_back("--function");
    Args.push_back(Config.EntryFunction);
  }
  if (!IsCBMCExt && Config.ObjectBits) {
    Args.push_back("--object-bits");
    Args.push_back(std::to_string(Config.ObjectBits));
  }
  if (Config.Depth) {
    Args.push_back("--depth");
    Args.push_back(std::to_string(Config.Depth));
  }
  if (!Config.DimacsOutputPath.empty()) {
    Args.push_back("--dimacs");
    Args.push_back("--outfile");
    Args.push_back(Config.DimacsOutputPath);
  }
  Args.insert(Args.end(), Config.ExtraArgs.begin(), Config.ExtraArgs.end());
  Args.push_back(Config.SourceFilePath);
  return Args;
}

VerificationOutcome determineOutcome(StringRef Output) {
  if (Output.contains("VERIFICATION FAILED"))
    return VerificationOutcome::Unsafe;
  if (Output.contains("VERIFICATION SUCCESSFUL"))
    return VerificationOutcome::Safe;
  return VerificationOutcome::Unknown;
}

} // namespace

std::optional<std::string> locateCBMCExecutable() {
  return locateCBMCExecutable("cbmc");
}

std::optional<std::string> locateCBMCExecutable(StringRef Backend) {
  if (Backend != "cbmc" && Backend != "cbmc-ext")
    return std::nullopt;
  if (Backend == "cbmc-ext")
    return locateBundledCBMC(Backend);
  if (ErrorOr<std::string> CBMCInPath = sys::findProgramByName("cbmc"))
    return *CBMCInPath;
  return locateBundledCBMC(Backend);
}

Expected<CBMCRunResult> runCBMC(const CBMCRunConfig &Config,
                                StringRef LogBasePath) {
  std::optional<std::string> Executable = locateCBMCExecutable(Config.Backend);
  if (!Executable) {
    return createStringError(
        inconvertibleErrorCode(),
        "未找到可执行的后端 %s；可使用 PATH 中的 cbmc 或仓库内 nichecker/Cseq/backends/cbmc-5.11/cbmc-ext",
        Config.Backend.c_str());
  }

  CBMCRunResult Result;
  Result.Executable = *Executable;
  Result.SourceFilePath = Config.SourceFilePath;
  Result.StdoutLogPath = (LogBasePath + ".stdout.log").str();
  Result.StderrLogPath = (LogBasePath + ".stderr.log").str();
  Result.CombinedLogPath = (LogBasePath + ".log").str();

  std::vector<std::string> StorageArgs = buildCBMCArgs(*Executable, Config);
  SmallVector<StringRef, 16> Args;
  for (const std::string &Arg : StorageArgs)
    Args.push_back(Arg);

  std::array<std::optional<StringRef>, 3> Redirects = {
      std::nullopt,
      StringRef(Result.StdoutLogPath),
      StringRef(Result.StderrLogPath),
  };

  Result.ExitCode = sys::ExecuteAndWait(*Executable, Args, std::nullopt,
                                        Redirects, 0, 0, &Result.ErrorMessage,
                                        &Result.ExecutionFailed);

  std::string StdoutText = readFileIfPresent(Result.StdoutLogPath);
  std::string StderrText = readFileIfPresent(Result.StderrLogPath);
  Result.CombinedOutput = buildCombinedLog(StdoutText, StderrText);
  if (Error Err = writeFile(Result.CombinedLogPath, Result.CombinedOutput))
    return std::move(Err);

  Result.Outcome = determineOutcome(Result.CombinedOutput);
  return Result;
}

StringRef verificationOutcomeToString(VerificationOutcome Outcome) {
  switch (Outcome) {
  case VerificationOutcome::Safe:
    return "SAFE";
  case VerificationOutcome::Unsafe:
    return "UNSAFE";
  case VerificationOutcome::Unknown:
    return "UNKNOWN";
  }
  llvm_unreachable("unknown verification outcome");
}

} // namespace clang::nichecker
