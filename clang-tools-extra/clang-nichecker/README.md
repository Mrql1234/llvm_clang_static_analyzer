# clang-nichecker

## 项目定位

`clang-nichecker` 的目标是把旧版 `nichecker/Cseq` 的 Python 源码到源码处理链路，逐步迁移到 C++ / Clang 体系下的 in-tree 工具。

当前关注点不是单条规则检查，而是保留旧 nichecker 的整体处理流程：

1. 读取顺序程序、多线程程序或中断程序。
2. 按 pipeline 依次做 AST 分析、源码改写、旧 jar 桥接和顺序化。
3. 产出新的 C 源码。
4. 在可行时继续对接 CBMC。

## 当前入口

主入口文件：

1. `clang-tools-extra/clang-nichecker/ClangNIChecker.cpp`
2. `clang-tools-extra/clang-nichecker/lib/Driver/Frontend.cpp`
3. `clang-tools-extra/clang-nichecker/lib/Driver/PipelineBuilder.cpp`

主要模块入口：

1. `clang-tools-extra/clang-nichecker/lib/Passes/SlicePass.cpp`
2. `clang-tools-extra/clang-nichecker/lib/Passes/LabelInsertionPass.cpp`
3. `clang-tools-extra/clang-nichecker/lib/Passes/SliceSeqProgramPass.cpp`
4. `clang-tools-extra/clang-nichecker/lib/Passes/SequentializationPass.cpp`
5. `clang-tools-extra/clang-nichecker/lib/Passes/FeederPass.cpp`
6. `clang-tools-extra/clang-nichecker/lib/Passes/FeederSeqProgramPass.cpp`
7. `clang-tools-extra/clang-nichecker/lib/Backend/CBMCDriverPass.cpp`
8. `clang-tools-extra/clang-nichecker/lib/Support/LegacyJarRunner.cpp`
9. `clang-tools-extra/clang-nichecker/lib/Analysis/ProgramAnalyzer.cpp`

## 当前已经落地的能力

### 真实 pass

当前已经不是占位的 pass 包括：

1. `ProgramClassifierPass`
2. `InterruptLoweringPass`
3. `ConditionExtractionPass`
4. `VariableRenamingPass`
5. `LoopUnrollPass`
6. `SequentializationPass`
7. `SourceEmissionPass`
8. `SlicePass`
9. `LabelInsertionPass`
10. `SliceSeqProgramPass`
11. `FeederPass`
12. `FeederSeqProgramPass`
13. `CBMCDriverPass`

### 旧 jar 桥接

当前已经接通的旧 jar 包：

1. `nichecker/Cseq/testSlice.jar`
2. `nichecker/Cseq/testLabelReduc.jar`
3. `nichecker/Cseq/testSlice_seqProgramSlice.jar`

这些桥接统一由 `lib/Support/LegacyJarRunner.cpp` 负责：

1. 只查找当前系统 `PATH` 里的 `java`。
2. 不再调用 Windows `java.exe`。
3. 输入源码和 `data.json` 会先写入临时目录，再调用 `java -jar ...`。
4. 当前 `slice` 使用旧协议入口名 `main_task`。
5. 当前 `label-insertion` 使用旧协议入口名 `main_task_0`。

## 2026-06 新增：AST 重解析机制

### 背景

之前的 C++ pipeline 只在最开始 parse 一次，后续所有 AST pass 都共享第一次的 `CompilerInstance`。只要前面的 pass 或 jar 已经改过源码，后面的 AST pass 看到的仍然是旧 AST，偏移和语义基准都可能过时。

### 入口文件

这次机制主要落在下面几个文件：

1. `clang-tools-extra/clang-nichecker/lib/Driver/Frontend.cpp`
2. `clang-tools-extra/clang-nichecker/include/clang-nichecker/Support/Types.h`
3. `clang-tools-extra/clang-nichecker/lib/Support/LegacyJarRunner.cpp`
4. `clang-tools-extra/clang-nichecker/lib/Passes/SequentializationPass.cpp`

### 现在的行为

现在每个 pass 结束后，驱动层会做下面的事情：

1. 先把 `Result.Source` 或 `PendingReplacements` 物化成“当前源码”。
2. 如果这份当前源码和上一个 pass 的输入源码不同，就复制当前 `CompilerInvocation`。
3. 把主文件 remap 成当前源码。
4. 调用 `ASTUnit::LoadFromCompilerInvocation(...)` 重新生成 AST。
5. 下一个 pass 读到的 `PipelineContext` 就会切换到“当前 AST 会话 + 当前源码”。

`PipelineContext` 现在通过 `TranslationUnitHandle` 统一提供：

1. `ASTContext`
2. `SourceManager`
3. `LangOptions`
4. `CurrentSource`

### 直接修复的点

1. `slice_seqprogram` 改完源码后，`SequentializationPass` 可以基于新 AST 继续工作，不再退回旧的文本兜底分支。
2. 后续 AST pass 不会再对第一次 parse 的旧源码计算 replacement。
3. `LegacyJarRunner` 的源码物化基准已经改成 `CurrentSource`。

## 2026-06 新增：阶段化摘要刷新

### 背景

重解析接通以后，`ProgramSummary` 里有两类信息不能混在一起：

1. 稳定语义摘要。
2. 当前 AST 绑定字段。

如果重解析后整包重算摘要，`lazy` 在 `slice` 后会因为中间源码看起来像顺序程序，被误判成 `sequential`，从而错误跳过 `label-insertion`。

### 入口文件

这部分改动主要在：

1. `clang-tools-extra/clang-nichecker/include/clang-nichecker/Analysis/ProgramAnalyzer.h`
2. `clang-tools-extra/clang-nichecker/lib/Analysis/ProgramAnalyzer.cpp`
3. `clang-tools-extra/clang-nichecker/lib/Driver/Frontend.cpp`

### 当前行为

现在的摘要刷新规则是：

1. `ProgramSummary.Kind`、线程入口、ISR 名单这些仍然保留分类阶段的稳定语义，不会因为中间 jar 产物长得像顺序程序就改变 pipeline 分支。
2. `ProgramSummary.MainFunction` 会在每次重解析后刷新到当前 AST，避免悬空指针和旧 AST 绑定。

## 2026-06 新增：中断链路修正

### 入口文件

这部分改动主要在：

1. `clang-tools-extra/clang-nichecker/lib/Passes/InterruptLoweringPass.cpp`
2. `clang-tools-extra/clang-nichecker/lib/Passes/LoopUnrollPass.cpp`

### 当前行为

1. `InterruptLoweringPass` 已经改成完全基于 `CurrentSource` 工作，不再依赖 pass 边界前的 `Result.Source`。
2. `LoopUnrollPass` 修掉了 `while/for` 展开时多输出一个 `}` 的问题。

## 构建命令

仓库根目录：

```bash
cd /home/ql/code/llvm_clang_static_analyzer
```

稳定构建命令：

```bash
cd /home/ql/code/llvm_clang_static_analyzer/build-csa
ninja -j1 clang-nichecker
```

说明：

1. `build-csa` 已经配置为 `Ninja` 生成器。
2. 当前 WSL 内存较紧时，并行编译容易把多个 `cc1plus` 顶掉，所以这里优先记录 `-j1` 的稳定命令。

## 回归命令

### shenfei 回归

入口文件：

1. `nichecker/Cseq/examples/example011.c`

运行命令：

```bash
cd /home/ql/code/llvm_clang_static_analyzer
env PATH=/home/ql/.local/java/jdk-11.0.31+11-jre/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  build-csa/bin/clang-nichecker --pipeline-profile=shenfei -print-analysis \
  -output=/tmp/shenfei_reparse_refresh2.c \
  nichecker/Cseq/examples/example011.c -- -I./nichecker/Cseq/core/include
```

当前结果：

1. `slice_seqprogram` 成功调用旧 jar。
2. jar 产物会被重新 parse。
3. `SequentializationPass` 能继续重写入口。
4. `FeederSeqProgramPass` 仍然得到 `SAFE`。

### lazy 回归

入口文件：

1. `nichecker/Cseq/examples/lazy_unsafe.c`

运行命令：

```bash
cd /home/ql/code/llvm_clang_static_analyzer
env PATH=/home/ql/.local/java/jdk-11.0.31+11-jre/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  build-csa/bin/clang-nichecker --pipeline-profile=lazy -print-analysis \
  -output=/tmp/lazy_reparse_refresh2.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
```

当前结果：

1. `slice` 和 `label-insertion` 的旧 jar 调用没有被 AST 重解析机制打断。
2. `ProgramSummary` 不会再因为中间源码形态变化而把 `lazy` 主链路误判成顺序程序。
3. `feeder` 仍然会识别源码中残留的 `pthread_*` / `addLabel()` 等并发痕迹。
4. 当前 `lazyseq` / `replacegoto` 真实语义还没迁完，所以这里仍然会明确跳过直接喂给 CBMC。

### 中断样例回归

入口文件：

1. `nichecker/Cseq/examples/mytest_3.c`

运行命令：

```bash
cd /home/ql/code/llvm_clang_static_analyzer
env PATH=/home/ql/.local/java/jdk-11.0.31+11-jre/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  build-csa/bin/clang-nichecker -print-analysis \
  -output=/tmp/interrupt_reparse_refresh4.c \
  nichecker/Cseq/examples/mytest_3.c -- -I./nichecker/Cseq/core/include
```

如果想单独检查产物语法，可以再执行：

```bash
clang -fsyntax-only /tmp/interrupt_reparse_refresh4.c
```

当前结果：

1. `phase2` 已经能稳定进入 `interrupt-lowering`，不再因为旧的 `Result.Source` 边界检查而直接跳过。
2. `loop-unroll` 生成的括号结构已经修正。
3. 这个样例现在剩下的主要问题来自样例自身历史问题，例如 `ISR_L` 里 `return NULL;` 与函数返回类型不匹配，以及 `pthread_create` 还没有声明，不再是这次重解析机制引入的新语法错误。

## 当前限制

1. `lazy` 主链路还没有把旧版 `lazyseq` / `replacegoto` 的真实顺序化语义迁完。
2. 仍然有一批旧模块只是占位映射。
3. `CBMCDriverPass` 和 `feeder_seqprogram` 还没有补齐旧版全部参数与 counterexample 抽取逻辑。
4. `ProgramSummary` 目前采用“稳定语义摘要 + 当前 AST 绑定字段”的折中方案；如果后续要支持更细粒度的阶段性语义切换，还需要继续拆分摘要模型。

## 建议的后续推进方向

1. 继续把旧链路中的真实语义模块拆成独立 C++ pass。
2. 优先补 `lazyseq` / `replacegoto`，把多线程到顺序程序的主链路打通。
3. 在 AST 重解析机制已经稳定的前提下，再考虑把 legacy jar 的“去函数定义再拼回去”逻辑进一步往真正的 AST 级实现靠拢。
