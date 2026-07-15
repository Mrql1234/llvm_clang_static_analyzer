#include "clang-nichecker/Passes/InstrumenterPass.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatVariadic.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

using namespace llvm;

namespace clang::nichecker {

namespace {

bool shouldRunInstrumenter(const PipelineOptions &Options) {
  return Options.PipelineProfile == "lazy" ||
         StringRef(Options.PipelineSpec).contains("instrumenter");
}

unsigned bitWidth(unsigned Value) {
  unsigned Width = 1;
  while (Value > 1) {
    Value >>= 1;
    ++Width;
  }
  return Width;
}

std::vector<unsigned> parseThreadSizes(StringRef Source) {
  size_t Name = Source.find("__cs_thread_lines");
  if (Name == StringRef::npos)
    return {};

  size_t Equal = Source.find('=', Name);
  size_t OpenBrace = Source.find('{', Equal);
  if (Equal == StringRef::npos || OpenBrace == StringRef::npos)
    return {};
  size_t CloseBrace = Source.find('}', OpenBrace + 1);
  if (CloseBrace == StringRef::npos)
    return {};

  std::vector<unsigned> Sizes;
  StringRef Values = Source.slice(OpenBrace + 1, CloseBrace);
  SmallVector<StringRef, 8> Parts;
  Values.split(Parts, ',', -1, false);
  for (StringRef Part : Parts) {
    unsigned Value = 0;
    if (!Part.trim().getAsInteger(10, Value))
      Sizes.push_back(Value);
  }
  return Sizes;
}

std::string materializeRawLines(StringRef Source) {
  std::string Output;
  size_t Cursor = 0;
  constexpr StringRef Prefix = "__CSEQ_rawline(\"";
  while (Cursor < Source.size()) {
    size_t Start = Source.find(Prefix, Cursor);
    if (Start == StringRef::npos) {
      Output.append(Source.substr(Cursor));
      break;
    }
    Output.append(Source.slice(Cursor, Start));
    size_t StringStart = Start + Prefix.size();
    size_t EndQuote = StringStart;
    bool Escaped = false;
    for (; EndQuote < Source.size(); ++EndQuote) {
      char C = Source[EndQuote];
      if (C == '"' && !Escaped)
        break;
      Escaped = C == '\\' && !Escaped;
      if (C != '\\')
        Escaped = false;
    }
    if (EndQuote == Source.size() ||
        !Source.substr(EndQuote).starts_with("\");")) {
      Output.append(Source.substr(Start));
      break;
    }

    StringRef Raw = Source.slice(StringStart, EndQuote);
    for (size_t I = 0; I < Raw.size(); ++I) {
      if (Raw[I] == '\\' && I + 1 < Raw.size()) {
        char Next = Raw[++I];
        Output.push_back(Next == 'n' ? '\n' : Next);
      } else {
        Output.push_back(Raw[I]);
      }
    }
    Cursor = EndQuote + 3;
  }
  return Output;
}

void replaceAll(std::string &Source, StringRef From, StringRef To) {
  size_t Offset = 0;
  while ((Offset = Source.find(From, Offset)) != std::string::npos) {
    Source.replace(Offset, From.size(), To);
    Offset += To.size();
  }
}

void lowerControlVariable(std::string &Source, StringRef Name, unsigned Width) {
  size_t Cursor = 0;
  const std::string Needle = "unsigned " + Name.str();
  const std::string Replacement =
      "unsigned __CPROVER_bitvector[" + std::to_string(Width) + "] " +
      Name.str();
  while ((Cursor = Source.find(Needle, Cursor)) != std::string::npos) {
    Source.replace(Cursor, Needle.size(), Replacement);
    Cursor += Replacement.size();
  }
}

} // namespace

llvm::StringRef InstrumenterPass::name() const { return "instrumenter"; }

llvm::Error InstrumenterPass::run(const PipelineContext &Context,
                                  TransformResult &Result) const {
  if (!shouldRunInstrumenter(Context.Options))
    return Error::success();

  std::string Source = materializeRawLines(materializeSource(Context, Result));
  std::vector<unsigned> ThreadSizes = parseThreadSizes(Source);
  if (ThreadSizes.empty()) {
    // Raw lines still belong to the backend-only representation even when a
    // caller provides no lazyseq thread metadata.
    Result.Source = std::move(Source);
    Result.PendingReplacements.clear();
    Result.RequiresASTReparse = false;
    Result.Notes.push_back(
        "phase6: instrumenter 未发现 lazyseq 线程大小元数据，保持源码不变");
    return Error::success();
  }

  unsigned MaxThreadSize =
      *std::max_element(ThreadSizes.begin(), ThreadSizes.end());
  const unsigned PcWidth = bitWidth(MaxThreadSize);
  const unsigned ThreadWidth = bitWidth(ThreadSizes.size());

  replaceAll(Source, "__VERIFIER_assume", "__CPROVER_assume");
  replaceAll(Source, "__VERIFIER_assertext", "__CPROVER_assert");
  lowerControlVariable(Source, "__cs_active_thread", 1);
  lowerControlVariable(Source, "__cs_pc", PcWidth);
  lowerControlVariable(Source, "__cs_pc_cs", PcWidth);
  lowerControlVariable(Source, "__cs_thread_lines", PcWidth);
  lowerControlVariable(Source, "__cs_thread_index", ThreadWidth);
  Source = "#define __CS_LAZY_INSTRUMENTED 1\n" + Source;

  Result.Source = std::move(Source);
  Result.PendingReplacements.clear();
  Result.RequiresASTReparse = false;
  Result.Notes.push_back(
      formatv("phase6: instrumenter 已映射 CBMC assume，并降级控制变量 bitwidth(pc={0}, thread={1})",
              PcWidth, ThreadWidth)
          .str());
  return Error::success();
}

} // namespace clang::nichecker
