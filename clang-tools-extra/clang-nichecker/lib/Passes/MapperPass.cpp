#include "clang-nichecker/Passes/MapperPass.h"
#include "clang-nichecker/Backend/CBMCRunner.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "clang-nichecker/Support/SourceUtils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/FormatVariadic.h"

#include <cmath>
#include <string>

using namespace llvm;

namespace clang::nichecker {

namespace {

bool isPowerOfTwo(unsigned Value) {
  return Value && (Value & (Value - 1)) == 0;
}

std::vector<unsigned> parseContextSwitchBits(StringRef Dimacs,
                                              unsigned Contexts,
                                              unsigned Cores) {
  std::vector<unsigned> Bits;
  if (Contexts == 0 || Cores <= 1)
    return Bits;

  const unsigned SplitWidth = static_cast<unsigned>(std::log2(Cores));
  SmallVector<StringRef, 4096> Lines;
  Dimacs.split(Lines, '\n', -1, false);
  for (StringRef Line : Lines) {
    // mapper.py splits the DIMACS formula on the least significant bit of
    // __cs_tid for context 1, 2, ...; __cs_thread_index is a runtime value
    // and does not identify the independent context choices.
    const size_t Name = Line.find("__cs_tid!0@1#1");
    if (Name == StringRef::npos)
      continue;

    SmallVector<StringRef, 64> Tokens;
    Line.split(Tokens, ' ', -1, false);
    unsigned First = 0;
    unsigned Last = 0;
    bool Found = false;
    for (StringRef Token : Tokens) {
      unsigned Value = 0;
      if (Token.getAsInteger(10, Value))
        continue;
      if (!Found)
        First = Value;
      Last = Value;
      Found = true;
    }
    if (!Found || Last < First)
      continue;

    const unsigned TotalWidth = Last - First + 1;
    if (TotalWidth % Contexts != 0)
      continue;
    const unsigned TidWidth = TotalWidth / Contexts;
    if (TidWidth == 0 || TotalWidth < SplitWidth * TidWidth)
      continue;
    for (unsigned Context = 1; Context <= SplitWidth; ++Context)
      Bits.push_back(First + Context * TidWidth);
    return Bits;
  }
  return Bits;
}

std::vector<std::string> buildPartitions(ArrayRef<unsigned> Bits,
                                         unsigned Cores) {
  std::vector<std::string> Partitions;
  const unsigned Width = static_cast<unsigned>(Bits.size());
  for (unsigned Partition = 0; Partition < Cores; ++Partition) {
    std::string Assume;
    for (unsigned Bit = 0; Bit < Width; ++Bit) {
      if (!Assume.empty())
        Assume += ',';
      unsigned Value = (Partition >> (Width - Bit - 1)) & 1U;
      Assume += std::to_string(Bits[Bit]) + "=" + std::to_string(Value);
    }
    Partitions.push_back(std::move(Assume));
  }
  return Partitions;
}

} // namespace

llvm::StringRef MapperPass::name() const { return "mapper"; }

llvm::Error MapperPass::run(const PipelineContext &Context,
                            TransformResult &Result) const {
  Result.RequiresASTReparse = false;
  if (Context.Options.Contexts == 0) {
    Result.Notes.push_back(
        "phase6: mapper 按 Python 默认语义跳过，未请求 context-bounded DIMACS 映射");
    return Error::success();
  }
  if (Context.Options.Cores <= 1) {
    Result.Notes.push_back(
        "phase6: mapper 按 Python 语义跳过，--cores 小于等于 1 不需要并行映射");
    return Error::success();
  }
  if (Context.Options.Backend != "cbmc-ext") {
    Result.Notes.push_back(
        formatv("phase6: mapper 仅支持 cbmc-ext，当前 backend={0}，跳过 DIMACS 映射",
                Context.Options.Backend)
            .str());
    return Error::success();
  }

  if (!isPowerOfTwo(Context.Options.Cores)) {
    Result.Notes.push_back(
        "phase6: mapper 仅支持 2 的幂次 --cores，以便完整覆盖二进制 DIMACS 分片");
    return Error::success();
  }

  std::string Source = materializeSource(Context, Result);
  const std::string SourcePath = Context.Options.OutputPath + ".mapper-input.c";
  const std::string DimacsPath =
      Context.Options.OutputPath + ".u" + std::to_string(Context.Options.Unwind) +
      "c" + std::to_string(Context.Options.Contexts) + ".dimacs";
  if (!Context.Options.ReuseDimacs) {
    if (Error Err = writeFile(SourcePath, Source))
      return Err;
    CBMCRunConfig Config;
    Config.SourceFilePath = SourcePath;
    Config.Backend = "cbmc-ext";
    Config.Unwind = Context.Options.Unwind ? Context.Options.Unwind : 1;
    Config.DimacsOutputPath = DimacsPath;
    Config.BoundsCheck = false;
    Config.DivByZeroCheck = false;
    Config.PointerCheck = false;
    Config.ConversionCheck = false;
    Config.Trace = false;
    Config.NoLibrary = false;
    Config.ObjectBits = 0;
    Expected<CBMCRunResult> Run = runCBMC(Config, DimacsPath + ".mapper");
    if (!Run) {
      Result.Notes.push_back(
          formatv("phase6: mapper 无法生成 DIMACS，跳过并行分片；原因: {0}",
                  toString(Run.takeError()))
              .str());
      return Error::success();
    }
    if (Run->ExecutionFailed) {
      Result.Notes.push_back(
          formatv("phase6: mapper 生成 DIMACS 失败，exit={0}，日志: {1}",
                  Run->ExitCode, Run->CombinedLogPath)
              .str());
      return Error::success();
    }
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer = MemoryBuffer::getFile(DimacsPath);
  if (!Buffer) {
    Result.Notes.push_back(
        formatv("phase6: mapper 未找到 DIMACS 文件 {0}，跳过并行分片", DimacsPath)
            .str());
    return Error::success();
  }
  const unsigned SplitWidth =
      static_cast<unsigned>(std::log2(Context.Options.Cores));
  std::vector<unsigned> Bits = parseContextSwitchBits(
      Buffer.get()->getBuffer(), Context.Options.Contexts, Context.Options.Cores);
  if (Bits.size() != SplitWidth) {
    Result.Notes.push_back(
        formatv("phase6: mapper 未能从 DIMACS 的 __cs_tid context 映射中提取 {0} 个命题位，无法分为 {1} 个分片",
                Bits.size(), Context.Options.Cores)
            .str());
    return Error::success();
  }
  Result.BackendAssumptions = buildPartitions(Bits, Context.Options.Cores);
  Result.Notes.push_back(
      formatv("phase6: mapper 已从 DIMACS 提取 {0} 个调度位，生成 {1} 个 CBMC 分片",
              SplitWidth, Result.BackendAssumptions.size())
          .str());
  return Error::success();
}

} // namespace clang::nichecker
