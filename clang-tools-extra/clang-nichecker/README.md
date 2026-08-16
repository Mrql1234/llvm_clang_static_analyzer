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
8. `clang-tools-extra/clang-nichecker/lib/Passes/LazySequentializationPass.cpp`
9. `clang-tools-extra/clang-nichecker/lib/Support/LegacyJarRunner.cpp`
10. `clang-tools-extra/clang-nichecker/lib/Analysis/ProgramAnalyzer.cpp`

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

1. 优先使用 `JAVA_HOME/bin/java`，其次在 `~/.local/java` 和 `/usr/lib/jvm` 查找 Linux JDK 11，最后才回退到当前 `PATH` 的 `java`。
2. WSL 中会避开 Windows `java.exe` 符号链接，避免它不能读取 `/tmp` 和工作区的 Linux 绝对路径。
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

## 2026-07 新增：native lazyseq AST 重写

### 背景

之前 `lazy` profile 虽然在 C++ pipeline 里保留了 `lazyseq / instrumenter / replacegoto / feeder` 这些阶段名，但真正的多线程顺序化主体还没迁到 C++。本次先迁移 `lazyseq` 的 AST 重写主体：

1. 从当前 AST 收集 `main` 和 `pthread_create` 对应的线程入口。
2. 将每个线程函数改写为由 `__cs_pc / __cs_pc_cs` 控制的语句片段。
3. 将原始 `main` 重命名为 `main_thread`，再根据 `--rounds` 生成轮次调度器 `main`。
4. 局部变量提升为函数静态存储，并将初始化改为受程序计数器守卫的赋值，避免分段执行后失去作用域。

### 入口文件

这次接通 lazy 主链路的入口主要在：

1. `clang-tools-extra/clang-nichecker/lib/Passes/LazySequentializationPass.cpp`
2. `clang-tools-extra/clang-nichecker/lib/Driver/PipelineBuilder.cpp`
3. `clang-tools-extra/clang-nichecker/lib/Driver/Frontend.cpp`
4. `clang-tools-extra/clang-nichecker/lib/Passes/FeederPass.cpp`

### 当前行为

1. `lazyseq` 已是原生 C++ pass，不再调用 Python bridge 生成源码。
2. `Frontend` 会在 `lazyseq` 完成源码改写后重解析；因此 `lazyseq` 后面的 AST pass 看到的是当前源码，而不是初始 AST。
3. `__CPROVER_bitvector[...]` 是后续 `instrumenter` 的后端语法；在该 pass 之前，native lazyseq 只生成标准 C 的 `unsigned` 控制变量，保持可重解析。
4. `instrumenter` 及其之后的模块将不再要求 AST 重解析；`feeder` 会将该后端文本交给 CBMC。事件/定时器、单变量访问序和完整 runtime 语义仍在继续收敛。
5. 当前已覆盖默认轮次调度、直接 `pthread_create` / `pthread_join` 和线程入口的程序计数器切片；显式 schedule、context-bounded scheduler、ISR 优先级与时间约束仍待继续迁移。

### 与 Python `lazyseq` 的对比结论

当前 C++ `LazySequentializationPass` 不是 Python `lazyseq` 的完整等价实现，而是可重解析的第一阶段迁移。下面命令用于复现对比：

```bash
cd /home/q/code/llvm_clang_static_analyzer/nichecker/Cseq
python3 cseq.py -l lazy_until_replacegoto --input examples/lazy_unsafe.c --unwind 1 --rounds 1 -D

cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,lazyseq --rounds=1 -print-analysis \
  -output=/tmp/native_lazyseq.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
```

Python 命令的 `lazyseq` 中间产物默认写入 `nichecker/Cseq/log/*_output__lazyseq.c`；第二条命令生成 C++ 版本 `/tmp/native_lazyseq.c`。

| 语义 | Python `lazyseq` | 当前 C++ `LazySequentializationPass` |
| --- | --- | --- |
| 线程入口 | 依赖前序 `duplicator`，调度复制后的 `thread1_0` 等入口 | 从 `ProgramSummary.ThreadEntryFunctions` 收集原始 `pthread_create` 入口 |
| 语句切片 | 用 `IF(thread, pc, next_label)`、标签和 `goto` 表示可恢复控制流 | 用 `__cs_pc <= index < __cs_pc_cs` 守卫函数体顶层语句 |
| `main` 与创建线程 | 改成 `main_thread`，调用 `pthread_create_2` 维护线程状态 | 改成 `main_thread`，内联改写直接 `pthread_create` 为线程状态赋值 |
| pthread 运行时 | 注入 `lazyseqB.c` 的 create/join/lock/cond/barrier/key 模型 | 仅注入控制变量，尚未注入运行时模型 |
| 输出元数据 | 输出 `header`、`bitwidth`、`threadsizes`、`threadendlines` 和中断字典 | 目前只在生成源码中保留线程大小数组 |
| 调度变体 | 支持轮次、显式 schedule、contexts、norobin 及随机/事件/定时中断 | 目前仅支持默认轮次调度 |

因此当前 C++ pass 已验证“改写后可重新解析”，但尚不能声称完成 Python `lazyseq` 的语义迁移。下一步应按上表先补齐标签化可恢复控制流和 `lazyseqB` pthread 运行时模型，再迁移中断调度分支。

## 后续迁移记录：instrumenter

### Python 入口与职责

Python 实现的入口是 `nichecker/Cseq/pycparser/newParser/c_generator.py` 的 `visit_Compound()`，约在 1471 行；`nichecker/Cseq/modules/instrumenter.py` 的 `loadfromstring()` 负责准备输入参数、后端映射和最终头文件注入。

`instrumenter` 不再产出需要 Clang AST 重解析的普通 C，而是将 lazyseq 的可解析中间表示降级为面向 CBMC 等后端的源码。因此 C++ pipeline 应在该 pass 之后停止重解析，只将源码交给 `replacegoto`、`mapper`、`cex` 和后端。

### 算法拆分

1. 后端符号映射：根据 backend 将 `__VERIFIER_assume`、断言和 nondet 原语映射到 `__CPROVER_assume`、`assert`、`nondet_*` 等目标名称。
2. 位宽降级：消费 lazyseq 输出的 `bitwidth` 元数据，将对应整型声明改成 `unsigned __CPROVER_bitvector[k]`；同时将 ISR 屏蔽数组 `__cs_disable_thread` 降级为 1 位 bitvector，并修复 bitvector 数组初始化表达式。
3. 原始行物化：去掉 `__CSEQ_rawline()` 包装和行标记，恢复 `IF(...)`、标签和 scheduler 中需要原样交给后端的 C 片段，并重新排版缩进。
4. 运行时与头文件：拼接 lazyseq 传来的 `header`，再按 backend 注入 `cbmc_extra.c`、pthread 定义、系统头信息和最终文件头。
5. `main_thread` 特化：识别事件/定时线程的 `pthread_create` 与初始化，避免在普通启动路径重复创建这些线程。
6. `main_task_0` 特化：移除前序 unroller 引入的 `while(1)`，在事件赋值处创建事件线程，在周期计数达到约束时创建定时线程，并跳到对应标签恢复主任务。
7. 单变量访问序：当模式为 `rww`、`wwr`、`rwr`、`wrw` 时，针对普通变量和指针分别插入读写记录、地址跟踪和断言；对循环抽象的伪读写跳过插桩。

### C++ 迁移顺序

1. 新建 `InstrumenterPass`，先实现后端符号映射、`__CSEQ_rawline` 物化、bitvector 声明和静态 runtime/header 拼接。
2. 将 native lazyseq 的线程大小、位宽和标签信息扩展为 `TransformResult` 元数据，替换 Python 模块间的 `outputparam` 传递。
3. 在 `InstrumenterPass` 中实现 `main_thread` 与 `main_task_0` 的事件/定时线程分支。
4. 最后迁移四种单变量访问序模式，并为每种模式建立独立回归样例。

## 构建命令

仓库根目录：

```bash
cd /home/ql/code/llvm_clang_static_analyzer
```

稳定构建命令：

```bash
cd /home/ql/code/llvm_clang_static_analyzer
cmake -S llvm -B build-clang -G Ninja -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra'
ninja -C build-clang -j1 clang-nichecker
```

说明：

1. 当前实际验证通过的是 `build-clang` 目录。
2. 当前 WSL 内存较紧时，并行编译容易把多个 `cc1plus` 顶掉，所以这里优先记录 `-j1` 的稳定命令。

## LoopAbstraction 原生迁移

入口文件：

1. `clang-tools-extra/clang-nichecker/include/clang-nichecker/Passes/LoopAbstractionPass.h`
2. `clang-tools-extra/clang-nichecker/lib/Passes/LoopAbstractionPass.cpp`
3. `clang-tools-extra/clang-nichecker/ClangNIChecker.cpp` 中的 `--loop-abs`

Python 的开关来自 `nichecker/Cseq/modules/absOption`；默认关闭。C++ 默认同样关闭，开启后对非 `while(1)` 的标量归纳变量构造摘要：保存循环前值、以 `nondet_int()` 选择迭代次数/重置值、插入 `__CPROVER_assume`，并对循环内 `assert` 使用一次具体执行、一次假设执行、一次具体执行的归纳检查结构。数组、结构体、指针及嵌套循环暂不在该 pass 中摘要化，保留原循环，以避免用错误的摘要替换真实程序。

单模块验证命令：

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline=LoopAbstraction --loop-abs -print-analysis \
  -output=/tmp/lazy-loop-abs-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-loop-abstraction-input.c --
build-clang/bin/clang -fsyntax-only /tmp/lazy-loop-abs-native.c
```

与 Python 链路对比时，先在 `nichecker/Cseq/modules/absOption` 中开启 `loopAbs`，再运行：

```bash
cd /home/q/code/llvm_clang_static_analyzer/nichecker/Cseq
python3 cseq.py -l lazy --input /tmp/loop-input.c --unwind 1 --rounds 1 -D
```

Python 的中间输出位于 `nichecker/Cseq/log/*_output__LoopAbstraction.c`；将其与 `/tmp/lazy-loop-abs-native.c` 对比保存变量、nondet 约束、循环体和末尾否定条件约束。对比关注语义结构，不以变量编号或排版差异作为不一致依据。归纳变量依赖关系使用 AST `DeclRefExpr` 判定而非字符串匹配，回归输入中的 `i += initial` 用于防止 `i` 被错误地视为 `initial` 的子串。

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
cd /home/q/code/llvm_clang_static_analyzer
env PATH=/home/ql/.local/java/jdk-11.0.31+11-jre/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  build-clang/bin/clang-nichecker --pipeline-profile=lazy --rounds=1 --unwind=1 -print-analysis \
  -output=/tmp/clang_nichecker_lazy_bridge.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
```

当前结果：

1. `lazyseq` 由 C++ AST pass 原生重写，不再调用 Python bridge。
2. 改写后的 `/tmp/native_lazyseq.c` 能通过 `build-clang/bin/clang -fsyntax-only`。
3. `instrumenter / replacegoto` 仍在迁移中，因此当前不会把该中间产物直接交给 CBMC。

### lazy 中断样例回归

入口文件：

1. `nichecker/Cseq/examples/_mt_mytest_3.c`

运行命令：

```bash
cd /home/q/code/llvm_clang_static_analyzer
env PATH=/home/ql/.local/java/jdk-11.0.31+11-jre/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  build-clang/bin/clang-nichecker --pipeline-profile=lazy --rounds=1 --unwind=1 -print-analysis \
  -output=/tmp/clang_nichecker_lazy_interrupt.c \
  nichecker/Cseq/examples/_mt_mytest_3.c -- -I./nichecker/Cseq/core/include
```

当前结果：

1. `lazyseq` 可收集 `main_task` 这类 `pthread_create` 入口并生成原生调度器。
2. 当前版本尚未迁移 ISR 优先级、定时器和事件约束的调度语义。
3. 当前样例 `ISR_L` 自身含有 `void` 函数中 `return NULL;` 的历史语法问题；这不是 lazyseq 改写引入的问题。

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

1. `lazyseq` 已有 C++ AST 第一版，但尚未完整覆盖 Python 版本的标签跳转、pthread runtime、元数据和中断调度语义。
2. `slice`、`label-insertion` 按当前项目约定继续调用 legacy jar，不纳入 C++ 迁移范围。
3. `instrumenter`、`replacegoto`、`mapper`、`cex` 已接入原生 C++ pass；其中完整 instrumenter 事件/定时器语义、mapper 的 cbmc-ext DIMACS 映射，以及 cex 的源码行映射与 witness 仍待迁移。
4. `CBMCDriverPass` 和 `feeder_seqprogram` 还没有补齐旧版全部参数与 counterexample 抽取逻辑。
5. `ProgramSummary` 目前采用“稳定语义摘要 + 当前 AST 绑定字段”的折中方案；如果后续要支持更细粒度的阶段性语义切换，还需要继续拆分摘要模型。

## 建议的后续推进方向

1. 继续把旧链路中的真实语义模块拆成独立 C++ pass。
2. 优先补齐 native `lazyseq` 的完整 pthread runtime、元数据和事件/定时中断调度。
3. 继续扩展 instrumenter、mapper 与 cex 的后端语义，并把当前主链路的语义差异逐项收敛到 Python 产物。

## 2026-07：lazyseq 后端阶段的原生迁移与验证

### 入口文件

1. `clang-tools-extra/clang-nichecker/lib/Passes/LazySequentializationPass.cpp`：在仍可被 Clang 解析的阶段生成 lazy 顺序化程序、调度器和线程大小元数据。
2. `clang-tools-extra/clang-nichecker/lib/Passes/InstrumenterPass.cpp`：物化 `__CSEQ_rawline`，映射 CBMC 原语，并将 lazy 控制变量降级为 `__CPROVER_bitvector[...]`。此 pass 及之后明确禁止 AST 重解析。
3. `clang-tools-extra/clang-nichecker/lib/Passes/ReplaceGotoPass.cpp`：按旧 Python `replacegoto` 的两轮算法，将 `goto __exit_loop_*` 转为 PC 约束、PC 赋值和目标标签跳转。
4. `clang-tools-extra/clang-nichecker/lib/Passes/MapperPass.cpp`：实现 mapper 默认配置下的原生跳过语义；非默认的 `cbmc-ext` DIMACS 并行映射尚待 feeder 提供符号表。
5. `clang-tools-extra/clang-nichecker/lib/Passes/CounterexamplePass.cpp`：消费 feeder 保存的 CBMC 结果，并报告原始反例日志位置。
6. `clang-tools-extra/clang-nichecker/test/replacegoto-input.c`：`replacegoto` 的最小回归输入。

### 重写与重解析边界

1. `lazyseq` 之前和 `lazyseq` 本身的输出均为标准 C，因此每次源码变化后继续通过 Clang AST 重解析。
2. `instrumenter` 会生成 `__CPROVER_bitvector[...]`。这是 CBMC 后端语法，不是 Clang C 语法；从该模块开始，驱动只传递当前源码，不再构建新 AST。
3. `replacegoto`、`mapper`、`feeder`、`cex` 均在后端文本阶段工作，不能重新引入 AST 依赖。
4. 默认 round-robin 调度会在全部 worker 轮次之后追加一个 `main_thread` 最终上下文，匹配 Python `lazyseq.__mt_scheduler()` 对 `--rounds=1` 生成 `__cs_tmp_t0_r1` 的行为，使主线程能继续完成 `pthread_join` 等尾部语句。

### 构建与回归命令

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker

# legacy slice/label-insertion jar 需要 WSL 原生 JDK 11。
# 首次使用前执行一次：
source nichecker/Cseq/java11-env.sh
java -version

# 验证 lazyseq 到 instrumenter 的后端文本转换；输出应包含
# __CS_LAZY_INSTRUMENTED 和 __CPROVER_bitvector，且不会出现 reparse failed。
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,lazyseq,instrumenter,replacegoto,mapper \
  --rounds=1 -print-analysis \
  -output=/tmp/native_lazy_after_mapper.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include

# 迁移 Python lazyseq 的显式轮次约束。首轮始终额外调度 main，
# schedule 长度超过 --rounds 时会自动扩展轮数。
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,lazyseq --rounds=1 \
  --schedule=0,1:2 \
  -output=/tmp/native_lazy_schedule.c \
  clang-tools-extra/clang-nichecker/test/lazy-duplicator-input.c --

# 迁移 Python lazyseq 的 --norobin 分支：首轮运行 main，后续由
# nondet 运行位决定是否执行线程，__cs_last_thread 禁止相邻轮次重复线程。
# 与 Python 一致，--schedule 仅可扩展轮数，不会约束该调度器的选线程集合。
# instrumenter 会为该分支的临时 PC、运行位和最后线程编号补齐 bitvector 宽度。
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,lazyseq --norobin --rounds=2 \
  -output=/tmp/native_lazy_norobin.c \
  clang-tools-extra/clang-nichecker/test/lazy-duplicator-input.c --
build-clang/bin/clang -fsyntax-only \
  -Wno-implicit-function-declaration -Wno-return-type /tmp/native_lazy_norobin.c

# 迁移 Python lazyseq 的 --contexts 分支。首个 context 固定执行 main，
# 后续 context 由 __cs_tid[] 选择线程，由 __cs_cs[] 选择该线程的目标 PC。
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,lazyseq,instrumenter \
  --contexts=3 -output=/tmp/native_lazy_contexts.c \
  clang-tools-extra/clang-nichecker/test/lazy-duplicator-input.c --
grep -nE '__cs_(tid|cs)\\[' /tmp/native_lazy_contexts.c

# 迁移 Python --nondet-condvar-wakeups：cond_wait_2 和 barrier_wait_2
# 的信号/计数检查改为可选 assume，从而允许伪唤醒。
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,condwaitconverter,lazyseq \
  --nondet-condvar-wakeups -output=/tmp/native_lazy_spurious_wakeup.c \
  clang-tools-extra/clang-nichecker/test/lazy-runtime-input.c --
grep -n '__cs_wakeup' /tmp/native_lazy_spurious_wakeup.c

# 验证 replacegoto 的 Python 等价核心规则。
build-clang/bin/clang-nichecker \
  --pipeline=replacegoto -print-analysis \
  -output=/tmp/replacegoto-output.c \
  clang-tools-extra/clang-nichecker/test/replacegoto-input.c --
grep -n '__CPROVER_assume\|__cs_pc\|goto tworker_1' /tmp/replacegoto-output.c

# 运行完整 lazy 链路。slice 和 label-insertion 仍按约定调用 legacy jar。
build-clang/bin/clang-nichecker \
  --pipeline-profile=lazy --rounds=1 -print-analysis \
  -output=/tmp/native_lazy_profile.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include

# 与 Python proSlice/labelReduc 开关一致：只有显式启用时才调用 jar。
# slice-var 与 slice-mode 对应 Python modefile 中的 globalVariable/mode。
build-clang/bin/clang-nichecker \
  --pipeline-profile=lazy --pro-slice --label-reduc \
  --slice-var=data --slice-mode=rww --rounds=1 -print-analysis \
  -output=/tmp/native_lazy_with_jar.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include

# Python mapper 等价的 DIMACS 分片。cores 必须是 2 的幂。
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,lazyseq,instrumenter,replacegoto,mapper,feeder,cex \
  --backend=cbmc-ext --contexts=1 --cores=4 --rounds=1 -print-analysis \
  -output=/tmp/native_lazy_dimacs4.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
```

### Python 与 C++ 对比调试方法

迁移任一模块时，必须比较 Python 链路和 C++ 链路在该模块结束后的源码，而不是只比较最终 CBMC 结论：

```bash
cd /home/q/code/llvm_clang_static_analyzer/nichecker/Cseq
python3 cseq.py -l lazy_until_replacegoto \
  --input examples/lazy_unsafe.c --unwind 1 --rounds 1 -D

# Python 产物位于 log/*_output__<模块名>.c，例如：
# log/_19_output__lazyseq.c、log/_20_output__instrumenter.c、
# log/_21_output__replacegoto.c。

cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,lazyseq,instrumenter,replacegoto \
  --rounds=1 -output=/tmp/native_after_replacegoto.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
diff -u nichecker/Cseq/log/_21_output__replacegoto.c \
  /tmp/native_after_replacegoto.c
```

对比时按语义检查线程入口、PC 宽度、标签/跳转、pthread 运行时和后端头文件，不要求变量命名、排版或运行时实现细节完全一致。若 Python 产物已经含 `__CPROVER_bitvector[...]`，不要把它再次交给 Clang 解析；应将其作为 instrumenter 之后的纯文本后端产物比较。

### 嵌套线程退出回归

`lazyseq` 现在会在 worker 函数的任意嵌套语句块内，把 `return` 改写为先调用
`__cs_pthread_exit()` 再返回；同时会把嵌套在 `if`、循环和复合语句中的
`pthread_create`、`pthread_join`、mutex、条件变量、屏障和线程特定数据调用改写到
native lazy runtime。这样线程特定数据的 destructor 不会因为提前返回而遗漏，嵌套
同步调用也不会漏回原始 pthread API。实现入口是
`clang-tools-extra/clang-nichecker/lib/Passes/LazySequentializationPass.cpp`，回归输入为
`clang-tools-extra/clang-nichecker/test/lazy-nested-return-input.c`。

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,lazyseq --rounds=1 \
  -output=/tmp/lazy-nested-return.c \
  clang-tools-extra/clang-nichecker/test/lazy-nested-return-input.c --
build-clang/bin/clang -fsyntax-only \
  -Wno-implicit-function-declaration -Wno-return-type /tmp/lazy-nested-return.c
grep -nE '__cs_pthread_(mutex_lock|mutex_unlock|exit)\\(' \
  /tmp/lazy-nested-return.c
```

### JDK 11 配置

legacy jar 只能使用 WSL 原生 JDK 11；Windows `java.exe` 在临时工作目录执行 `java -jar` 时会错误地报告主类不存在。当前用户级安装位置为 `/home/q/.local/java/openjdk11/usr/lib/jvm/java-11-openjdk-amd64`，入口脚本为 `nichecker/Cseq/java11-env.sh`。每个新 shell 在运行含 `slice` 或 `label-insertion` 的链路前都应执行：

```bash
cd /home/q/code/llvm_clang_static_analyzer
source nichecker/Cseq/java11-env.sh
java -version
```

已用 OpenJDK 11.0.31 验证 jar 可启动。若完整 lazy 链路仍在 `lazyseq` 提示找不到 `main`，应检查 jar 工作目录中的 `data.json`：旧 Python 链路传递的是选定的单一 `globalVariable`，当前 C++ `SlicePass` 仍传递全局变量列表，这会使 jar 产生不完整的切片结果；该问题与 JDK 配置无关。

### 当前限制

1. `lazyseq` 已迁移可重解析的调度、PC 切片和基础 pthread 运行时，但尚未等价覆盖 Python 的完整 `lazyseqB.c`、事件/定时中断和全部调度变体。
2. `instrumenter` 已覆盖 rawline 物化、CBMC 基础符号映射和 lazy 控制变量位宽；Python 中单变量访问序、事件/定时器和完整头文件拼接仍待迁移。
3. `mapper` 已支持 `--backend=cbmc-ext --contexts>0 --cores=2^n`：生成 DIMACS，按 Python `mapper.py` 的规则映射 `__cs_tid` 各 context 槽的最低命题位，并由 feeder 验证全部分片。当前分片按顺序执行，尚未迁移 Python feeder 的多进程并发调度；可用 `--reuse-dimacs` 复用同名 DIMACS 文件。
4. `cex` 已接收并报告 CBMC 原始轨迹；旧 Python 的源码行映射与 SV-COMP witness 生成尚待迁移。

### 2026-08：pthread 条件变量与屏障 runtime

`clang-tools-extra/clang-nichecker/lib/Passes/LazySequentializationPass.cpp` 现在原生注入了 Python `lazyseqB.c` 的条件变量/屏障核心状态：条件变量由地址槽和信号状态表示，`pthread_cond_wait_1` 解锁 mutex，`pthread_cond_wait_2` 假设已被 signal/broadcast 后重新上锁；屏障记录地址、初始计数和当前计数，分别由 `_wait_1` 递减、`_wait_2` 等待归零并复位。`condwaitconverter` 的 `_1/_2` 调用会在 `lazyseq` 中显式改写为这些内部 runtime 函数。

同一 runtime 还覆盖单 key 的线程特定数据模型：`pthread_key_create` 保存 destructor，`pthread_setspecific`/`pthread_getspecific` 按 `__cs_thread_index` 存取值，`pthread_self` 返回 CSeq 约定的线程编号；线程函数顶层 `return` 与显式 `pthread_exit` 均会先调用 `__cs_pthread_exit()` 执行 destructor。

回归入口为 `clang-tools-extra/clang-nichecker/test/lazy-runtime-input.c`，运行命令如下：

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,condwaitconverter,lazyseq,instrumenter \
  --rounds=1 -output=/tmp/lazy-runtime-backend.c \
  clang-tools-extra/clang-nichecker/test/lazy-runtime-input.c --
grep -n '__cs_pthread_cond_wait_1\|__cs_pthread_cond_wait_2\|__cs_pthread_cond_signal' \
  /tmp/lazy-runtime-backend.c
```

## 2026-08：lazy 前序规范化 pass

### 入口文件

1. `clang-tools-extra/clang-nichecker/lib/Passes/SpinlockPass.cpp`：迁移 Python `spinlock` 的空自旋循环处理。
2. `clang-tools-extra/clang-nichecker/lib/Passes/DoWhileConverterPass.cpp`：迁移 Python `dowhileconverter` 的 `do-while` 和 `for` 规范化。
3. `clang-tools-extra/clang-nichecker/lib/Passes/SelfOperationPass.cpp`：迁移 Python `selfop` 的自操作表达式展开。
4. `clang-tools-extra/clang-nichecker/test/lazy-normalization-input.c`：以上三个 pass 的最小回归输入。

### 当前行为

1. `spinlock` 将条件无副作用的空 `while` 循环替换为 `__VERIFIER_assume(!(条件))`，避免 lazy 顺序化保留无进展的自旋。
2. `dowhileconverter` 将 `do { ... } while (条件)` 规范化为首轮执行一次的 `for` 与后续 `while`，并将 `for` 循环改写为等价 `while`。
3. `selfop` 将独立语句中的 `++`、`--`、`+=`、`-=`、`*=`、`/=` 展开为普通赋值，减少后续文本阶段的表达式形式。

### 构建与回归命令

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker

build-clang/bin/clang-nichecker \
  --pipeline=spinlock,dowhileconverter,selfop \
  -print-analysis \
  -output=/tmp/lazy-normalization-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-normalization-input.c --

build-clang/bin/clang -fsyntax-only /tmp/lazy-normalization-native.c
```

预期分析输出包含“`spinlock 将 1 个空自旋循环改写为 assume`”、“`dowhileconverter 改写 do-while=1, for=1`”和“`selfop 展开了 5 个自操作`”。最后一项包含 `do-while` 展开后的两份循环体，因此计数为 5。

## 2026-08：lazy 等待与错误位置预处理

### 入口文件

1. `clang-tools-extra/clang-nichecker/lib/Passes/CondWaitConverterPass.cpp`：迁移 Python `condwaitconverter`，为条件变量和屏障等待增加 lazyseq 可插入调度点。
2. `clang-tools-extra/clang-nichecker/lib/Passes/PreinstrumenterPass.cpp`：迁移 Python `preinstrumenter` 的 `ERROR` 位置降级和顶层函数指针调用展开。
3. `clang-tools-extra/clang-nichecker/test/lazy-preinstrumentation-input.c`：上述两个 pass 的最小回归输入。

### 当前行为

1. `condwaitconverter` 将 `pthread_cond_wait`、`pthread_cond_timedwait` 分成 `pthread_cond_wait_1` 与 `pthread_cond_wait_2`；将 `pthread_barrier_wait` 分成对应的 `_1`、`_2` 调用。若等待调用嵌在表达式中，第一段会插入包含该表达式的语句之前。
2. `preinstrumenter` 将 `goto ERROR;` 和 `ERROR:` 所在语句降为 `__VERIFIER_error();`，并将顶层语句或赋值右侧的函数指针调用展开为按函数指针值选择的直接调用分支。

### 回归命令

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker

build-clang/bin/clang-nichecker \
  --pipeline=preinstrumenter,condwaitconverter \
  -print-analysis \
  -output=/tmp/lazy-preinstrumentation-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-preinstrumentation-input.c --

build-clang/bin/clang -fsyntax-only /tmp/lazy-preinstrumentation-native.c
```

预期输出包含 `__VERIFIER_error()`、`if (operation == increment)`、`pthread_cond_wait_1`、`pthread_cond_wait_2`、`pthread_barrier_wait_1` 与 `pthread_barrier_wait_2`。

## 2026-08：lazy 线程入口复制

### 入口文件

1. `clang-tools-extra/clang-nichecker/lib/Passes/DuplicatorPass.cpp`：迁移 Python `duplicator`，将每个静态 `pthread_create` 入口对应到一个独立函数副本。
2. `clang-tools-extra/clang-nichecker/test/lazy-duplicator-input.c`：验证同一函数被两次创建并同时被普通调用时的复制行为。

### 当前行为与命令

`duplicator` 会将同一入口函数依创建顺序复制为 `worker_0`、`worker_1` 等，并改写每个 `pthread_create` 的第三参数。若原函数仍存在普通调用，则保留原定义；函数原型也会同步复制。`--threads=N` 对应 Python 模块的 `--threads` 上界，`0` 表示不限制。

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker

build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator \
  -print-analysis \
  -output=/tmp/lazy-duplicator-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-duplicator-input.c --

build-clang/bin/clang -fsyntax-only /tmp/lazy-duplicator-native.c
```

预期产物包含 `worker_0`、`worker_1`、两个改写后的 `pthread_create` 调用，以及因普通调用而保留的 `worker` 原定义。

`lazyseq` 会在 duplicator 重解析后直接扫描当前 AST 中的 `pthread_create` 参数来建立线程计划，而不再使用重写前的函数名摘要。可用下列命令验证两个复制入口都会被调度：

```bash
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,lazyseq --rounds=1 \
  -print-analysis \
  -output=/tmp/lazy-duplicator-lazyseq-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-duplicator-input.c --

build-clang/bin/clang -fsyntax-only /tmp/lazy-duplicator-lazyseq-native.c
```

预期分析含“`native lazyseq 重写了 3 个线程函数`”；最后的语法检查目前会给出顺序化线程函数受 PC 守卫影响的非 void 返回路径警告，但不会失败。

## 2026-08：函数归属追踪

入口文件为 `clang-tools-extra/clang-nichecker/lib/Passes/FunctionTrackerPass.cpp`。该 pass 迁移 Python `functiontracker` 的源码行到函数名映射，并在顶层函数调用中把 `p++`、`p--` 实参改为先传递旧值、再执行自操作，避免后续源码变换丢失调用参数的求值顺序。

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker

build-clang/bin/clang-nichecker \
  --pipeline=functiontracker -print-analysis \
  -output=/tmp/lazy-functiontracker-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-functiontracker-input.c --

build-clang/bin/clang -fsyntax-only /tmp/lazy-functiontracker-native.c
```

预期产物包含 `consume(first, second); first++; second--;`，分析信息会报告映射的源码行数和拆分数量。

## 2026-08：switch 控制流转换

入口文件为 `clang-tools-extra/clang-nichecker/lib/Passes/SwitchTransformerPass.cpp`，回归输入为 `clang-tools-extra/clang-nichecker/test/lazy-switchtransformer-input.c`。该 pass 缓存 switch 条件，生成 case/default 条件分支、case 标签和落空跳转；仅 case/default 直接包含的 `break` 会跳到 switch 出口，循环内部的 `break` 保持原样。

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline=switchtransformer -print-analysis \
  -output=/tmp/lazy-switchtransformer-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-switchtransformer-input.c --
build-clang/bin/clang -fsyntax-only /tmp/lazy-switchtransformer-native.c
```

嵌套 switch 当前保留在外层生成代码中，递归转换将在后续迭代补齐。

## 2026-08：嵌套调用预处理

入口文件为 `clang-tools-extra/clang-nichecker/lib/Passes/PreInlinerPass.cpp`，回归输入为 `clang-tools-extra/clang-nichecker/test/lazy-preinliner-input.c`。该 pass 迁移 Python `preinliner` 的核心语义：将有定义且非 `void` 的嵌套调用提升为同一 compound 语句前的临时变量赋值。

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker --pipeline=preinliner \
  -output=/tmp/lazy-preinliner-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-preinliner-input.c --
build-clang/bin/clang -fsyntax-only /tmp/lazy-preinliner-native.c
```

预期产物为 `int __cs_preinliner_0 = twice(2); return add(__cs_preinliner_0, 1);`。

## 2026-08：函数实例内联

入口文件为 `clang-tools-extra/clang-nichecker/lib/Passes/InlinerPass.cpp`，回归输入为 `clang-tools-extra/clang-nichecker/test/lazy-inliner-input.c`。该 pass 消费 preinliner 的扁平调用，在调用点建立参数副本、`__cs_retval_<函数>_<序号>`、`__exit_<函数>_<序号>`，并将被内联函数的 return 变为赋值后跳至实例出口。原函数内的标签与 goto 会统一加上实例后缀，避免同一函数多次内联后产生重复标签。当前保留原函数定义，保证函数指针和递归引用不被误删。

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker --pipeline=preinliner,inliner \
  -output=/tmp/lazy-inliner-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-inliner-input.c --
build-clang/bin/clang -fsyntax-only /tmp/lazy-inliner-native.c
```

## 2026-08：前置兼容处理

入口文件为 `clang-tools-extra/clang-nichecker/lib/Passes/WorkaroundsPass.cpp`，回归输入为 `clang-tools-extra/clang-nichecker/test/lazy-workarounds-input.c`。当前已迁移 Python `workarounds` 的恒假 `if (0)`/`if (!1)` 删除、`pthread_create` 启动函数 cast 去除、`pointer->field` 到 `(*pointer).field` 的降级、`auto/inline/extern/volatile/register` 声明前缀移除，以及当前 compound 中无对应 begin 的 `__VERIFIER_atomic_end()` 到 `__CSEQ_noop()` 的降级。线程局部变量数组化、匿名结构命名、复合声明拆分与所有初始化器兼容规则仍将继续补齐。

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker --pipeline=workarounds \
  -output=/tmp/lazy-workarounds-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-workarounds-input.c --
build-clang/bin/clang -fsyntax-only /tmp/lazy-workarounds-native.c
```

## 2026-08：整数常量折叠

入口文件为 `clang-tools-extra/clang-nichecker/lib/Passes/ConstantsPass.cpp`，回归输入为 `clang-tools-extra/clang-nichecker/test/lazy-constants-input.c`。该 pass 迁移 Python `constants` 的默认整数二元表达式折叠：递归处理 `+`、`-`、`*` 和可整除的 `/`，不可整除除法保持原表达式。

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker --pipeline=constants \
  -output=/tmp/lazy-constants-native.c \
  clang-tools-extra/clang-nichecker/test/lazy-constants-input.c --
build-clang/bin/clang -fsyntax-only /tmp/lazy-constants-native.c
```

预期产物包含 `int first = 10;`、`int second = 4;` 和未折叠的 `int third = 7 / 2;`。

## 2026-08：中断入口与随机优先级

中断输入不再跳过 `conditionextractor` 和 `varnames`。入口分别为 `clang-tools-extra/clang-nichecker/lib/Passes/ConditionExtractionPass.cpp` 与 `clang-tools-extra/clang-nichecker/lib/Passes/VariableRenamingPass.cpp`；回归输入为 `clang-tools-extra/clang-nichecker/test/lazy-interrupt-normalization-input.c`。

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,interrupt-lowering,conditionextractor,varnames \
  -output=/tmp/lazy-interrupt-normalization.c \
  clang-tools-extra/clang-nichecker/test/lazy-interrupt-normalization-input.c -- -w
build-clang/bin/clang -fsyntax-only /tmp/lazy-interrupt-normalization.c
```

预期产物包含 `__cs_tmp_if_cond_` 条件临时变量，以及 `__cs_local_main_task_local_value` 和 `__cs_local_interrupt_handler_local_value` 这类函数作用域局部变量名。

入口文件：

1. `clang-tools-extra/clang-nichecker/lib/Passes/InterruptLoweringPass.cpp`：将原始 `main` 和 ISR 定义降级为 pthread 入口，并从当前 AST 的 `pthread_create` 原型推导线程句柄类型；没有原型的旧样例保持 `unsigned` 回退。
2. `clang-tools-extra/clang-nichecker/lib/Analysis/ProgramAnalyzer.cpp`：读取紧邻 ISR 定义的 `// priority N` 注释，稳定保存为 `ProgramSummary.InterruptInfos`，不受后续重解析和 duplicator 重命名影响。
3. `clang-tools-extra/clang-nichecker/lib/Passes/LazySequentializationPass.cpp`：迁移 Python `lazyseq.getInputList()` 的随机优先级完成状态门控。
4. `clang-tools-extra/clang-nichecker/test/lazy-interrupt-priority-input.c`：随机优先级 ISR 回归输入。
5. `clang-tools-extra/clang-nichecker/test/lazy-interrupt-mask-input.c`：ISR 屏蔽和恢复回归输入。

运行命令：

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline-profile=lazy --rounds=1 -print-analysis \
  -output=/tmp/lazy-interrupt-priority.c \
  clang-tools-extra/clang-nichecker/test/lazy-interrupt-priority-input.c -- -w
grep -n -E 'interrupt_(low|high)_0|__cs_active_thread\[[12]\]' \
  /tmp/lazy-interrupt-priority.c

build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,interrupt-lowering,duplicator,lazyseq,instrumenter \
  --rounds=1 -output=/tmp/lazy-interrupt-mask.c \
  clang-tools-extra/clang-nichecker/test/lazy-interrupt-mask-input.c -- -w
grep -n '__cs_disable_thread' /tmp/lazy-interrupt-mask.c
```

预期 `interrupt_high_0` 仅在自身 PC 为 0 时启动，且没有优先级等待条件；首轮的 `main_task_0` 不应用完成门控。第二个回归产物应包含 `__cs_disable_thread` 数组、屏蔽/恢复赋值以及每个 scheduler 分支上的屏蔽检查。此行为对应旧 Python 随机 ISR 分支：最高优先级 ISR 可立即抢占，低优先级随机 ISR 只受非最高优先级候选的完成状态约束。

Python/C++ 分段对比继续使用本 README 前文的 `lazy_until_replacegoto` 命令：Python 的模块产物位于 `nichecker/Cseq/log/*_output__<模块>.c`，C++ 通过 `--pipeline=<截至模块>` 和 `-output=/tmp/<模块>.c` 输出对应阶段源码。对 ISR 优先级输入，应首先比较 `lazyseq` 后调度器中每个 `__cs_active_thread` 分支的 PC 完成门控。

## 2026-08：dictfile 的原生 JSON 替代

旧 Python GUI 将 ISR 元数据以 pickle 写入 `modules/dictfile`。C++ 不读取该 Python 私有二进制格式，改由 `--isr-config=<文件>` 接收 JSON；入口在 `clang-tools-extra/clang-nichecker/ClangNIChecker.cpp`，加载与校验位于 `clang-tools-extra/clang-nichecker/lib/Analysis/ProgramAnalyzer.cpp`。每个键是函数名，支持与 GUI 一致的 `kind`、`prio`、`t`、`event` 和 `constraint` 字段。配置可以覆盖源代码相邻 `// priority N` 注释，也可以声明不符合 ISR 命名约定的函数。

运行命令：

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline-profile=lazy \
  --isr-config=clang-tools-extra/clang-nichecker/test/lazy-interrupt-config.json \
  --rounds=1 -output=/tmp/lazy-interrupt-config.c \
  clang-tools-extra/clang-nichecker/test/lazy-interrupt-priority-input.c -- -w
grep -n -E 'interrupt_(low|high)_0|__cs_pc\[[23]\] != 0' \
  /tmp/lazy-interrupt-config.c
```

该回归 JSON 将源注释中的优先级反转，预期 `interrupt_low_0` 而非 `interrupt_high_0` 带有仅首次启动的 PC 条件，证明 lazyseq 使用了原生配置而非注释默认值。

如果 JSON 声明的函数不匹配 `ISR_`、`isr_` 或 `interrupt` 命名约定，配置本身仍会将程序分类为 interrupt-driven：

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline-profile=lazy \
  --isr-config=clang-tools-extra/clang-nichecker/test/lazy-configured-handler.json \
  --rounds=1 -print-analysis -output=/tmp/lazy-configured-handler.c \
  clang-tools-extra/clang-nichecker/test/lazy-configured-handler-input.c -- -w
grep -n -E 'void \*deferred_handler_0|__CS_PTHREAD_CREATE.*deferred_handler_0' \
  /tmp/lazy-configured-handler.c
```

预期分析输出为 `kind: interrupt-driven`，产物包含 `deferred_handler_0`，而不是把该输入当作顺序程序跳过 lazyseq。

### event 触发回归

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,interrupt-lowering,duplicator,lazyseq,instrumenter \
  --isr-config=clang-tools-extra/clang-nichecker/test/lazy-interrupt-event-config.json \
  --rounds=1 -output=/tmp/lazy-interrupt-event.c \
  clang-tools-extra/clang-nichecker/test/lazy-interrupt-priority-input.c -- -w
grep -n -E '__cs_active_thread\[3\] = [01]|__cs_pc_cs\[1\]' \
  /tmp/lazy-interrupt-event.c
```

`LazySequentializationPass.cpp` 会把 event ISR 的 wrapper 创建改为未激活状态；当 `main_task_0` 处理到 JSON 中 `event` 指定的赋值时，它激活对应线程、清零其 PC 并提前返回当前主任务上下文。event 调度只等待同一 event 副本或 timer，不会被无关随机 ISR 阻塞。

### timer 周期回归

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,interrupt-lowering,duplicator,lazyseq,instrumenter \
  --isr-config=clang-tools-extra/clang-nichecker/test/lazy-interrupt-timer-config.json \
  --rounds=1 -output=/tmp/lazy-interrupt-timer.c \
  clang-tools-extra/clang-nichecker/test/lazy-interrupt-priority-input.c -- -w
grep -n -E '__cs_timer_counter\[2\]|__cs_active_thread\[2\] = [01]' \
  /tmp/lazy-interrupt-timer.c
```

timer ISR 同样延后到主任务中激活。每个主任务赋值使对应 `__cs_timer_counter` 加一；计数到达 `t + constraint` 时，runtime 重新设置计数器、激活 timer 线程、清零其 PC 并让出当前上下文。timer 多实例、循环体内赋值和 `constraint>0` 的 signed nondet 初始相位均已迁移；event 最大间隔和 context-bounded 的完整时间分支仍待继续收敛。

### timer 多实例回归

入口实现为 `clang-tools-extra/clang-nichecker/lib/Passes/InterruptLoweringPass.cpp` 和 `clang-tools-extra/clang-nichecker/lib/Passes/LazySequentializationPass.cpp`；回归输入为 `clang-tools-extra/clang-nichecker/test/lazy-interrupt-timer-multi-input.c`，JSON 配置为 `clang-tools-extra/clang-nichecker/test/lazy-interrupt-timer-multi-config.json`。它对应 Python `nichecker.py` 的实例数公式：`unwind * while(1) 循环体中的分号数 / t`。

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,interrupt-lowering,duplicator,lazyseq,instrumenter \
  --isr-config=clang-tools-extra/clang-nichecker/test/lazy-interrupt-timer-multi-config.json \
  --unwind=2 --rounds=1 -output=/tmp/lazy-interrupt-timer-multi.c \
  clang-tools-extra/clang-nichecker/test/lazy-interrupt-timer-multi-input.c -- -w
grep -n -E 'interrupt_timer_[01]|__cs_timer_counter\[1\]|__cs_active_thread\[[12]\]' \
  /tmp/lazy-interrupt-timer-multi.c
```

预期 wrapper 中存在两次 `pthread_create(... interrupt_timer, ...)`，duplicator 后为 `interrupt_timer_0` 和 `interrupt_timer_1`。主任务循环体的每个计时点只递增 `__cs_timer_counter[2]`；到期后按顺序激活第一个未激活副本，并在其已激活时继续尝试下一个副本。`constraint=0` 时从零相位开始，timer 的触发阈值为 `t + constraint`。

### timer 随机相位回归

入口仍为 `clang-tools-extra/clang-nichecker/lib/Passes/LazySequentializationPass.cpp`，配置文件为 `clang-tools-extra/clang-nichecker/test/lazy-interrupt-timer-jitter-config.json`。该实现对应 Python `nichecker/Cseq/pycparser/newParser/c_generator.py` 的 `visit_mainTaskBody()`：每个 timer 函数族在首次调度和每次到期后以 `nondet_int()` 选择 `[0, 2 * constraint]` 内的计数器初相位，并使用等号判断 `counter == t + constraint`。

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,interrupt-lowering,duplicator,lazyseq,instrumenter \
  --isr-config=clang-tools-extra/clang-nichecker/test/lazy-interrupt-timer-jitter-config.json \
  --unwind=2 --rounds=1 -output=/tmp/lazy-interrupt-timer-jitter.c \
  clang-tools-extra/clang-nichecker/test/lazy-interrupt-timer-multi-input.c -- -w
grep -n -E 'nondet_int|__cs_timer_counter\[2\].*== 2|timer_counter\[2\].*>= 0.*<= 2' \
  /tmp/lazy-interrupt-timer-jitter.c
```

预期产物含有 `nondet_int()` 和 `__VERIFIER_assume(__cs_timer_counter[2] >= 0 && __cs_timer_counter[2] <= 2)`，并在主任务计时点使用 `== 2` 触发 timer。`constraint=0` 的旧配置不引入非确定性相位，保持计数器从零开始的既有行为。

## 2026-08：instrumenter 的 CBMC 原语与声明迁移

入口文件为 `clang-tools-extra/clang-nichecker/lib/Passes/InstrumenterPass.cpp`，回归输入为 `clang-tools-extra/clang-nichecker/test/lazy-instrumenter-primitives-input.c`。该部分迁移 Python `nichecker/Cseq/modules/instrumenter.py` 的 CBMC 原语映射和 `modules/cbmc_extra.c` 声明注入：`__VERIFIER_assume`、`__VERIFIER_assertext`、`__VERIFIER_assert` 与五个 `__VERIFIER_nondet_*` 名称会在 `cbmc`、`cbmc-ext`、`cbmc-5.10`、`cbmc-svcomp2020` 后端改写为对应的后端原语。最终文件还会在 lazyseq runtime 前注入带 include guard 的 `nondet_*` 函数声明。

该转换发生在最后一次 AST 重解析之后，生成 `__CPROVER_bitvector[...]` 后不会再由 Clang 解析；后续 `replacegoto`、`mapper`、`feeder` 和 `cex` 只消费后端文本。

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,lazyseq,instrumenter,replacegoto \
  --backend=cbmc -output=/tmp/lazy-instrumenter-primitives.c \
  clang-tools-extra/clang-nichecker/test/lazy-instrumenter-primitives-input.c -- -w
grep -n -E '__CS_CBMC_EXTRA_DECLS|nondet_int\(\)|__VERIFIER_(assume|nondet)|__CPROVER_assume' \
  /tmp/lazy-instrumenter-primitives.c
```

预期产物包含 `__CS_CBMC_EXTRA_DECLS` 和 `nondet_int()`，不包含输入源码中的 `__VERIFIER_assume` 或 `__VERIFIER_nondet_int` 调用；`__CPROVER_assume` 保留为 CBMC 后端原语。

## 2026-08：lazyseq 可见访问点切分

入口文件为 `clang-tools-extra/clang-nichecker/lib/Passes/LazySequentializationPass.cpp`。Python `lazyseq` 并非为每条顶层语句建立恢复点：它只在全局存储访问和同步调用等可见共享访问点插入 `IF(thread, pc, label)`。C++ 的 `VisibleLazyStatementCollector` 现在采用相同粒度：不含共享访问的连续语句会保留在相邻恢复点之间；`main_task_0` 的赋值仍单独作为恢复点，确保 event/timer 激活写入的 continuation PC 可以恢复。

以下命令同时生成 Python 和 C++ 的 lazyseq 阶段产物，用于验证迁移是否正确：

```bash
cd /home/q/code/llvm_clang_static_analyzer/nichecker/Cseq
python3 cseq.py -l lazy_until_replacegoto \
  --input examples/lazy_unsafe.c --unwind 1 --rounds 1 -D

cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,lazyseq --rounds=1 \
  -output=/tmp/native-lazyseq-visible.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
build-clang/bin/clang -fsyntax-only \
  -Wno-implicit-function-declaration -Wno-return-type \
  /tmp/native-lazyseq-visible.c
grep -n '__cs_thread_lines' nichecker/Cseq/log/_19_output__lazyseq.c \
  /tmp/native-lazyseq-visible.c
```

`lazy_unsafe.c` 的 Python 与 C++ 产物均应给出线程行数 `{1, 3, 3, 3}`：main 只有 mutex 初始化这个可见恢复点，三个 worker 分别是 lock、共享变量访问、unlock 三个恢复点。Python 的 `_19_output__lazyseq.c` 含有后续 instrumenter 才会使用的 `__CPROVER_bitvector[...]` 语法；C++ 对照应停在 `lazyseq`，此时仍是可由 Clang 重解析的标准 C。完整 profile 在 instrumenter 后进入后端文本阶段，不应再以 Clang 语法检查。

## 2026-08：modefile 的原生 JSON 替代

入口文件为 `clang-tools-extra/clang-nichecker/ClangNIChecker.cpp`，配置载体为 `PipelineOptions`（`clang-tools-extra/clang-nichecker/include/clang-nichecker/Support/Types.h`）。Python GUI/命令行原先以 pickle 写入 `nichecker/Cseq/modules/modefile`，字段为 `mode`、`type`、`globalVariable` 和 `isMt`。C++ 不读取该 pickle，改用 `--svp-config=<JSON>` 或显式 `--svp-mode`、`--svp-type`、`--svp-var`；显式参数优先于 JSON。

JSON 的 `mode` 只接受 `rww`、`wwr`、`rwr`、`wrw`，选择任意模式时必须同时给出目标变量。配置中的 `mode` 与 `globalVariable` 会自动传给已保留的 `slice`/`label-insertion` jar 协议，除非另行使用 `--slice-mode` 或 `--slice-var` 覆盖。`type` 与完整单变量访问序插桩将由后续 native instrumenter 消费。

回归配置为 `clang-tools-extra/clang-nichecker/test/lazy-svp-config.json`，其等价于旧 `modefile` 的 `rww/int/state` 配置：

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,slice --pro-slice \
  --svp-config=clang-tools-extra/clang-nichecker/test/lazy-svp-config.json \
  -print-analysis -output=/tmp/lazy-svp-config.c \
  clang-tools-extra/clang-nichecker/test/lazy-interrupt-priority-input.c -- -w
```

预期分析信息包含“`slice 已通过 legacy jar 生成输出`”。对应临时目录 `clang-nichecker-legacy-*/data.json` 中会有 `"mode": "rww"` 与 `"var": "state"`，证明 JSON 已进入原 jar 协议而未读取 Python pickle。

### rwr 标量访问序

入口文件为 `clang-tools-extra/clang-nichecker/lib/Passes/InstrumenterPass.cpp`。当 JSON 或 `--svp-mode=rwr` 选择命名标量全局变量时，instrumenter 在最后一次 AST 重解析之后插入 Python `deal_with_svp()` 的 rwr 核心状态机：读取目标变量后保存该线程快照；同线程连续读会断言快照相等；直接赋值或自增写入会清空该线程的读取快照。该阶段随后才降级为 CBMC 后端文本，因此不会要求 Clang 再次解析 bitvector 语法。

回归输入为 `clang-tools-extra/clang-nichecker/test/lazy-svp-rwr-input.c`，配置为 `clang-tools-extra/clang-nichecker/test/lazy-svp-rwr-config.json`：

```bash
cd /home/q/code/llvm_clang_static_analyzer
ninja -C build-clang -j1 clang-nichecker
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,lazyseq,instrumenter \
  --svp-config=clang-tools-extra/clang-nichecker/test/lazy-svp-rwr-config.json \
  --rounds=1 -print-analysis -output=/tmp/lazy-svp-rwr.c \
  clang-tools-extra/clang-nichecker/test/lazy-svp-rwr-input.c -- -w
grep -n -E '__cs_svp_rwr_(read|last|seen)|assert\(__cs_svp_rwr_read' \
  /tmp/lazy-svp-rwr.c
```

预期产物含有 `__cs_svp_rwr_last`、`__cs_svp_rwr_seen`、读取快照与断言，以及每次 `state` 直接写入后的 `seen` 清空。数组下标、指针别名、复杂表达式以及 `rww`、`wwr`、`wrw` 仍在继续迁移，不能把该标量回归视为完整 SVP 覆盖。

同一输入也覆盖 `rww` 的读后快照、写前比较分支：

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,lazyseq,instrumenter \
  --svp-mode=rww --svp-type=int --svp-var=state --rounds=1 \
  -output=/tmp/lazy-svp-rww.c \
  clang-tools-extra/clang-nichecker/test/lazy-svp-rwr-input.c -- -w
grep -n -E '__cs_svp_rww_(read|last|seen)|assert\(state == __cs_svp_rww' \
  /tmp/lazy-svp-rww.c
```

该标量 rww 实现对应 Python 在读取后保存 `rww_r_tmp*`、在后续直接写入之前比较保存值的规则。

同一输入还覆盖 `wwr` 的写后快照、读后比较分支：

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,duplicator,lazyseq,instrumenter \
  --svp-mode=wwr --svp-type=int --svp-var=state --rounds=1 \
  -output=/tmp/lazy-svp-wwr.c \
  clang-tools-extra/clang-nichecker/test/lazy-svp-rwr-input.c -- -w
grep -n -E '__cs_svp_wwr_(write|read|last|seen)|assert\(__cs_svp_wwr_read' \
  /tmp/lazy-svp-wwr.c
```

该标量 wwr 实现对应 Python 在写入后保存 `wwr_w_tmp*`、在后续读取时比较保存值的规则。`wrw` 及数组/指针仍未完成。

### wrw 优先级访问序

入口文件仍为 `clang-tools-extra/clang-nichecker/lib/Passes/InstrumenterPass.cpp`。`wrw` 使用 lazyseq 线程函数的 `__CS_LAZY_IF` 索引和 `ProgramSummary.InterruptInfos` 中稳定保存的优先级：非主任务的读取会标记所有严格低优先级线程；非最高优先级线程写入后按写入次数检查自己的标记为零，再清空标记。该逻辑对应 Python 的 `wrw_a_*` 与 `wrw_a_*_num` 状态。

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,interrupt-lowering,duplicator,lazyseq,instrumenter \
  --svp-config=clang-tools-extra/clang-nichecker/test/lazy-svp-wrw-config.json \
  --rounds=1 -output=/tmp/lazy-svp-wrw.c \
  clang-tools-extra/clang-nichecker/test/lazy-svp-wrw-input.c -- -w
grep -n -E '__cs_svp_wrw_(mark|writes)|assert\(__cs_svp_wrw_mark' \
  /tmp/lazy-svp-wrw.c
```

预期高优先级 ISR 的读取写入 `mark[低优先级线程索引]`，低优先级 ISR 的第二次写入前包含 `assert(mark[...] == 0)`。数组下标、指针别名和复杂表达式仍未覆盖。

### 随机 ISR 时间约束回归

```bash
cd /home/q/code/llvm_clang_static_analyzer
build-clang/bin/clang-nichecker \
  --pipeline=program-classifier,interrupt-lowering,duplicator,lazyseq,instrumenter \
  --isr-config=clang-tools-extra/clang-nichecker/test/lazy-interrupt-constraint-config.json \
  --rounds=2 -output=/tmp/lazy-interrupt-constraint.c \
  clang-tools-extra/clang-nichecker/test/lazy-interrupt-priority-input.c -- -w
grep -n -E '__cs_counter|__cs_counter_before' /tmp/lazy-interrupt-constraint.c
```

配置中任一 random ISR 的非零 `constraint` 会启用全局计数器；`main_task_0` 的赋值递增计数，每轮 scheduler 结束记录 `__cs_counter_before`。从第二轮开始，每个随机 ISR 必须满足 `__cs_counter - __cs_counter_before >= constraint` 才能运行。该部分对应 Python lazyseq 的随机 ISR 最小间隔规则；event 最大间隔和 context-bounded 分支仍待收敛。
