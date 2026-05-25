//===--- ClangNIChecker.cpp - NIChecker migration tool -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang-nichecker/Driver/Frontend.h"
#include "clang-nichecker/Support/Types.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

namespace {

static cl::OptionCategory ClangNICheckerCategory("clang-nichecker options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

static cl::opt<std::string>
    OutputPathOpt("output", cl::cat(ClangNICheckerCategory),
                  cl::desc("指定输出源码文件路径"));

static cl::opt<bool>
    PrintAnalysisOpt("print-analysis", cl::cat(ClangNICheckerCategory),
                     cl::desc("打印程序分类与入口分析结果"));

static cl::opt<std::string>
    PipelineOpt("pipeline", cl::cat(ClangNICheckerCategory),
                cl::desc("以逗号分隔的模块链配置，留空时使用默认模块链"));

static cl::opt<bool>
    EnableCBMCOpt("enable-cbmc", cl::cat(ClangNICheckerCategory),
                  cl::desc("把 cbmc-driver 模块加入当前模块链"));

} // namespace

int main(int argc, const char **argv) {
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  auto ExpectedParser =
      CommonOptionsParser::create(argc, argv, ClangNICheckerCategory);
  if (!ExpectedParser) {
    errs() << toString(ExpectedParser.takeError()) << "\n";
    return 1;
  }

  CommonOptionsParser &OptionsParser = ExpectedParser.get();
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
      {"-include", "assert.h", "-include", "pthread.h", "-include",
       "stddef.h", "-Wno-error=implicit-function-declaration",
       "-Wno-error=return-type", "-Wno-error=return-mismatch"},
      ArgumentInsertPosition::BEGIN));

  clang::nichecker::PipelineOptions Options;
  Options.OutputPath = OutputPathOpt;
  Options.PrintAnalysis = PrintAnalysisOpt;
  Options.PipelineSpec = PipelineOpt;
  Options.EnableCBMC = EnableCBMCOpt;

  auto Factory = clang::nichecker::createActionFactory(Options);
  return Tool.run(Factory.get());
}
