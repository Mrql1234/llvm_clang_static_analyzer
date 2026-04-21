# Feature Specification: C++ 常见缺陷检测器开发

**Workspace**: `csa-defect-checkers`  
**Created**: 2026-03-17  
**Status**: Completed  
**Input**: 用户描述: "基于 Clang Static Analyzer 修改源码，支持 17 种 C++ 常见运行时缺陷的检测。MVP 阶段先完成 7 种最核心缺陷，灵活选择 CSA Checker / clang-tidy / 编译器 warning 等最合适的方法。"

---

## 背景：17 种缺陷与现有支持情况总览

下表列出全部 17 种缺陷、当前已有工具支持情况、以及建议的实现方式。

| # | 缺陷类型 | 已有支持 | 建议实现方式 | MVP |
|---|---------|---------|-------------|-----|
| 1 | 数组越界（上界/下界） | CSA `security.ArrayBound` (已有) | 验证现有 Checker + 补充测试 | ✅ |
| 2 | 除以零 | CSA `core.DivideZero` (已有) | 验证现有 Checker + 理解源码 | ✅ |
| 3 | 死循环 | 无直接支持 | 需研究可行性，暂缓 | |
| 4 | 缓冲区读写越界（strcpy/strcat） | CSA `alpha.unix.cstring.OutOfBounds` (alpha) | 验证 alpha Checker 效果 | |
| 5 | 递归栈溢出 | 无直接支持 | 静态分析难以精确检测，暂缓 | |
| 6 | 指针使用前非空判断 | CSA `core.NullDereference` (已有) | 验证现有 Checker + 理解源码 | ✅ |
| 7 | 变量使用前未初始化 | CSA `core.uninitialized.*` (已有) | 验证现有 Checker | |
| 8 | 浮点数等号比较 | 编译器 `-Wfloat-equal` | AST 模式匹配 / clang-tidy check | ✅ |
| 9 | 无符号数负值 | 编译器 `-Wsign-conversion` | 编译器 warning | |
| 10 | 反三角函数参数范围 | 无支持 | **新建 CSA Checker** | ✅ |
| 11 | 浮点数转整数溢出 | clang-tidy `bugprone-narrowing-conversions` (部分) | 验证现有 + 考虑增强 | |
| 12 | 函数内大局部变量栈溢出 | 编译器 `-Wframe-larger-than=` | **新建 AST 级检查** | ✅ |
| 13 | memcpy 目标空间不足 | CSA `alpha.unix.cstring.OutOfBounds` (alpha) | 验证 alpha Checker 效果 | |
| 14 | memset 目标空间不足 | CSA `alpha.unix.cstring.OutOfBounds` (alpha) | 验证 alpha Checker 效果 | |
| 15 | sqrt 输入为负数 | 无支持 | **新建 CSA Checker** | ✅ |
| 16 | 浮点运算使用双精度 | 无直接支持 | AST 模式匹配 / clang-tidy check | |
| 17 | 有符号数与无符号数比较 | 编译器 `-Wsign-compare`；clang-tidy `modernize-use-integer-sign-comparison` | 验证现有能力 | |

---

## MVP 范围（7 种缺陷）

### User Story 1 - 理解已有 CSA Checker 检测能力 (Priority: P1)

作为静态分析工具开发者，我希望验证 CSA 对除以零、空指针解引用、数组越界这三类缺陷的已有检测能力，以便掌握 Checker 的工作机制和源码结构。

**Why this priority**: 这三类缺陷是最基础的运行时错误，CSA 已有成熟支持；先理解已有实现是后续开发新 Checker 的前提。

**覆盖缺陷**:
- #2 除以零（`core.DivideZero`）
- #6 空指针解引用（`core.NullDereference`）
- #1 数组越界（`security.ArrayBound`）

**Acceptance Scenarios**:

1. **Given** 一个包含除以零缺陷的 C++ 示例程序  
   **When** 使用自编译的 clang++ --analyze 分析该程序  
   **Then** 输出包含 `core.DivideZero` 告警，指向除零位置，并附带路径推导信息

2. **Given** 一个包含空指针解引用缺陷的 C++ 示例程序  
   **When** 使用自编译的 clang++ --analyze 分析该程序  
   **Then** 输出包含 `core.NullDereference` 告警，指向解引用位置

3. **Given** 一个包含数组越界访问的 C++ 示例程序  
   **When** 使用自编译的 clang++ --analyze 并启用 `security.ArrayBound`  
   **Then** 输出包含数组越界告警

4. **Given** 每种缺陷对应的 Checker 源码  
   **When** 阅读并注释关键路径  
   **Then** 生成每个 Checker 的源码阅读笔记，说明其检测原理

**Edge Cases**:

- **边界条件**: 除以零时除数为变量表达式（非字面量 0），Checker 是否能通过路径敏感分析检出
- **边界条件**: 指针经过多层函数传递后为 null，跨过程分析是否能覆盖
- **边界条件**: 数组下标为负数时的检测行为

---

### User Story 2 - 新建数学函数域检查 Checker (Priority: P1)

作为静态分析工具开发者，我希望新增一个 CSA Checker，检测 `sqrt` 输入为负数以及反三角函数（`asin`、`acos`）参数超出 [-1, 1] 范围的缺陷，以便在编译期发现数学函数的域错误。

**Why this priority**: 这是现有 CSA 完全不覆盖的缺陷类型，需要从零开发新 Checker，是学习 CSA Checker 开发的核心练手项。

**覆盖缺陷**:
- #15 sqrt 输入为负数（含变体 sqrtf / sqrtl / std::sqrt）
- #10 反三角函数参数范围（含变体 asinf / acosf / std::asin / std::acos）

**Acceptance Scenarios**:

1. **Given** 一个调用 `sqrt(-1.0)` 的 C++ 示例程序  
   **When** 使用自编译的 clang++ --analyze 分析该程序  
   **Then** 输出包含 "sqrt argument is negative" 类型的告警

2. **Given** 一个调用 `asin(2.0)` 或 `acos(-1.5)` 的 C++ 示例程序  
   **When** 使用自编译的 clang++ --analyze 分析该程序  
   **Then** 输出包含 "argument out of domain [-1, 1]" 类型的告警

3. **Given** `sqrt(x)` 其中 `x` 是一个已被路径条件约束为非负的变量  
   **When** 分析该程序  
   **Then** 不产生误报

4. **Given** 新 Checker 的源码  
   **When** 在 CSA 框架中注册并编译  
   **Then** 可通过 `-analyzer-checker=<name>` 显式启用

5. **Given** `sqrt(x)` 其中 `x` 在当前执行路径上已被约束为负值（如 `if (x < 0) { sqrt(x); }`）  
   **When** 使用分析器分析该程序  
   **Then** 输出包含 "sqrt argument is negative" 告警

**Edge Cases**:

- **边界条件**: `sqrt(0.0)` 是合法调用，不应报警
- **边界条件**: `asin(1.0)` 和 `acos(-1.0)` 是边界合法值，不应报警
- **边界条件**: 参数为符号值（变量），分析器需利用路径约束判断是否可能为负/超范围

---

### User Story 3 - 新建大局部变量栈溢出检查 (Priority: P1)

作为静态分析工具开发者，我希望新增一个 clang-tidy check，检测函数内定义了过大的局部变量（如大数组）可能导致栈溢出的情况。

**Why this priority**: 这是一种常见的嵌入式/安全关键系统缺陷，现有工具没有直接的检查支持，适合用 clang-tidy 的 AST 模式匹配方法实现。

**覆盖缺陷**:
- #12 函数内定义大内存局部变量导致栈溢出

**Acceptance Scenarios**:

1. **Given** 一个函数内声明了 `int arr[1000000]`（约 4MB）的 C++ 程序  
   **When** 使用检查工具分析该程序  
   **Then** 输出告警，提示局部变量占用栈空间过大

2. **Given** 一个函数内声明了 `char buf[256]`（256 字节）的 C++ 程序  
   **When** 使用检查工具分析该程序  
   **Then** 不产生告警（未超过阈值）

3. **Given** 阈值可配置  
   **When** 用户设置自定义阈值（如 1MB）  
   **Then** 仅对超过该阈值的局部变量报警

**Edge Cases**:

- **边界条件**: 多个局部变量累加后超过阈值，是否应报警
- **边界条件**: VLA（变长数组）无法在编译期确定大小，应单独提示
- **边界条件**: `alloca()` 调用分配的栈内存是否纳入检查

---

### User Story 4 - 浮点数等号比较 clang-tidy 检查 (Priority: P2)

作为静态分析工具开发者，我希望实现一个 clang-tidy check，检测对浮点数使用 `==` 或 `!=` 进行比较的代码模式。

**Why this priority**: 这是经典的编码规范问题，适合用 clang-tidy 的 AST Matcher 框架实现；虽然编译器有 `-Wfloat-equal`，但独立实现有助于理解 AST 遍历和匹配机制。

**覆盖缺陷**:
- #8 浮点数等号比较

**Acceptance Scenarios**:

1. **Given** `if (a == 0.1)` 其中 `a` 为 `double` 类型  
   **When** 使用检查工具分析  
   **Then** 输出告警，提示不应对浮点数使用等号比较

2. **Given** `if (a == b)` 其中 `a`、`b` 均为 `int` 类型  
   **When** 使用检查工具分析  
   **Then** 不产生告警

3. **Given** `if (a != 0.0)` 其中 `a` 为 `float` 类型  
   **When** 使用检查工具分析  
   **Then** 输出告警（`!=` 同样有浮点精度问题）

**Edge Cases**:

- **边界条件**: 与 NaN 比较（`x != x` 是检测 NaN 的惯用写法）不应误报
- **边界条件**: 与字面量 `0.0` 比较在某些场景下是安全的，是否提供抑制选项

---

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: 系统必须能对每种 MVP 缺陷类型提供对应的 C++ 示例测试程序
- **FR-002**: 系统必须能使用自编译的 clang++ --analyze 命令分析示例程序并输出检测结果
- **FR-003**: 对于已有 CSA Checker 的缺陷（除以零、空指针、数组越界），必须验证检测效果并生成源码阅读笔记
- **FR-004**: 对于 sqrt/反三角函数域检查，必须新建 CSA Checker 并在 Clang 源码中注册
- **FR-005**: 对于大局部变量栈溢出检查，必须新建 clang-tidy check（AST Matcher）
- **FR-006**: 对于浮点等号比较检查，必须新建 clang-tidy check（AST Matcher）
- **FR-007**: 每种新建的检查器必须包含至少 3 个测试用例（正例、反例、边界）

### Non-Functional Requirements

- **NFR-001**: 所有新增代码必须能在当前 llvm-project-personal 仓库中编译通过
- **NFR-002**: 新增 Checker 的分析性能不应使单文件分析时间增长超过 5%
- **NFR-003**: 新增检查必须可通过命令行选项独立启用/禁用

### Key Entities

- **CSA Checker**: Clang Static Analyzer 的路径敏感检查器，运行在 ExplodedGraph 上，可获取符号值和路径约束
- **AST Check**: 基于 AST 遍历的模式匹配检查，不做路径分析，适合编码规范类检查
- **clang-tidy Check**: 基于 AST Matcher 框架的检查，可独立于 CSA 运行
- **示例程序**: 每种缺陷对应的最小 C++ 测试文件，包含触发缺陷的代码和期望的检测结果

---

## Assumptions

- 所有开发和测试在当前 llvm-project-personal 仓库（基于 Clang 23.0.0git）上进行
- 构建环境已就绪：CMake + Ninja + build-csa 目录已配置
- 分析命令统一使用 `build-csa/bin/clang++ --analyze` 并显式传入 macOS SDK 路径
- 数学函数域检查的 Checker 以 `alpha.` 前缀注册（表示实验性）
- 大局部变量栈溢出检查的默认阈值设为 1MB

---

## Out of Scope

- 死循环检测（静态分析难以可靠实现，涉及停机问题；后续可研究断言 + 模型检测方法）
- 递归栈溢出检测（需要调用图深度分析，复杂度高，暂缓；后续可研究断言 + 模型检测方法）
- 断言 + 模型检测方法（如 CBMC/KLEE 等，适用于死循环等复杂缺陷，不纳入 MVP）
- 跨翻译单元分析（CTU）
- 与 CI/CD 系统的集成
- GUI 或 Web 报告界面
- Phase 2 的剩余 10 种缺陷（将在 MVP 完成后扩展）

---

## Clarifications

### Session 2026-03-17

- Q: 复杂缺陷（死循环、递归栈溢出）应定位为 Research Phase 还是保持 Out of Scope？ → A: 保持 Out of Scope，MVP 优先，复杂类型以后再说
- Q: MVP 是否纳入"断言 + 模型检测"方法？ → A: 不纳入，MVP 仅用 CSA Checker / AST 检查 / clang-tidy 实现
- Q: 新建检查器的注册位置？ → A: 数学函数域检查作为 CSA Checker（路径敏感）；大局部变量栈溢出和浮点等号比较作为 clang-tidy check（AST Matcher）
