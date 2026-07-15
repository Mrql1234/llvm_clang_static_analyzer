#include "clang-nichecker/Passes/ReplaceGotoPass.h"
#include "clang-nichecker/Support/LegacyJarRunner.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FormatVariadic.h"

#include <cctype>
#include <cstring>
#include <string>

using namespace llvm;

namespace clang::nichecker {

namespace {

bool isCommentLine(StringRef Line) {
  return Line.trim().starts_with("//") || Line.trim().starts_with("/*");
}

bool parseIF(StringRef Line, std::string &Thread, std::string &NextLabel) {
  size_t Start = Line.find("IF(");
  if (Start == StringRef::npos || Line.contains("#define"))
    return false;
  size_t End = Line.find(')', Start + 3);
  if (End == StringRef::npos)
    return false;
  SmallVector<StringRef, 4> Args;
  Line.slice(Start + 3, End).split(Args, ',', -1, false);
  if (Args.size() != 3)
    return false;
  Thread = Args[0].trim().str();
  NextLabel = Args[2].trim().str();
  return !Thread.empty() && !NextLabel.empty();
}

std::string currentLabel(StringRef Line) {
  size_t Colon = Line.find(':');
  if (Colon == StringRef::npos)
    return "first";
  return Line.take_front(Colon).trim().str();
}

std::string labelProgramCounter(StringRef Label) {
  size_t Underscore = Label.rfind('_');
  if (Underscore == StringRef::npos)
    return "";
  StringRef Value = Label.drop_front(Underscore + 1);
  if (Value.empty() ||
      !std::all_of(Value.begin(), Value.end(), [](char C) {
        return std::isdigit(static_cast<unsigned char>(C));
      }))
    return "";
  return Value.str();
}

} // namespace

llvm::StringRef ReplaceGotoPass::name() const { return "replacegoto"; }

llvm::Error ReplaceGotoPass::run(const PipelineContext &Context,
                                 TransformResult &Result) const {
  std::string Source = materializeSource(Context, Result);
  SmallVector<StringRef, 128> Lines;
  StringRef(Source).split(Lines, '\n', -1, false);

  StringMap<std::string> ExitLabels;
  std::string Label;
  for (StringRef Line : Lines) {
    if (isCommentLine(Line))
      continue;
    std::string Thread;
    std::string NextLabel;
    if (parseIF(Line, Thread, NextLabel))
      Label = currentLabel(Line);
    size_t Exit = Line.find("__exit_loop_");
    if (Exit != StringRef::npos && !Line.contains("goto")) {
      size_t End = Line.find(':', Exit);
      if (End != StringRef::npos)
        ExitLabels[Line.slice(Exit, End).trim()] = Label;
    }
  }

  unsigned Replaced = 0;
  std::string Thread;
  std::string NextLabel;
  std::string Output;
  raw_string_ostream OS(Output);
  for (StringRef Line : Lines) {
    if (!isCommentLine(Line))
      (void)parseIF(Line, Thread, NextLabel);

    size_t Goto = Line.find("goto __exit_loop_");
    if (Goto == StringRef::npos || isCommentLine(Line)) {
      OS << Line << '\n';
      continue;
    }
    size_t NameStart = Goto + strlen("goto ");
    size_t NameEnd = Line.find(';', NameStart);
    if (NameEnd == StringRef::npos || Thread.empty() || NextLabel.empty()) {
      OS << Line << '\n';
      continue;
    }
    StringRef Target = Line.slice(NameStart, NameEnd).trim();
    auto It = ExitLabels.find(Target);
    if (It == ExitLabels.end()) {
      OS << Line << '\n';
      continue;
    }
    std::string ProgramCounter = labelProgramCounter(It->second);
    if (ProgramCounter.empty()) {
      OS << Line << '\n';
      continue;
    }
    unsigned Indent = Line.find("goto");
    OS.indent(Indent) << "__CPROVER_assume(__cs_pc_cs[" << Thread
                      << "] >= " << (std::stoul(ProgramCounter) + 1)
                      << ");\n";
    OS.indent(Indent) << "__cs_pc[" << Thread << "] = " << ProgramCounter
                      << ";\n";
    OS.indent(Indent) << "goto " << NextLabel << ";\n";
    ++Replaced;
  }
  OS.flush();

  Result.Source = std::move(Output);
  Result.PendingReplacements.clear();
  Result.RequiresASTReparse = false;
  Result.Notes.push_back(
      formatv("phase6: replacegoto 原生替换了 {0} 个 __exit_loop 跳转", Replaced)
          .str());
  return Error::success();
}

} // namespace clang::nichecker
