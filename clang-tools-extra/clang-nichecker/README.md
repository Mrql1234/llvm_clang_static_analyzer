# clang-nichecker

## 目标

`clang-nichecker` 是对 `nichecker/Cseq` 源码到源码迁移链路的 C++ 重构版本，落地在 `clang-tools-extra` 下，基于 Clang LibTooling 和 AST 实现。

最终目标不是做单点诊断，而是完成下面这条完整流程：

1. 读取用户输入的中断程序或多线程程序
2. 经过多个源码级模块做解析、归一化和变换
3. 生成顺序程序
4. 调用 CBMC
5. 输出验证结果

## 当前阶段

当前实现已经完成两步基础工作：

1. Phase 1：AST 解析与程序分类
   - 识别顺序程序、多线程程序、中断驱动程序
   - 收集 `pthread_create` 线程入口和 `ISR_*` 中断入口
2. Phase 2：中断程序归一化
   - 对中断驱动输入，把 `main` 改写成 `main_task`
   - 生成一个新的 wrapper `main`
   - 让输出源码先落到“有界多线程风格”的中间形态
3. Phase 4：作用域变量改名
   - 对顺序/多线程输入，基于 AST 收集函数参数和局部变量
   - 输出 `__cs_param_<func>_<name>` / `__cs_local_<func>_<name>` 风格的新名字
   - 先避免不同函数里的同名局部变量互相冲突，给后续顺序化打基础
4. Phase 4：第一版条件抽取
   - 对顺序/多线程输入，把部分 `if (cond)` 改写成“先计算临时变量，再进入 `if`”
   - 当前先覆盖不依赖局部变量/函数参数的 `if` 条件
   - 输出 `__cs_tmp_if_cond_<n>` 风格的临时条件变量

当前还没有完成真正的顺序化和 CBMC 联调，因此默认输出文件虽然仍使用 `.seq.c` 扩展名，但对于中断输入，现阶段含义更接近“归一化后的中间源码”。

## 入口文件

- 工具主入口：`/home/ql/code/llvm_clang_static_analyzer/clang-tools-extra/clang-nichecker/ClangNIChecker.cpp`
- Frontend/Driver 入口：`/home/ql/code/llvm_clang_static_analyzer/clang-tools-extra/clang-nichecker/lib/Driver/Frontend.cpp`
- 模块链装配入口：`/home/ql/code/llvm_clang_static_analyzer/clang-tools-extra/clang-nichecker/lib/Driver/PipelineBuilder.cpp`

## 结构约束

当前 `ClangNIChecker.cpp` 里的实现是迁移早期原型，不代表最终代码组织方式。

后续新增功能必须遵循下面约束：

1. 不再把新分析器、pass、源码改写工具、CBMC 驱动继续堆进同一个类或同一个源文件
2. `Driver` 只负责参数解析、模块链装配和执行调度，不直接承载具体变换逻辑
3. 一个 pass 一个独立类，后续应逐步拆到独立文件，便于配置模块链
4. 公共上下文、源码替换、AST 辅助函数应放到独立 `support` 层

目标层次应至少包括：

1. `Driver`
2. `Analysis`
3. `Passes`
4. `Support`
5. `Backend`

目标是让后续模块能够按配置组成模块链，而不是依赖一个不断膨胀的入口文件。

## 当前目录结构

当前代码已经拆到下面几层：

```text
clang-tools-extra/clang-nichecker/
  ClangNIChecker.cpp
  README.md
  include/clang-nichecker/
    Analysis/
    Backend/
    Driver/
    Passes/
    Support/
  lib/
    Analysis/
    Backend/
    Driver/
    Passes/
    Support/
```

## 当前模块链

默认模块链：

1. `program-classifier`
2. `interrupt-lowering`
3. `condition-extraction`
4. `variable-renaming`
5. `label-insertion`
6. `loop-unroll`
7. `sequentialization`
8. `source-emission`

说明：

1. `program-classifier`、`interrupt-lowering`、`condition-extraction`、`variable-renaming` 已有第一版实现
2. `label-insertion`、`loop-unroll`、`sequentialization` 当前已拆成独立模块骨架，后续在各自文件内继续补实现
3. `cbmc-driver` 已拆成独立 backend 模块骨架，需要显式启用

## 构建命令

```bash
cd /home/ql/code/llvm_clang_static_analyzer
cmake --build build-csa --target clang-nichecker -j 4
```

## 运行命令

### 1. 分析多线程样例

```bash
cd /home/ql/code/llvm_clang_static_analyzer
build-csa/bin/clang-nichecker \
  -print-analysis \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
```

### 2. 分析并归一化中断样例

```bash
cd /home/ql/code/llvm_clang_static_analyzer
build-csa/bin/clang-nichecker \
  -print-analysis \
  nichecker/Cseq/examples/mytest_3.c -- -I./nichecker/Cseq/core/include
```

### 3. 指定输出路径

```bash
cd /home/ql/code/llvm_clang_static_analyzer
build-csa/bin/clang-nichecker \
  -output=/tmp/mytest_3.seq.c \
  nichecker/Cseq/examples/mytest_3.c -- -I./nichecker/Cseq/core/include
```

### 4. 查看变量改名结果

```bash
cd /home/ql/code/llvm_clang_static_analyzer
build-csa/bin/clang-nichecker \
  -output=/tmp/lazy_unsafe.seq.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
```

### 5. 查看条件抽取结果

```bash
cd /home/ql/code/llvm_clang_static_analyzer
build-csa/bin/clang-nichecker \
  -print-analysis \
  -output=/tmp/lazy_unsafe.seq.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
```

### 6. 指定自定义模块链

```bash
cd /home/ql/code/llvm_clang_static_analyzer
build-csa/bin/clang-nichecker \
  -pipeline=program-classifier,variable-renaming,source-emission \
  -print-analysis \
  -output=/tmp/lazy_unsafe.custom.seq.c \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
```

### 7. 启用 CBMC backend 骨架

```bash
cd /home/ql/code/llvm_clang_static_analyzer
build-csa/bin/clang-nichecker \
  -enable-cbmc \
  -print-analysis \
  nichecker/Cseq/examples/lazy_unsafe.c -- -I./nichecker/Cseq/core/include
```

## 当前输出特征

对于中断输入，当前输出会保留原始全局变量和中断函数，并把原始 `main` 改写成类似下面的结构：

```c
void *main_task(void *__cs_param_main_task_arg) {
  /* 原 main 函数体 */
}

int main() {
  pthread_t __cs_local_main_t0;
  pthread_create(&__cs_local_main_t0, 0, main_task, 0);
  return 0;
}
```

这是为了先把“中断驱动程序”收敛到后续顺序化更容易处理的统一入口形态。

对于顺序/多线程输入，当前还会对函数参数和局部变量做第一版作用域改名。例如：

```c
void *thread1(void *__cs_param_thread1_arg) {
  ...
}

int main() {
  pthread_t __cs_local_main_t1;
  ...
}
```

这一版改名逻辑当前只覆盖函数参数、局部变量声明和 `DeclRefExpr` 引用，还没有扩展到更复杂的源码重写场景。

对于顺序/多线程输入，当前还支持第一版 `if` 条件抽取。例如：

```c
_Bool __cs_tmp_if_cond_0 = (data >= 3);
if (__cs_tmp_if_cond_0) {
  ...
}
```

当前限制：

1. 只处理 `if`，还没有覆盖 `while` 和 `for`
2. 只处理不依赖局部变量/函数参数的条件，避免和当前变量改名规则冲突
3. 中断输入暂时跳过条件抽取，后续需要在“降级后重新建 AST”这一步补齐

## 后续阶段

后续按下面顺序推进：

1. 补齐输入兼容层，减少对临时 `-include` 参数的依赖
2. 在已经拆开的 `Analysis / Passes / Support / Backend` 层次上继续扩展模块链
3. 在现有变量改名和第一版 `if` 条件抽取基础上，实现 `while/for` 条件抽取、标签插入、循环展开等归一化 pass
4. 实现多线程到顺序程序的核心顺序化逻辑
5. 接入 CBMC 驱动与回归验证
