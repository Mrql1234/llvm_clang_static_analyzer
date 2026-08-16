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
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
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

static cl::opt<std::string>
    PipelineProfileOpt("pipeline-profile", cl::cat(ClangNICheckerCategory),
                       cl::desc("选择预定义模块链，支持 default/lazy/shenfei"));

static cl::opt<bool>
    EnableCBMCOpt("enable-cbmc", cl::cat(ClangNICheckerCategory),
                  cl::desc("把 cbmc-driver 模块加入当前模块链"));

static cl::opt<unsigned>
    UnwindOpt("unwind", cl::cat(ClangNICheckerCategory),
              cl::desc("循环展开默认上界"), cl::init(1));

static cl::opt<unsigned>
    RoundsOpt("rounds", cl::cat(ClangNICheckerCategory),
              cl::desc("lazy 调度轮数上界"), cl::init(1));

static cl::opt<unsigned>
    ContextsOpt("contexts", cl::cat(ClangNICheckerCategory),
                cl::desc("context-bounded DIMACS 映射上下文数"), cl::init(0));

static cl::opt<unsigned>
    CoresOpt("cores", cl::cat(ClangNICheckerCategory),
             cl::desc("mapper 并行分支数"), cl::init(1));

static cl::opt<unsigned>
    ThreadsOpt("threads", cl::cat(ClangNICheckerCategory),
               cl::desc("pthread_create 静态创建上界，0 表示不限制"), cl::init(0));

static cl::opt<std::string>
    ScheduleOpt("schedule", cl::cat(ClangNICheckerCategory),
                cl::desc("lazy 轮次调度限制，例如 0,1:2 或 +:+"));

static cl::opt<std::string>
    InterruptConfigOpt("isr-config", cl::cat(ClangNICheckerCategory),
                       cl::desc("ISR 元数据 JSON 文件，替代旧 Python dictfile"));

static cl::opt<bool>
    NoRoundRobinOpt("norobin", cl::cat(ClangNICheckerCategory),
                    cl::desc("使用 Python lazyseq 的非轮转调度器"));

static cl::opt<bool>
    NondetCondvarWakeupsOpt(
        "nondet-condvar-wakeups", cl::cat(ClangNICheckerCategory),
        cl::desc("允许 lazy 条件变量和 barrier 发生伪唤醒"));

static cl::opt<bool>
    EnableLoopAbstractionOpt("loop-abs", cl::cat(ClangNICheckerCategory),
                             cl::desc("启用 lazy.chain 的 LoopAbstraction 循环摘要"));

static cl::opt<std::string>
    BackendOpt("backend", cl::cat(ClangNICheckerCategory),
               cl::desc("后端名称，例如 cbmc 或 cbmc-ext"), cl::init("cbmc"));

static cl::opt<bool>
    EnableLegacySliceJarOpt("pro-slice", cl::cat(ClangNICheckerCategory),
                            cl::desc("启用 Python proSlice 对应的 legacy slice jar"));

static cl::opt<bool>
    EnableLegacyLabelJarOpt("label-reduc", cl::cat(ClangNICheckerCategory),
                            cl::desc("启用 Python labelReduc 对应的 legacy label jar"));

static cl::opt<std::string>
    SliceVariableOpt("slice-var", cl::cat(ClangNICheckerCategory),
                     cl::desc("legacy jar 使用的 Python globalVariable"));

static cl::opt<std::string>
    SliceModeOpt("slice-mode", cl::cat(ClangNICheckerCategory),
                 cl::desc("legacy jar 使用的 Python mode，例如 rww"));

static cl::opt<std::string>
    SvpConfigOpt("svp-config", cl::cat(ClangNICheckerCategory),
                 cl::desc("单变量访问序 JSON 配置，替代 Python modefile"));

static cl::opt<std::string>
    SvpModeOpt("svp-mode", cl::cat(ClangNICheckerCategory),
               cl::desc("单变量访问序模式：rww/wwr/rwr/wrw"));

static cl::opt<std::string>
    SvpTypeOpt("svp-type", cl::cat(ClangNICheckerCategory),
               cl::desc("单变量访问序目标类型，例如 int 或 int *"));

static cl::opt<std::string>
    SvpVariableOpt("svp-var", cl::cat(ClangNICheckerCategory),
                   cl::desc("单变量访问序目标，例如 counter 或 data[3]"));

static cl::opt<bool>
    ReuseDimacsOpt("reuse-dimacs", cl::cat(ClangNICheckerCategory),
                   cl::desc("复用 mapper 已生成的 DIMACS 文件"));

static cl::opt<unsigned>
    UnwindWhileOpt("unwind-while", cl::cat(ClangNICheckerCategory),
                   cl::desc("while 循环展开上界，默认继承 --unwind"),
                   cl::init(0));

static cl::opt<unsigned>
    UnwindForOpt("unwind-for", cl::cat(ClangNICheckerCategory),
                 cl::desc("for 循环展开上界，默认继承 --unwind"),
                   cl::init(0));

struct SvpConfig {
  std::string Mode;
  std::string Type;
  std::string Variable;
};

Expected<SvpConfig> loadSvpConfig(StringRef Path) {
  auto Buffer = MemoryBuffer::getFile(Path);
  if (!Buffer)
    return errorCodeToError(Buffer.getError());
  Expected<json::Value> Parsed = json::parse((*Buffer)->getBuffer());
  if (!Parsed)
    return Parsed.takeError();
  const json::Object *Root = Parsed->getAsObject();
  if (!Root)
    return createStringError(inconvertibleErrorCode(),
                             "SVP 配置必须是 JSON 对象: %s",
                             Path.str().c_str());

  SvpConfig Config;
  if (auto Mode = Root->getString("mode"))
    Config.Mode = Mode->str();
  if (auto Type = Root->getString("type"))
    Config.Type = Type->str();
  if (auto Variable = Root->getString("globalVariable"))
    Config.Variable = Variable->str();
  return Config;
}

bool isValidSvpMode(StringRef Mode) {
  return Mode.empty() || Mode == "rww" || Mode == "wwr" || Mode == "rwr" ||
         Mode == "wrw";
}

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
  Options.PipelineProfile = PipelineProfileOpt;
  Options.Unwind = UnwindOpt;
  Options.Rounds = RoundsOpt;
  Options.Contexts = ContextsOpt;
  Options.Cores = CoresOpt;
  Options.Threads = ThreadsOpt;
  Options.Schedule = ScheduleOpt;
  Options.InterruptConfigPath = InterruptConfigOpt;
  Options.NoRoundRobin = NoRoundRobinOpt;
  Options.NondetCondvarWakeups = NondetCondvarWakeupsOpt;
  Options.EnableLoopAbstraction = EnableLoopAbstractionOpt;
  Options.Backend = BackendOpt;
  Options.EnableLegacySliceJar = EnableLegacySliceJarOpt;
  Options.EnableLegacyLabelJar = EnableLegacyLabelJarOpt;
  Options.SliceVariable = SliceVariableOpt;
  Options.SliceMode = SliceModeOpt;
  Options.SvpMode = SvpModeOpt;
  Options.SvpType = SvpTypeOpt;
  Options.SvpVariable = SvpVariableOpt;
  if (!SvpConfigOpt.empty()) {
    Expected<SvpConfig> Config = loadSvpConfig(SvpConfigOpt);
    if (!Config) {
      errs() << "[clang-nichecker] 无法读取 --svp-config: "
             << toString(Config.takeError()) << "\n";
      return 1;
    }
    if (Options.SvpMode.empty())
      Options.SvpMode = Config->Mode;
    if (Options.SvpType.empty())
      Options.SvpType = Config->Type;
    if (Options.SvpVariable.empty())
      Options.SvpVariable = Config->Variable;
  }
  if (!isValidSvpMode(Options.SvpMode)) {
    errs() << "[clang-nichecker] --svp-mode 只支持 rww/wwr/rwr/wrw\n";
    return 1;
  }
  if (!Options.SvpMode.empty() && Options.SvpVariable.empty()) {
    errs() << "[clang-nichecker] 使用 --svp-mode 时必须提供 --svp-var 或 --svp-config\n";
    return 1;
  }
  // Python modefile also fed the slice jar. Keep a direct --slice-* argument
  // authoritative while making the JSON replacement useful to that bridge.
  if (Options.SliceMode.empty())
    Options.SliceMode = Options.SvpMode;
  if (Options.SliceVariable.empty())
    Options.SliceVariable = Options.SvpVariable;
  Options.ReuseDimacs = ReuseDimacsOpt;
  Options.UnwindWhile = UnwindWhileOpt ? UnwindWhileOpt : UnwindOpt;
  Options.UnwindFor = UnwindForOpt ? UnwindForOpt : UnwindOpt;
  Options.EnableCBMC = EnableCBMCOpt;

  auto Factory = clang::nichecker::createActionFactory(Options);
  return Tool.run(Factory.get());
}
