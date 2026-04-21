# Feature Specification: C++ 缺陷检测器 Phase 2（剩余 10 种缺陷）

**Workspace**: `csa-phase2-defects`
**Created**: 2026-03-17
**Status**: Draft
**Input**: 用户描述: "继续完成 17 种 C++ 常见运行时缺陷中剩余 10 种的检测能力。MVP（7 种）和增强（3 种可能缺陷检测）已完成，现在完成其余全部缺陷。"

---

## 背景

17 种缺陷整体进度：

| # | 缺陷类型 | Phase 1 MVP | 增强 | Phase 2 |
|---|---------|-------------|------|---------|
| 1 | 数组越界 | ✅ 已完成 | ✅ PossibleArrayBound | |
| 2 | 除以零 | ✅ 已完成 | ✅ PossibleDivideZero | |
| 3 | 死循环 | | | ✅ 本次 |
| 4 | 缓冲区读写越界（strcpy/strcat） | | | ✅ 本次 |
| 5 | 递归栈溢出 | | | ✅ 本次 |
| 6 | 指针使用前非空判断 | ✅ 已完成 | | |
| 7 | 变量使用前未初始化 | | | ✅ 本次 |
| 8 | 浮点数等号比较 | ✅ 已完成 | | |
| 9 | 无符号数负值 | | | ✅ 本次 |
| 10 | 反三角函数参数范围 | ✅ 已完成 | ✅ MathDomainGuard | |
| 11 | 浮点数转整数溢出 | | | ✅ 本次 |
| 12 | 函数内大局部变量栈溢出 | ✅ 已完成 | | |
| 13 | memcpy 目标空间不足 | | | ✅ 本次 |
| 14 | memset 目标空间不足 | | | ✅ 本次 |
| 15 | sqrt 输入为负数 | ✅ 已完成 | ✅ MathDomainGuard | |
| 16 | 浮点运算使用双精度 | | | ✅ 本次 |
| 17 | 有符号数与无符号数比较 | | | ✅ 本次 |

Phase 2 的 10 种缺陷按难度和实现策略分为三档：

| 档次 | 策略 | 缺陷 |
|------|------|------|
| A — 验证已有能力 | 验证已有 Checker / 编译器 warning，编写测试，生成文档 | #7, #4, #13, #14, #9, #17 |
| B — 新建或增强检查 | 验证已有部分能力 + 新建 / 增强检查器 | #11, #16 |
| C — 新建检查器（含研究） | 研究可行方案，实现完整原型检查器，对无法静态判断的场景输出提示 | #3, #5 |

---

## User Story 1 — 验证未初始化变量检测能力 (Priority: P1)

作为静态分析工具开发者，我希望验证 CSA 对"变量使用前未初始化"的已有检测能力，编写测试程序覆盖典型场景和边界条件，以确认其可靠性并记录检测原理。

**Why this priority**: `core.uninitialized.*` 是 CSA 核心内置 Checker 之一，验证工作量小且能快速完成一种缺陷的覆盖。

**覆盖缺陷**:
- #7 变量使用前未初始化（`core.uninitialized.Assign`, `core.uninitialized.Branch`, `core.uninitialized.UndefReturn` 等）

**Acceptance Scenarios**:

1. **Given** 一个函数中声明了局部变量但未赋值就直接使用的 C++ 程序
   **When** 使用 `clang++ --analyze` 分析该程序
   **Then** 输出包含 `core.uninitialized.*` 类告警，指向使用未初始化变量的位置

2. **Given** 一个未初始化变量用作 if 条件的 C++ 程序
   **When** 使用 `clang++ --analyze` 分析该程序
   **Then** 输出包含 `core.uninitialized.Branch` 告警

3. **Given** 一个局部变量在所有路径上都已初始化后使用的 C++ 程序
   **When** 使用 `clang++ --analyze` 分析该程序
   **Then** 不产生 `core.uninitialized.*` 告警

4. **Given** 每种未初始化变量场景的测试结果
   **When** 汇总分析
   **Then** 生成检测能力报告，记录已覆盖/未覆盖的场景

**Edge Cases**:

- **边界条件**: 变量在某些路径上初始化、某些路径上未初始化（路径敏感能力验证）
- **边界条件**: 结构体成员部分初始化
- **边界条件**: 数组元素未初始化后访问
- **边界条件**: 通过指针间接传递未初始化值

---

## User Story 2 — 验证缓冲区 / 内存操作越界检测能力 (Priority: P1)

作为静态分析工具开发者，我希望验证 CSA 对 `strcpy`/`strcat`/`memcpy`/`memset` 等内存操作函数的越界检测能力，确认 `alpha.unix.cstring.OutOfBounds` 和相关 Checker 的覆盖范围。

**Why this priority**: 缓冲区越界是最常见的安全漏洞之一（CWE-120/CWE-121），三种缺陷（#4, #13, #14）共享同一个 alpha Checker，可以一起验证。

**覆盖缺陷**:
- #4 缓冲区读写越界（strcpy/strcat 目标缓冲区不足）
- #13 memcpy 目标空间不足
- #14 memset 目标空间不足

**Acceptance Scenarios**:

1. **Given** 一个 `strcpy(dst, src)` 其中 dst 缓冲区小于 src 长度的 C 程序
   **When** 使用 `clang++ --analyze` 并启用 `alpha.unix.cstring.OutOfBounds`
   **Then** 输出告警提示目标缓冲区空间不足

2. **Given** 一个 `strcat(dst, src)` 其中拼接后总长度超过 dst 容量的 C 程序
   **When** 使用 `clang++ --analyze` 并启用相关 Checker
   **Then** 输出告警提示缓冲区越界

3. **Given** 一个 `memcpy(dst, src, n)` 其中 n 大于 dst 大小的 C 程序
   **When** 使用 `clang++ --analyze` 并启用相关 Checker
   **Then** 输出告警提示目标空间不足

4. **Given** 一个 `memset(buf, 0, n)` 其中 n 大于 buf 大小的 C 程序
   **When** 使用 `clang++ --analyze` 并启用相关 Checker
   **Then** 输出告警提示目标空间不足

5. **Given** 所有内存操作函数参数大小安全的 C 程序
   **When** 使用 `clang++ --analyze` 分析
   **Then** 不产生越界告警

6. **Given** alpha Checker 的验证结果
   **When** 评估检测覆盖率和误报率
   **Then** 生成评估报告，记录哪些场景能/不能检出；未覆盖场景记录为 Known Limitation，不做增强

**Edge Cases**:

- **边界条件**: 目标和源来自动态分配（`malloc`），CSA 是否能追踪动态大小
- **边界条件**: 字符串长度由外部输入决定（符号值）
- **边界条件**: `strncpy`/`strncat` 等带长度限制的安全变体是否被正确处理
- **边界条件**: 使用 `sizeof` 计算大小但类型有误（常见 C 陷阱）

---

## User Story 3 — 验证整数符号类检测能力 (Priority: P1)

作为静态分析工具开发者，我希望验证编译器 warning 和 clang-tidy 对"无符号数赋负值"和"有符号数与无符号数比较"两类缺陷的检测能力，确认覆盖范围并编写测试。

**Why this priority**: 这两种缺陷已有编译器 warning 支持（`-Wsign-conversion`, `-Wsign-compare`），验证工作量小。部署 CSA 必然有 clang++（两者是同一二进制），因此编译器 warning 在任何 CSA 部署环境中都可用。

**覆盖缺陷**:
- #9 无符号数赋负值
- #17 有符号数与无符号数比较

**Acceptance Scenarios**:

1. **Given** 一个将负数赋值给 `unsigned int` 变量的 C++ 程序
   **When** 使用 `clang++ -Wsign-conversion` 编译
   **Then** 输出 `-Wsign-conversion` 告警

2. **Given** 一个 `int` 与 `unsigned int` 使用 `<`/`>`/`==` 比较的 C++ 程序
   **When** 使用 `clang++ -Wsign-compare` 编译
   **Then** 输出 `-Wsign-compare` 告警

3. **Given** 所有赋值和比较类型匹配正确的 C++ 程序
   **When** 使用相同编译选项
   **Then** 不产生告警

4. **Given** 验证结果
   **When** 汇总分析
   **Then** 生成报告记录编译器 warning 的覆盖范围及建议的使用方式

**Edge Cases**:

- **边界条件**: 隐式类型提升（如 `char` 参与 `unsigned int` 运算）
- **边界条件**: 模板中的有符号/无符号比较
- **边界条件**: `size_t`（无符号）与 `int`（有符号）的循环计数器比较（极其常见的 C++ 场景）
- **边界条件**: 强制类型转换（`static_cast<unsigned>(-1)`）是否仍报

---

## User Story 4 — 浮点数转整数溢出检测 (Priority: P2)

作为静态分析工具开发者，我希望验证 `bugprone-narrowing-conversions` 对浮点数转整数溢出的检测覆盖，并在不足时进行增强，以检测可能导致数据截断或未定义行为的类型转换。

**Why this priority**: 浮点转整数溢出是 C/C++ 未定义行为之一（若浮点值超出目标整数类型可表示范围），已有 clang-tidy check 部分覆盖，需评估后决定是否增强。

**覆盖缺陷**:
- #11 浮点数转整数溢出

**Acceptance Scenarios**:

1. **Given** 一个 `double d = 1e18; int i = d;` 的 C++ 程序
   **When** 使用 clang-tidy `bugprone-narrowing-conversions` 分析
   **Then** 输出告警提示可能的精度丢失或溢出

2. **Given** 一个 `double d = 3.14; int i = (int)d;` 的 C++ 程序
   **When** 使用 clang-tidy 分析
   **Then** 输出告警提示窄化转换

3. **Given** 一个 `int i = 42; double d = i;` 的 C++ 程序（安全方向转换）
   **When** 使用 clang-tidy 分析
   **Then** 不产生告警

4. **Given** 已有 check 覆盖不足的场景
   **When** 评估后确认需要增强
   **Then** 新增或扩展 check 覆盖缺失场景，并编写对应测试

**Edge Cases**:

- **边界条件**: `float` 转 `short`（双重窄化）
- **边界条件**: 函数返回 `double`，调用方用 `int` 接收
- **边界条件**: 模板/泛型代码中的隐式转换
- **边界条件**: `static_cast<int>(d)` 显式转换是否应告警（不同策略有不同取舍）

---

## User Story 5 — 新建浮点运算精度检查 (Priority: P2)

作为静态分析工具开发者，我希望新增多个独立的检查器，分别检测不同类型的浮点精度误用（精度提升、精度丢失、字面量后缀缺失），以帮助开发者管理浮点精度，每种检查可独立启用。

**Why this priority**: 这是嵌入式和高性能计算中的常见关注点。没有现成工具支持，需要全新设计。对不同类型的精度误用分别建立检查器，便于用户按项目需求选择性启用。

**覆盖缺陷**:
- #16 浮点运算使用双精度

**检查器拆分设计**:
- **检查器 A — 精度提升**: `float` 变量参与 `double` 运算（隐式提升导致不必要的 double 计算）
- **检查器 B — 精度丢失**: `double` 值赋给 `float` 变量或传给 `float` 参数（可能丢失有效数字）
- **检查器 C — 字面量后缀缺失**: `float` 上下文中使用了无 `f` 后缀的浮点字面量（如 `float x = 3.14;`）

**Acceptance Scenarios**:

1. **Given** 一个 `float` 变量与 `double` 字面量运算的 C++ 程序（如 `float x = 1.0; float y = x + 2.0;`）
   **When** 使用精度提升检查器分析
   **Then** 输出告警提示 `float` 变量参与了 `double` 精度运算

2. **Given** 一个 `float` 变量与 `float` 字面量运算的 C++ 程序（如 `float x = 1.0f; float y = x + 2.0f;`）
   **When** 使用精度提升检查器分析
   **Then** 不产生告警

3. **Given** 一个 `double` 表达式赋值给 `float` 变量的 C++ 程序（如 `double d = sin(1.0); float f = d;`）
   **When** 使用精度丢失检查器分析
   **Then** 输出告警提示可能的精度丢失

4. **Given** 一个 `float x = 3.14;` 的 C++ 程序（字面量缺少 `f` 后缀）
   **When** 使用字面量后缀检查器分析
   **Then** 输出告警建议使用 `3.14f`

5. **Given** 一个 `float x = 3.14f;` 的 C++ 程序（正确使用 `f` 后缀）
   **When** 使用字面量后缀检查器分析
   **Then** 不产生告警

6. **Given** 三个检查器分别注册
   **When** 用户只启用其中一个
   **Then** 仅产生该检查器负责的告警类型

**Edge Cases**:

- **边界条件**: 字面量 `0.0` vs `0.0f` — 零值是否豁免（精度无损）
- **边界条件**: 数学库函数 `sin(x)` 返回 `double`，赋值给 `float` 变量（精度丢失）
- **边界条件**: 模板代码中的类型推导导致隐式精度变化
- **边界条件**: `static_cast<float>(d)` 显式转换是否仍告警（用户已表达意图）
- **边界条件**: `printf` 等可变参数函数中 float 自动提升为 double（C 标准行为，不应报）

---

## User Story 6 — 死循环检测 (Priority: P2)

作为静态分析工具开发者，我希望实现死循环检测检查器，覆盖常见的死循环模式，并对无法静态判断的循环给出提示性 warning。

**Why this priority**: 死循环是严重的运行时缺陷。虽然停机问题理论上无法完全解决，但常见模式（循环条件恒真、循环变量不变化）可以实用检测。对于依赖外部函数返回值的循环退出条件，应给出"循环退出条件取决于外部调用，建议审查"的提示。

**覆盖缺陷**:
- #3 死循环

**Acceptance Scenarios**:

1. **Given** 一个 `while (true) { ... }` 无 break/return 的 C++ 程序
   **When** 使用检查器分析
   **Then** 输出告警提示循环条件恒真且无退出路径

2. **Given** 一个循环变量在循环体内未被修改的 C++ 程序（如 `while (x > 0) { y++; }`）
   **When** 使用检查器分析
   **Then** 输出告警提示循环条件变量未在循环体内被修改

3. **Given** 一个循环退出条件依赖外部函数返回值的 C++ 程序（如 `while (get_status()) { process(); }`）
   **When** 使用检查器分析
   **Then** 输出提示性 warning："循环退出条件取决于外部调用，建议人工审查"

4. **Given** 一个正常循环（有明确的退出条件和变化变量，如 `for (int i=0; i<n; i++)`）
   **When** 使用检查器分析
   **Then** 不产生告警

5. **Given** 一个有意的事件循环（如 `while (running) { process_events(); }` 其中 `running` 可被其他线程/信号修改）
   **When** 使用检查器分析
   **Then** 可归类为"依赖外部状态"的提示，而非"死循环"错误

6. **Given** 检测方法的可行性分析
   **When** 整理分析
   **Then** 生成研究报告，记录实现方法、检测覆盖范围和已知限制

**Edge Cases**:

- **边界条件**: `do { ... } while (cond)` 与 `while (cond) { ... }` 和 `for (;;)` 三种循环形式
- **边界条件**: 循环体内有 `break` 但在不可达的分支中（`if (false) break;`）
- **边界条件**: 嵌套循环中的外层循环退出条件由内层修改
- **边界条件**: `volatile` 变量作为循环条件（硬件相关场景，不应误报）

---

## User Story 7 — 递归栈溢出检测 (Priority: P2)

作为静态分析工具开发者，我希望实现递归栈溢出检测检查器，检测无终止条件或终止条件不可达的递归调用，并对终止条件依赖外部输入的递归给出提示性 warning。

**Why this priority**: 无限递归导致栈溢出是严重的运行时错误。虽然静态分析难以精确判定递归深度，但无终止条件和间接递归环路可以通过调用图分析检测。对于终止条件依赖外部函数返回值的递归，应给出"递归终止取决于外部调用，建议审查"的提示。

**覆盖缺陷**:
- #5 递归栈溢出

**Acceptance Scenarios**:

1. **Given** 一个没有终止条件的递归函数（如 `void f() { f(); }`）
   **When** 使用检查器分析
   **Then** 输出告警提示无条件递归调用

2. **Given** 两个函数互相调用形成间接递归的 C++ 程序（如 `void a() { b(); } void b() { a(); }`）
   **When** 使用检查器分析
   **Then** 输出告警提示间接递归环路

3. **Given** 一个递归终止条件依赖外部函数返回值的 C++ 程序（如 `void f(int n) { if (get_flag()) return; f(n); }`）
   **When** 使用检查器分析
   **Then** 输出提示性 warning："递归终止条件取决于外部调用，建议人工审查"

4. **Given** 一个有正确终止条件的递归函数（如 `int fact(int n) { if (n <= 1) return 1; return n * fact(n-1); }`）
   **When** 使用检查器分析
   **Then** 不产生告警

5. **Given** 检测方法的可行性分析
   **When** 整理分析
   **Then** 生成研究报告，记录实现方法（调用图分析等）、检测覆盖范围和已知限制

**Edge Cases**:

- **边界条件**: 递归终止条件依赖运行时输入参数（如 `fact(n)` 中 n 由调用方传入），属于正常递归，不应报
- **边界条件**: 回调函数或函数指针形成的间接递归（可能无法静态解析调用目标）
- **边界条件**: 尾递归优化后实际不消耗栈空间，是否仍需报警
- **边界条件**: 递归深度有限但单帧栈占用大（与 US3/大局部变量 Checker 的交叉）

---

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: 对每种 Phase 2 缺陷提供对应的 C/C++ 示例测试程序（正例、反例、边界）
- **FR-002**: 对于已有检测能力的缺陷（#7, #4, #13, #14, #9, #17），验证效果并生成检测能力报告
- **FR-003**: 对于需增强的缺陷（#11），评估已有 check 的覆盖范围，在不足时新建或扩展
- **FR-004**: 对于全新缺陷（#16），新建检查器并在框架中注册
- **FR-005**: 对于研究类缺陷（#3, #5），输出可行性研究报告和概念验证原型
- **FR-006**: 每种检查器必须包含至少 3 个测试用例（正例、反例、边界）
- **FR-007**: 所有验证和新建的检测能力汇总到 Phase 2 完成报告中

### Non-Functional Requirements

- **NFR-001**: 所有新增代码必须能在当前 llvm-project-personal 仓库中编译通过
- **NFR-002**: 新增 Check 的分析性能不应使单文件分析时间增长超过 5%
- **NFR-003**: 新增检查必须可通过命令行选项独立启用/禁用
- **NFR-004**: US6 和 US7 应交付可运行的检查器和配套研究报告；对无法静态判断的场景（循环/递归终止依赖外部调用）须输出提示性 warning

### Key Entities

- **CSA Checker**: Clang Static Analyzer 的路径敏感检查器，运行在 ExplodedGraph 上
- **clang-tidy Check**: 基于 AST Matcher 框架的检查，可独立于 CSA 运行
- **编译器 Warning**: Clang 内置的编译期诊断（如 `-Wsign-conversion`），无需额外开发
- **alpha Checker**: CSA 中标记为实验性的 Checker，可能有较高的误报率
- **调用图 (Call Graph)**: 函数间调用关系的有向图，用于递归和间接调用分析

---

## Assumptions

- 所有开发和测试在当前 llvm-project-personal 仓库（Clang 23.0.0git）上进行
- 构建环境已就绪：CMake + Ninja + build-csa 目录已配置
- MVP 和增强阶段的所有代码和工具已可用
- alpha Checker 可能在未来 LLVM 版本中变更，验证结果基于当前版本
- US6 和 US7 需交付完整原型检查器和研究报告；检查器以 alpha 前缀注册，表示实验性

---

## Out of Scope

- 跨翻译单元分析（CTU）
- 与 CI/CD 系统的集成
- GUI 或 Web 报告界面
- 对已完成的 MVP 7 种缺陷的修改
- 对已完成的 3 种增强（PossibleDivideZero / PossibleArrayBound / MathDomainGuard）的修改
- 基于 LLVM Pass 的分析方法（除非研究报告建议）
- 模型检测工具（CBMC/KLEE）集成

---

## Clarifications

### Session 2026-03-17

- Q: US3 的编译器 warning（`-Wsign-conversion`, `-Wsign-compare`）在真实部署中是否可用？部署 CSA 时是否一定有 clang++？ → A: 选项 A — CSA 是 clang++ 的内置组件，从源码构建 CSA 必然产出 clang++，两者是同一个二进制的不同运行模式。编译器 warning 是纯静态检查（语义分析阶段产生），不需要运行目标代码。因此直接依赖编译器 warning 检测 #9 和 #17 是可行的。
- Q: US2 如果 alpha Checker 对某些场景检测不出，Phase 2 是否应增强？ → A: 选项 A — 仅验证记录，未覆盖场景记录为 Known Limitation，不在 Phase 2 中增强 alpha Checker。
- Q: US5 "浮点运算精度"的检测目标是什么？ → A: 选项 C — 全面检查混合精度，对不同类型的浮点误用分别创建 checker。包括精度提升（float 参与 double 运算）和精度丢失（double 赋给 float），分为独立的检查器以便用户按需启用。
- Q: US6/US7 研究类任务的交付深度？ → A: 选项 C — 文档 + 完整原型，覆盖 Acceptance Scenarios 中列出的所有场景。对于无法静态判断的情况（循环退出条件或递归终止条件依赖外部函数返回值），检查器应给出提示性 warning 而非沉默跳过。
