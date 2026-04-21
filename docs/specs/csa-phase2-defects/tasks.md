# Tasks: C++ 缺陷检测器 Phase 2

**Workspace**: `csa-phase2-defects` | **Date**: 2026-03-17
**Input**: `specs/csa-phase2-defects/` 下的设计文档
**Prerequisites**: plan.md (必须), spec.md (必须)

---

## ⚠️ 测试要求（强制）

**每个 Phase 结束时必须满足**：
- 所有测试程序使用对应工具运行完毕
- 每个 Acceptance Scenario 逐条验证并记录通过/未通过
- Edge Cases 覆盖到对应测试程序中
- **User Story Phase 还须**：验收测试通过（覆盖该 Story 的 Acceptance Scenarios 和 Edge Cases）

**验证方式**：运行 `clang++ --analyze` / `clang-tidy` / `clang++ -W...` 对测试程序进行分析，对照预期输出判定。本项目无传统 UT 框架，以工具分析输出作为测试结果。

**不满足以上要求，不能进入下一个 Phase**

---

## 路径约定

- 检查器源码: `llvm-project-personal/clang-tools-extra/clang-tidy/bugprone/`
- 测试程序: `llvm-project-personal/testProgram/`
- 验证报告: `llvm-project-personal/docs/`
- 构建目录: `llvm-project-personal/build-csa/`

---

## Phase 1: Setup (环境验证)

**目的**: 确认 Phase 1 构建环境可用，Phase 2 所需基础设施就绪

- [X] T001 验证构建环境：运行 `ninja -C build-csa clang clang-tidy` 确认编译通过，`build-csa/bin/clang++` 和 `build-csa/bin/clang-tidy` 可执行
- [X] T002 验证已有检查器可用：运行 `build-csa/bin/clang-tidy -list-checks -checks='bugprone-infinite-loop,bugprone-narrowing-conversions'` 确认已有 Check 存在
- [X] T003 [P] 创建 `llvm-project-personal/docs/` 目录用于存放 Phase 2 验证报告

**Phase Gate**: 构建环境可用，已有检查器可验证

---

## Phase 2: User Story 1 — 验证未初始化变量检测能力 (Priority: P1) 🎯

**目标**: 验证 `core.uninitialized.*` 6 个 Checker 的检测覆盖，生成验证报告

### 实现

- [X] T004 [US1] 编写测试程序 `llvm-project-personal/testProgram/test_uninitialized.cpp`，覆盖以下场景：
  - 正例：局部变量未赋值直接使用（触发 `core.uninitialized.Assign`）
  - 正例：未初始化变量用作 if 条件（触发 `core.uninitialized.Branch`）
  - 正例：函数返回未初始化值（触发 `core.uninitialized.UndefReturn`）
  - 正例：未初始化值用作数组下标（触发 `core.uninitialized.ArraySubscript`）
  - 正例：new[] 大小未初始化（触发 `core.uninitialized.NewArraySize`）
  - 反例：所有路径上都已初始化后使用（不应报）
  - 边界：变量在某些路径初始化、某些路径未初始化
  - 边界：结构体成员部分初始化
  - 边界：数组元素未初始化后访问
  - 边界：通过指针间接传递未初始化值

### 验收测试（必须）

- [X] T005 [US1] 运行 `clang++ --analyze` 分析 `test_uninitialized.cpp`，逐条验证 spec 中 4 个 Acceptance Scenarios：
  - AS1: 未赋值直接使用 → 应产生 `core.uninitialized.*` 告警
  - AS2: 未初始化变量用作 if 条件 → 应产生 `core.uninitialized.Branch` 告警
  - AS3: 所有路径已初始化 → 不应产生告警
  - AS4: 汇总所有场景 → 记录通过/未通过

### 报告

- [X] T006 [US1] 生成验证报告 `llvm-project-personal/docs/phase2-us1-uninitialized-report.md`，内容包括：每个 Checker 的验证结果、已覆盖/未覆盖场景、检测原理说明、已知限制

**Phase Gate**: US1 全部 Acceptance Scenarios 验证通过，报告已生成

---

## Phase 3: User Story 2 — 验证缓冲区/内存操作越界检测能力 (Priority: P1)

**目标**: 验证 `alpha.unix.cstring.OutOfBounds` 对 strcpy/strcat/memcpy/memset 越界的检测覆盖

### 实现

- [X] T007 [US2] 编写测试程序 `llvm-project-personal/testProgram/test_cstring_bounds.cpp`，覆盖以下场景：
  - 正例：`strcpy(dst, src)` 其中 dst 小于 src 长度
  - 正例：`strcat(dst, src)` 拼接后超过 dst 容量
  - 正例：`memcpy(dst, src, n)` 其中 n > sizeof(dst)
  - 正例：`memset(buf, 0, n)` 其中 n > sizeof(buf)
  - 反例：所有内存操作参数大小安全
  - 边界：目标和源来自 `malloc` 动态分配
  - 边界：字符串长度由外部输入决定（符号值）
  - 边界：`strncpy`/`strncat` 安全变体
  - 边界：`sizeof` 计算大小但类型有误

### 验收测试（必须）

- [X] T008 [US2] 运行 `clang++ --analyze -Xanalyzer -analyzer-checker=alpha.unix.cstring.OutOfBounds` 分析 `test_cstring_bounds.cpp`，逐条验证 spec 中 6 个 Acceptance Scenarios：
  - AS1: strcpy 越界 → 应产生告警
  - AS2: strcat 越界 → 应产生告警
  - AS3: memcpy 不足 → 应产生告警
  - AS4: memset 不足 → 应产生告警
  - AS5: 安全调用 → 不应产生告警
  - AS6: 未检出场景 → 记录为 Known Limitation，不做增强

### 报告

- [X] T009 [US2] 生成验证报告 `llvm-project-personal/docs/phase2-us2-cstring-bounds-report.md`，内容包括：每种函数的检测结果、Known Limitations 清单、alpha Checker 行为分析

**Phase Gate**: US2 全部 Acceptance Scenarios 验证通过，报告已生成

---

## Phase 4: User Story 3 — 验证整数符号类检测能力 (Priority: P1)

**目标**: 验证 `-Wsign-conversion` 和 `-Wsign-compare` 编译器 warning 的覆盖范围

### 实现

- [X] T010 [US3] 编写测试程序 `llvm-project-personal/testProgram/test_sign_conversion.cpp`，覆盖以下场景：
  - 正例：将负数赋值给 `unsigned int`（应触发 `-Wsign-conversion`）
  - 正例：`int` 与 `unsigned int` 使用 `<`/`>`/`==` 比较（应触发 `-Wsign-compare`）
  - 反例：类型匹配正确的赋值和比较（不应报）
  - 边界：隐式类型提升（`char` 参与 `unsigned int` 运算）
  - 边界：模板中的有符号/无符号比较
  - 边界：`size_t` 与 `int` 的循环计数器比较
  - 边界：`static_cast<unsigned>(-1)` 强制转换

### 验收测试（必须）

- [X] T011 [US3] 运行 `clang++ -fsyntax-only -Wsign-conversion -Wsign-compare` 分析 `test_sign_conversion.cpp`，逐条验证 spec 中 4 个 Acceptance Scenarios：
  - AS1: 负数赋给 unsigned → 应产生 `-Wsign-conversion` 告警
  - AS2: int 与 unsigned 比较 → 应产生 `-Wsign-compare` 告警
  - AS3: 类型匹配正确 → 不应产生告警
  - AS4: 汇总 → 记录覆盖范围和建议使用方式

### 报告

- [X] T012 [US3] 生成验证报告 `llvm-project-personal/docs/phase2-us3-sign-warnings-report.md`，内容包括：每种场景的告警输出、覆盖/未覆盖分析、建议的编译选项组合

**Phase Gate**: US3 全部 Acceptance Scenarios 验证通过，报告已生成

---

## Phase 5: User Story 4 — 浮点数转整数溢出检测 (Priority: P2)

**目标**: 验证 `bugprone-narrowing-conversions` 覆盖范围，评估是否需要增强

### 实现

- [X] T013 [US4] 编写测试程序 `llvm-project-personal/testProgram/test_narrowing_conversion.cpp`，覆盖以下场景：
  - 正例：`double d = 1e18; int i = d;`（溢出）
  - 正例：`double d = 3.14; int i = (int)d;`（窄化）
  - 反例：`int i = 42; double d = i;`（安全方向转换）
  - 边界：`float` 转 `short`（双重窄化）
  - 边界：函数返回 `double`，调用方用 `int` 接收
  - 边界：模板/泛型代码中的隐式转换
  - 边界：`static_cast<int>(d)` 显式转换

### 验收测试（必须）

- [X] T014 [US4] 运行 `clang-tidy -checks='-*,bugprone-narrowing-conversions'` 分析 `test_narrowing_conversion.cpp`，逐条验证 spec 中 4 个 Acceptance Scenarios：
  - AS1: double → int 溢出 → 应产生告警
  - AS2: double → int 窄化 → 应产生告警
  - AS3: int → double 安全方向 → 不应产生告警
  - AS4: 如覆盖不足 → 评估增强可行性并记录

### 报告

- [X] T015 [US4] 生成评估报告 `llvm-project-personal/docs/phase2-us4-narrowing-report.md`，内容包括：覆盖场景矩阵、增强评估结论、已知限制

**Phase Gate**: US4 全部 Acceptance Scenarios 验证通过，评估报告已生成

---

## Phase 6: User Story 5 — 新建浮点运算精度检查 (Priority: P2)

**目标**: 新建 3 个独立 clang-tidy Check，分别检测精度提升、精度丢失、字面量后缀缺失

### 实现 — 检查器 A: 精度提升

- [X] T016 [P] [US5] 创建 `clang-tools-extra/clang-tidy/bugprone/FloatPrecisionPromotionCheck.h`：声明 `FloatPrecisionPromotionCheck` 类，继承 `ClangTidyCheck`
- [X] T017 [P] [US5] 创建 `clang-tools-extra/clang-tidy/bugprone/FloatPrecisionPromotionCheck.cpp`：实现 `registerMatchers`（匹配 float 参与 double 运算的 binaryOperator）和 `check`（排除可变参数提升、显式 cast，报告隐式精度提升）

### 实现 — 检查器 B: 精度丢失

- [X] T018 [P] [US5] 创建 `clang-tools-extra/clang-tidy/bugprone/FloatPrecisionLossCheck.h`：声明 `FloatPrecisionLossCheck` 类
- [X] T019 [P] [US5] 创建 `clang-tools-extra/clang-tidy/bugprone/FloatPrecisionLossCheck.cpp`：实现 `registerMatchers`（匹配 double → float 隐式转换的 implicitCastExpr / varDecl）和 `check`（排除显式 `static_cast<float>`、0.0 常量，报告精度丢失）

### 实现 — 检查器 C: 字面量后缀缺失

- [X] T020 [P] [US5] 创建 `clang-tools-extra/clang-tidy/bugprone/FloatLiteralSuffixCheck.h`：声明 `FloatLiteralSuffixCheck` 类
- [X] T021 [P] [US5] 创建 `clang-tools-extra/clang-tidy/bugprone/FloatLiteralSuffixCheck.cpp`：实现 `registerMatchers`（匹配 float 上下文中的 floatLiteral）和 `check`（检查字面量是否为 double 类型即无 f 后缀，排除 0.0，报告建议加 f 后缀）

### 注册与构建

- [X] T022 [US5] 修改 `clang-tools-extra/clang-tidy/bugprone/BugproneTidyModule.cpp`：`#include` 三个新头文件，注册 `bugprone-float-precision-promotion`、`bugprone-float-precision-loss`、`bugprone-float-literal-suffix`
- [X] T023 [US5] 修改 `clang-tools-extra/clang-tidy/bugprone/CMakeLists.txt`：添加 `FloatPrecisionPromotionCheck.cpp`、`FloatPrecisionLossCheck.cpp`、`FloatLiteralSuffixCheck.cpp`
- [X] T024 [US5] 编译验证：运行 `ninja -C build-csa clang-tidy`，确认编译通过无错误

### 测试

- [X] T025 [P] [US5] 编写测试程序 `llvm-project-personal/testProgram/test_float_precision_promotion.cpp`，覆盖：
  - 正例：`float x = 1.0; float y = x + 2.0;`（float 参与 double 运算）
  - 反例：`float x = 1.0f; float y = x + 2.0f;`（全 float 运算）
  - 边界：`printf` 中 float 自动提升为 double（不应报）

- [X] T026 [P] [US5] 编写测试程序 `llvm-project-personal/testProgram/test_float_precision_loss.cpp`，覆盖：
  - 正例：`double d = sin(1.0); float f = d;`（精度丢失）
  - 反例：`float f = 1.0f;`（同类型赋值）
  - 边界：`static_cast<float>(d)` 显式转换（不应报）
  - 边界：`float f = 0.0;`（零值豁免）

- [X] T027 [P] [US5] 编写测试程序 `llvm-project-personal/testProgram/test_float_literal_suffix.cpp`，覆盖：
  - 正例：`float x = 3.14;`（缺少 f 后缀）
  - 反例：`float x = 3.14f;`（正确后缀）
  - 边界：`float x = 0.0;`（零值豁免）

### 验收测试（必须）

- [X] T028 [US5] 分别运行三个检查器验证 spec 中 6 个 Acceptance Scenarios：
  - AS1: `clang-tidy -checks='-*,bugprone-float-precision-promotion'` 分析 promotion 测试 → 应对 float+double 运算报警
  - AS2: 全 float 运算 → 不应报警
  - AS3: `clang-tidy -checks='-*,bugprone-float-precision-loss'` 分析 loss 测试 → 应对 double→float 报警
  - AS4: `clang-tidy -checks='-*,bugprone-float-literal-suffix'` 分析 suffix 测试 → 应对缺少 f 后缀报警
  - AS5: 正确使用 f 后缀 → 不应报警
  - AS6: 只启用一个检查器 → 仅产生该检查器的告警类型

**Phase Gate**: US5 三个检查器编译通过，全部 Acceptance Scenarios 验证通过

---

## Phase 7: User Story 6 — 死循环检测 (Priority: P2)

**目标**: 验证已有 `bugprone-infinite-loop` 覆盖，新建 `bugprone-loop-external-dependency` 补充检测，生成研究报告

### 验证已有能力

- [X] T029 [US6] 编写测试程序 `llvm-project-personal/testProgram/test_loop_external_dep.cpp`，覆盖以下场景：
  - 场景 1：`while (true) { ... }` 无 break/return（`bugprone-infinite-loop` 应检出）
  - 场景 2：循环变量在体内未被修改 `while (x > 0) { y++; }`（`bugprone-infinite-loop` 应检出）
  - 场景 3：循环退出依赖外部函数 `while (get_status()) { process(); }`（新检查器应提示）
  - 场景 4：正常循环 `for (int i=0; i<n; i++)`（不应报）
  - 场景 5：事件循环 `while (running) { process_events(); }` 其中 running 可被外部修改（应归类为外部依赖提示）
  - 边界：`do { } while (cond)` 和 `for(;;)` 形式
  - 边界：`if (false) break;` 不可达 break
  - 边界：嵌套循环中外层条件由内层修改
  - 边界：`volatile` 变量作为循环条件（不应误报）

- [X] T030 [US6] 运行 `clang-tidy -checks='-*,bugprone-infinite-loop'` 分析测试程序，记录场景 1-2 的检出情况，确认已有检查器覆盖范围

### 实现新检查器

- [X] T031 [US6] 创建 `clang-tools-extra/clang-tidy/bugprone/LoopExternalDependencyCheck.h`：声明类，继承 `ClangTidyCheck`
- [X] T032 [US6] 创建 `clang-tools-extra/clang-tidy/bugprone/LoopExternalDependencyCheck.cpp`：实现核心逻辑——`registerMatchers`（匹配 whileStmt/forStmt/doStmt），`check`（扫描条件变量，判断修改是否仅依赖外部函数调用，排除 volatile 和直接赋值，输出提示性 warning）
- [X] T033 [US6] 修改 `clang-tools-extra/clang-tidy/bugprone/BugproneTidyModule.cpp`：注册 `bugprone-loop-external-dependency`
- [X] T034 [US6] 修改 `clang-tools-extra/clang-tidy/bugprone/CMakeLists.txt`：添加 `LoopExternalDependencyCheck.cpp`
- [X] T035 [US6] 编译验证：运行 `ninja -C build-csa clang-tidy`，确认编译通过

### 验收测试（必须）

- [X] T036 [US6] 运行 `clang-tidy -checks='-*,bugprone-infinite-loop,bugprone-loop-external-dependency'` 分析测试程序，逐条验证 spec 中 6 个 Acceptance Scenarios：
  - AS1: `while(true)` 无 break → 由 `bugprone-infinite-loop` 报死循环
  - AS2: 条件变量未修改 → 由 `bugprone-infinite-loop` 报告
  - AS3: 退出依赖外部调用 → 由 `bugprone-loop-external-dependency` 输出提示 warning
  - AS4: 正常循环 → 不报
  - AS5: 事件循环 → 归类为外部依赖提示
  - AS6: 生成研究报告

### 报告

- [X] T037 [US6] 生成研究报告 `llvm-project-personal/docs/phase2-us6-infinite-loop-research.md`，内容包括：已有 `bugprone-infinite-loop` 覆盖分析、新检查器设计原理、检测覆盖范围矩阵、已知限制（停机问题的本质约束）

**Phase Gate**: US6 已有检查验证完毕 + 新检查器编译通过 + 全部 Acceptance Scenarios 验证通过 + 研究报告已生成

---

## Phase 8: User Story 7 — 递归栈溢出检测 (Priority: P2)

**目标**: 新建 `bugprone-unbounded-recursion` 检查器，检测无终止递归和间接递归，生成研究报告

### 实现

- [X] T038 [US7] 创建 `clang-tools-extra/clang-tidy/bugprone/UnboundedRecursionCheck.h`：声明类，继承 `ClangTidyCheck`，重写 `registerMatchers` 和 `check`
- [X] T039 [US7] 创建 `clang-tools-extra/clang-tidy/bugprone/UnboundedRecursionCheck.cpp`：实现核心逻辑——使用 `CallGraph` 构建调用图，使用 `scc_iterator` 找递归环，分析终止条件（扫描 returnStmt 是否在条件分支中），分类报告（无终止条件 → 错误，依赖外部调用 → 提示 warning，间接递归 → 环路告警）
- [X] T040 [US7] 修改 `clang-tools-extra/clang-tidy/bugprone/BugproneTidyModule.cpp`：注册 `bugprone-unbounded-recursion`
- [X] T041 [US7] 修改 `clang-tools-extra/clang-tidy/bugprone/CMakeLists.txt`：添加 `UnboundedRecursionCheck.cpp`
- [X] T042 [US7] 编译验证：运行 `ninja -C build-csa clang-tidy`，确认编译通过

### 测试

- [X] T043 [US7] 编写测试程序 `llvm-project-personal/testProgram/test_unbounded_recursion.cpp`，覆盖以下场景：
  - 正例：无终止条件递归 `void f() { f(); }`
  - 正例：间接递归 `void a() { b(); } void b() { a(); }`
  - 正例：终止条件依赖外部调用 `void f(int n) { if (get_flag()) return; f(n); }`
  - 反例：正确递归 `int fact(int n) { if (n<=1) return 1; return n*fact(n-1); }`
  - 边界：终止条件依赖运行时参数（正常递归，不应报）
  - 边界：回调/函数指针间接递归（无法静态解析，不应报）
  - 边界：尾递归

### 验收测试（必须）

- [X] T044 [US7] 运行 `clang-tidy -checks='-*,bugprone-unbounded-recursion'` 分析测试程序，逐条验证 spec 中 5 个 Acceptance Scenarios：
  - AS1: 无终止递归 → 应产生错误告警
  - AS2: 间接递归环路 → 应产生告警
  - AS3: 终止依赖外部调用 → 应产生提示 warning
  - AS4: 正确递归 → 不报
  - AS5: 生成研究报告

### 报告

- [X] T045 [US7] 生成研究报告 `llvm-project-personal/docs/phase2-us7-recursion-research.md`，内容包括：调用图分析方法（CallGraph + SCCIterator）、终止条件判定算法、检测覆盖矩阵、已知限制（函数指针/虚函数不可解析）

**Phase Gate**: US7 检查器编译通过 + 全部 Acceptance Scenarios 验证通过 + 研究报告已生成

---

## Phase 9: Polish & 收尾

**目的**: 汇总所有结果，全量回归，生成 Phase 2 完成报告

- [X] T046 全量编译验证：运行 `ninja -C build-csa clang clang-tidy`，确认所有新增代码与已有代码共同编译通过
- [X] T047 全量回归测试：依次运行 Phase 1 的所有测试程序（test_divide_zero, test_array_bound, test_possible_div_zero, test_possible_array_bound, test_math_domain, test_math_domain_guard 等），确认 Phase 2 改动未破坏已有功能
- [X] T048 运行所有 Phase 2 测试程序的汇总验证：一次性运行 9 个新测试程序（test_uninitialized, test_cstring_bounds, test_sign_conversion, test_narrowing_conversion, test_float_precision_promotion, test_float_precision_loss, test_float_literal_suffix, test_loop_external_dep, test_unbounded_recursion），确认全部通过
- [X] T049 性能基准测试（NFR-002）：选取一个中等规模的 C++ 测试文件，分别在启用和不启用 Phase 2 新增的 5 个 clang-tidy Check 的情况下运行 `clang-tidy`，记录分析耗时，确认新增 Check 带来的性能开销不超过 5%
- [X] T050 生成 Phase 2 完成报告 `llvm-project-personal/docs/phase2-completion-report.md`，汇总所有 7 个 US 的验证/实现结果、检测能力矩阵（17 种缺陷全覆盖情况）、性能基准结果、已知限制
- [X] T051 检查并更新 `llvm-project-personal/todo.md`，标记 Phase 2 所有缺陷已完成

**Phase Gate**: 全量编译通过，回归测试通过，性能基准达标（<5%），Phase 2 完成报告已生成

---

## 依赖与执行顺序

### Phase 依赖

- **Phase 1 (Setup)**: 无依赖，立即开始
- **Phase 2-5 (US1-US4, 验证类)**: 依赖 Phase 1 完成；四个 US **可并行**（不同测试文件，无代码依赖）
- **Phase 6 (US5, 新建浮点 Check)**: 依赖 Phase 1 完成；与 Phase 2-5 无代码依赖，但建议先完成验证类以积累经验
- **Phase 7 (US6, 死循环)**: 依赖 Phase 1 完成；新建 Check 需编译，与 US5 需顺序编译
- **Phase 8 (US7, 递归)**: 依赖 Phase 7 完成（共享 BugproneTidyModule.cpp 和 CMakeLists.txt 修改）
- **Phase 9 (Polish)**: 依赖所有前序 Phase 完成

### 故事内部顺序

- US1-US4（验证类）: 编写测试 → 运行验证 → 生成报告
- US5（新建 Check）: 创建 .h/.cpp → 注册 + CMake → 编译 → 测试 → 验收
- US6: 验证已有 → 新建补充 Check → 注册 + CMake → 编译 → 验收 → 报告
- US7: 新建 Check → 注册 + CMake → 编译 → 测试 → 验收 → 报告

### 并行机会

- US1-US4 全部可并行（标记 [P] 的不同文件任务）
- US5 的三个检查器的 .h/.cpp 创建可并行（T016-T021 标记 [P]）
- US5 的三个测试程序可并行编写（T025-T027 标记 [P]）

---

## 实施策略

### MVP 优先（US1-US3, P1）

1. 完成 Phase 1: Setup
2. 并行完成 Phase 2-4: US1, US2, US3 验证
3. **验证**: 6 种缺陷（#7, #4, #13, #14, #9, #17）的已有检测能力已确认
4. 可交付 MVP 验证报告

### 增量交付

```
Phase 1 → 环境就绪
  ↓
Phase 2-5 → US1-US4 验证完成 → 6 种缺陷 + 1 种评估 = 7 种 ✓
  ↓
Phase 6 → US5 浮点精度 → +1 种缺陷 (3 个检查器) ✓
  ↓
Phase 7 → US6 死循环 → +1 种缺陷 ✓
  ↓
Phase 8 → US7 递归 → +1 种缺陷 ✓
  ↓
Phase 9 → 收尾 → 17 种缺陷全覆盖 🎯
```

---

## Notes

- 本项目无传统 UT 框架（JUnit/pytest 等），以工具分析输出对照预期结果作为验证手段
- [P] 任务 = 不同文件，无依赖，可并行
- [Story] 标签 = 映射到具体用户故事
- US5 的三个检查器共享 `BugproneTidyModule.cpp` 和 `CMakeLists.txt` 的修改，T022/T023 合并处理
- US6 和 US7 的 `BugproneTidyModule.cpp` 和 `CMakeLists.txt` 修改可在各自 Phase 内完成，避免跨 Phase 冲突
- 每次修改 `BugproneTidyModule.cpp` / `CMakeLists.txt` 后需重新编译 `ninja -C build-csa clang-tidy`
- 验证报告格式参照 Phase 1 已有的 `enhancement-report.md` 风格
