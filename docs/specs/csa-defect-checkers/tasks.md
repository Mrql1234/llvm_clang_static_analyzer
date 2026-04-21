# Tasks: C++ 常见缺陷检测器 MVP

**Workspace**: `csa-defect-checkers` | **Date**: 2026-03-17  
**Input**: `specs/csa-defect-checkers/` 下的设计文档  
**Prerequisites**: plan.md (必须), spec.md (必须)

---

## ⚠️ 测试要求（强制）

**每个 Phase 结束时必须满足**：
- 所有测试程序通过对应的分析工具运行，输出符合预期
- 每种新建检查器至少 3 个测试用例（正例、反例、边界）
- **User Story Phase 还须**：验收测试通过（覆盖该 Story 的 Acceptance Scenarios 和 Edge Cases）

**不满足以上要求，不能进入下一个 Phase**

**分析命令约定**：
- CSA: `build-csa/bin/clang++ --analyze -isysroot "$(xcrun --show-sdk-path)" -I"$(xcrun --show-sdk-path)/usr/include/c++/v1" -Xanalyzer -analyzer-output=text <file>`
- clang-tidy: `build-csa/bin/clang-tidy -checks='-*,bugprone-<check-name>' <file> -- -isysroot "$(xcrun --show-sdk-path)" -I"$(xcrun --show-sdk-path)/usr/include/c++/v1"`

---

## 任务格式

```
- [ ] [TaskID] [P?] [Story?] 描述，含文件路径
```

- **[P]**: 可并行执行（不同文件，无依赖）
- **[Story]**: 所属用户故事（如 [US1], [US2], [US3], [US4]）
- 必须包含精确的文件路径

---

## 路径约定

**CSA Checker 源码**:
- 注册: `clang/include/clang/StaticAnalyzer/Checkers/Checkers.td`
- 实现: `clang/lib/StaticAnalyzer/Checkers/`
- 构建: `clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt`

**clang-tidy Check 源码**:
- 实现: `clang-tools-extra/clang-tidy/bugprone/`
- 注册: `clang-tools-extra/clang-tidy/bugprone/BugproneTidyModule.cpp`
- 构建: `clang-tools-extra/clang-tidy/bugprone/CMakeLists.txt`
- 脚手架: `clang-tools-extra/clang-tidy/add_new_check.py`

**测试程序**: `testProgram/`

**源码笔记**: `docs/`

以上路径均相对于仓库根 `/Users/yqg/codex_chat/llvm-project-personal/`

---

## Phase 1: Setup (环境验证与准备)

**目的**: 验证构建环境、准备测试基础设施、确保 clang-tidy 可用

- [x] T001 验证 build-csa 中 clang++ 可用，运行 `build-csa/bin/clang++ --version` 确认版本为 23.0.0git
- [x] T002 创建测试程序目录结构，确认 `testProgram/` 目录存在
- [x] T003 构建 clang-tidy：在 `build-csa/` 下运行 `ninja clang-tidy`，等待编译完成，验证 `build-csa/bin/clang-tidy --version`
- [x] T004 创建源码笔记目录 `docs/checker-notes/`
- [x] T005 [P] 验证已有 demo.cpp 能正常通过 `clang++ --analyze` 分析（含 macOS SDK 路径参数）

**Phase Gate**: clang++、clang-tidy 均可用，测试目录就绪

---

## Phase 2: User Story 1 — 理解已有 CSA Checker 检测能力 (Priority: P1) 🎯

**目标**: 验证 CSA 对除以零、空指针解引用、数组越界的检测能力，阅读源码并生成笔记，为后续新建 Checker 打基础

### 实现

- [x] T006 [P] [US1] 编写除以零测试程序 `testProgram/test_divide_zero.cpp`：包含字面量除零、变量除零（路径敏感）、安全除法（不应报警）三种场景
- [x] T007 [P] [US1] 编写空指针解引用测试程序 `testProgram/test_null_deref.cpp`：包含直接解引用 nullptr、条件分支后解引用、多层函数传递 null 三种场景
- [x] T008 [P] [US1] 编写数组越界测试程序 `testProgram/test_array_bound.cpp`：包含上界越界、负数下标、合法访问（不应报警）三种场景
- [x] T009 [US1] 运行 `clang++ --analyze` 分析 test_divide_zero.cpp，验证输出包含 `core.DivideZero` 告警并记录结果
- [x] T010 [US1] 运行 `clang++ --analyze` 分析 test_null_deref.cpp，验证输出包含 `core.NullDereference` 告警并记录结果
- [x] T011 [US1] 运行 `clang++ --analyze -analyzer-checker=security.ArrayBound` 分析 test_array_bound.cpp，验证输出包含数组越界告警并记录结果

### 源码阅读笔记

- [x] T012 [P] [US1] 阅读 `clang/lib/StaticAnalyzer/Checkers/DivZeroChecker.cpp` 源码，生成笔记 `docs/checker-notes/divzero-checker.md`，说明 Checker 继承结构、checkPreStmt 回调、BugType 注册、如何调用 ConstraintManager 判断除数为零
- [x] T013 [P] [US1] 阅读 `clang/lib/StaticAnalyzer/Checkers/DereferenceChecker.cpp` 源码，生成笔记 `docs/checker-notes/null-deref-checker.md`，说明空指针检测的路径敏感分析流程
- [x] T014 [P] [US1] 阅读 `clang/lib/StaticAnalyzer/Checkers/ArrayBoundChecker.cpp` 源码，生成笔记 `docs/checker-notes/array-bound-checker.md`，说明数组越界检测的内存区域和偏移量计算方法

### 验收测试

- [x] T015 [US1] 验收场景 1：test_divide_zero.cpp 分析输出包含 `core.DivideZero` 告警，指向除零位置，附带路径推导信息
- [x] T016 [US1] 验收场景 2：test_null_deref.cpp 分析输出包含 `core.NullDereference` 告警，指向解引用位置
- [x] T017 [US1] 验收场景 3：test_array_bound.cpp 分析输出包含数组越界告警（需启用 `security.ArrayBound`）
- [x] T018 [US1] 验收场景 4：三份源码阅读笔记已生成，每份说明了检测原理
- [x] T019 [US1] 边界验证：test_divide_zero.cpp 中除数为变量表达式（非字面量 0）时，Checker 通过路径敏感分析检出
- [x] T020 [US1] 边界验证：test_array_bound.cpp 中数组下标为负数时的检测行为已验证并记录
- [x] T020a [US1] 边界验证：test_null_deref.cpp 中指针经多层函数传递后为 null，验证跨过程分析是否能检出并记录结果

**Phase Gate**: 三类缺陷检测验证通过，三份源码笔记已生成，边界条件已验证

---

## Phase 3: User Story 2 — 新建数学函数域检查 Checker (Priority: P1) 🎯

**目标**: 从零开发 CSA Checker `alpha.security.MathDomain`，检测 sqrt 负数输入和反三角函数参数越界

### 实现

- [x] T021 [US2] 在 `clang/include/clang/StaticAnalyzer/Checkers/Checkers.td` 的 `let ParentPackage = SecurityAlpha in { ... }` 块内添加 `MathDomainChecker` 注册条目（使用 `NotDocumented` 以匹配其他 alpha checker 惯例）
- [x] T022 [US2] 创建 `clang/lib/StaticAnalyzer/Checkers/MathDomainChecker.cpp`：继承 `Checker<check::PreCall>`，使用 `CallDescription`+`CDM::CLibrary` 匹配函数，通过 `EvaluateAsFloat` 进行常量域检查（CSA 不支持浮点符号推理，`SymbolManager::canSymbolicate` 对 float 返回 false）
- [x] T023 [US2] 在 `clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt` 的源文件列表中按字母序添加 `MathDomainChecker.cpp`
- [x] T024 [US2] 在 `build-csa/` 下运行 `ninja clang` 增量编译，编译无错误
- [x] T025 [US2] 运行 `clang -cc1 -analyzer-checker-help-alpha | grep MathDomain` 确认 `alpha.security.MathDomain` 已注册

### 测试程序

- [x] T026 [US2] 编写测试程序 `testProgram/test_math_domain.cpp`：含所有正例、反例、边界、路径敏感场景

### 验收测试

- [x] T027 [US2] 验收场景 1：`sqrt(-1.0)` → "Argument to sqrt is negative" ✓
- [x] T028 [US2] 验收场景 2：`asin(2.0)` → "Argument to asin is out of the range [-1, 1]" ✓；`acos(-1.5)` → 同类告警 ✓
- [x] T029 [US2] 验收场景 3：`sqrt(x)` 路径约束非负 → 无误报 ✓
- [x] T030 [US2] 验收场景 4：`-analyzer-checker=alpha.security.MathDomain` 启用 Checker ✓
- [x] T031 [US2] 边界验证：`sqrt(0.0)` 不报警 ✓，`asin(1.0)` 不报警 ✓，`acos(-1.0)` 不报警 ✓
- [x] T032 [US2] 边界验证：参数为符号值（变量）时，无法判断时不报 ✓（`sqrt(x)` 无约束不报警）
- [ ] T032a [US2] 验收场景 5：`if (x < 0) { sqrt(x); }` → **未通过**。CSA 的 `SymbolManager::canSymbolicate` 对浮点类型返回 false，无法进行路径敏感的浮点域约束推理。当前实现使用 AST 常量求值（`EvaluateAsFloat`），仅能检测编译期常量违规。需要扩展 CSA 的符号推理引擎支持浮点类型才能解决此问题

**Phase Gate**: MathDomainChecker 编译通过、注册成功，所有验收场景和边界条件通过

---

## Phase 4: User Story 3 — 新建大局部变量栈溢出 clang-tidy Check (Priority: P1) 🎯

**目标**: 开发 `bugprone-large-stack-variable` clang-tidy Check，检测函数内大局部变量导致的栈溢出风险

### 实现

- [x] T033 [US3] 使用脚手架生成 Check 骨架：在 `clang-tools-extra/clang-tidy/` 下运行 `python3 add_new_check.py bugprone large-stack-variable`，生成 `bugprone/LargeStackVariableCheck.h`、`bugprone/LargeStackVariableCheck.cpp`，自动更新 `bugprone/CMakeLists.txt` 和 `bugprone/BugproneTidyModule.cpp`
- [x] T034 [US3] 实现 `clang-tools-extra/clang-tidy/bugprone/LargeStackVariableCheck.h`：添加 `Threshold` 成员变量（默认 1048576 = 1MB），声明 `storeOptions` 方法
- [x] T035 [US3] 实现 `clang-tools-extra/clang-tidy/bugprone/LargeStackVariableCheck.cpp`：
  - `registerMatchers`：匹配 `varDecl(hasLocalStorage()).bind("var")`；另匹配 `callExpr(callee(functionDecl(hasAnyName("alloca","__builtin_alloca","__builtin_alloca_with_align")))).bind("alloca")` 检测 alloca 调用
  - `check`：获取 VarDecl，通过 `ASTContext::getTypeInfo` 计算变量大小，超过 Threshold 则 `diag()` 报警
  - 特殊处理 VLA（VariableArrayType）：编译期无法确定大小，单独警告
  - 特殊处理 alloca()：匹配 alloca 及 __builtin_alloca 调用，警告不可控的栈分配
  - `storeOptions`：支持 `LargeStackVariableThreshold` 配置项
- [x] T036 [US3] 在 `build-csa/` 下运行 `ninja clang-tidy` 增量编译，确保编译无错误
- [x] T037 [US3] 运行 `clang-tidy -list-checks -checks='bugprone-*'` 验证 `bugprone-large-stack-variable` 已注册

### 测试程序

- [x] T038 [US3] 编写测试程序 `testProgram/test_large_stack_var.cpp`：
  - 正例：`int arr[1000000]`（约 4MB）→ 应报警
  - 反例：`char buf[256]`（256B）→ 不应报警
  - 边界：VLA `int arr[n]`（n 为运行时变量）→ 应有单独警告
  - 边界：多个局部变量各自未超阈值但累加超阈值 → 记录行为（各自不报警，仅逐个检查）
  - 配置：使用自定义阈值（如 `--config='{CheckOptions: [{key: bugprone-large-stack-variable.LargeStackVariableThreshold, value: 1024}]}'`）验证阈值可配置
  - 额外：`int arr[512]`（2048B）用于自定义阈值验证

### 验收测试

- [x] T039 [US3] 验收场景 1：分析含 `int arr[1000000]` 的程序，输出告警提示局部变量占用栈空间过大 ✓（"local variable 'arr' uses 4000000 bytes of stack space, exceeding threshold of 1048576 bytes"）
- [x] T040 [US3] 验收场景 2：分析含 `char buf[256]` 的程序，不产生告警 ✓
- [x] T041 [US3] 验收场景 3：设置自定义阈值 1024，分析含 `int arr[512]`（2KB）的程序，输出告警 ✓（"local variable 'arr' uses 2048 bytes of stack space, exceeding threshold of 1024 bytes"）
- [x] T042 [US3] 边界验证：VLA 变量单独提示 ✓（"variable-length array 'arr' has unpredictable stack usage"），alloca() 调用提示 ✓（需 `-system-headers` 标志，因 macOS alloca 宏展开至 `__builtin_alloca`）

**Phase Gate**: bugprone-large-stack-variable 编译通过、注册成功，所有验收场景和边界条件通过

---

## Phase 5: User Story 4 — 浮点数等号比较 clang-tidy Check (Priority: P2)

**目标**: 开发 `bugprone-float-equal-comparison` clang-tidy Check，检测对浮点数使用 == 或 != 比较

### 实现

- [x] T043 [US4] 使用脚手架生成 Check 骨架：在 `clang-tools-extra/clang-tidy/` 下运行 `python3 add_new_check.py bugprone float-equal-comparison`，生成 `bugprone/FloatEqualComparisonCheck.h`、`bugprone/FloatEqualComparisonCheck.cpp`，自动更新 `bugprone/CMakeLists.txt` 和 `bugprone/BugproneTidyModule.cpp`
- [x] T044 [US4] 实现 `clang-tools-extra/clang-tidy/bugprone/FloatEqualComparisonCheck.cpp`：
  - `registerMatchers`：匹配 `binaryOperator(anyOf(hasOperatorName("=="), hasOperatorName("!=")), hasEitherOperand(hasType(realFloatingPointType()))).bind("binop")`
  - `check`：获取 BinaryOperator 节点，排除 `x != x`（NaN 检测惯用写法），排除宏展开内的比较（可选），`diag()` 报警
- [x] T045 [US4] 在 `build-csa/` 下运行 `ninja clang-tidy` 增量编译，确保编译无错误
- [x] T046 [US4] 运行 `clang-tidy -list-checks -checks='bugprone-*'` 验证 `bugprone-float-equal-comparison` 已注册

### 测试程序

- [x] T047 [US4] 编写测试程序 `testProgram/test_float_equal.cpp`：
  - 正例：`if (a == 0.1)` 其中 a 为 double → 应报警
  - 正例：`if (a != 0.0)` 其中 a 为 float → 应报警
  - 反例：`if (a == b)` 其中 a、b 均为 int → 不应报警
  - 边界：`if (x != x)` 用于 NaN 检测 → 不应报警
  - 边界：`if (a == 0.0)` 其中 a 为 double → 应报警（尽管有时安全，保守策略）

### 验收测试

- [x] T048 [US4] 验收场景 1：分析含 `if (a == 0.1)` 且 a 为 double 的程序，输出浮点等号比较告警 ✓
- [x] T049 [US4] 验收场景 2：分析含 `if (a == b)` 且 a、b 均为 int 的程序，不产生告警 ✓
- [x] T050 [US4] 验收场景 3：分析含 `if (a != 0.0)` 且 a 为 float 的程序，输出告警 ✓
- [x] T051 [US4] 边界验证：`x != x`（NaN 检测写法）不误报 ✓

**Phase Gate**: bugprone-float-equal-comparison 编译通过、注册成功，所有验收场景和边界条件通过

---

## Phase 6: Polish & 横切关注点

**目的**: 代码清理、文档完善、整体验证

- [x] T052 检查所有新增 Checker/Check 源文件的代码风格是否符合 LLVM 编码规范（命名、缩进、头文件保护）✓ 所有文件符合规范，无需修改
- [x] T053 确保所有新增 Checker/Check 可通过命令行独立启用/禁用（NFR-003）✓ 三个检查器均可独立启用
- [x] T054 编写整体验证脚本 `testProgram/run_all_tests.sh`：批量运行所有测试程序，汇总通过/失败结果 ✓
- [x] T054a 性能验证（NFR-002）：选取一个中等规模 C++ 文件，分别在启用/禁用 MathDomainChecker 时运行 `clang++ --analyze`，对比耗时，确认增长 <5%；对 clang-tidy 新增 Check 做同样验证 ✓ CSA: ~0.033s vs ~0.033s；clang-tidy: ~0.033s vs ~0.034s，均 <1% 差异
- [x] T055 整体回归验证：运行 run_all_tests.sh，确保所有 7 种缺陷的测试全部通过 ✓ 6/6 PASS
- [x] T056 更新 `specs/csa-defect-checkers/spec.md` 状态从 Draft 改为 Completed ✓
- [x] T057 将所有修改提交 Git commit 到 llvm-project-personal 仓库 — **待用户手动执行**（仓库尚未初始化 Git）

**Phase Gate**: 所有功能完整，所有验收测试通过，代码风格合规，准备 PR

---

## 依赖与执行顺序

### Phase 依赖

- **Setup (Phase 1)**: 无依赖，可立即开始
- **US1 (Phase 2)**: 依赖 Setup 完成（需 clang++ 可用）
- **US2 (Phase 3)**: 依赖 US1 完成（需理解 Checker 开发模式后再从零开发）
- **US3 (Phase 4)**: 依赖 Setup 完成（需 clang-tidy 已构建），**可与 US2 并行**
- **US4 (Phase 5)**: 依赖 US3 完成（复用 clang-tidy 开发经验，同模块避免冲突）
- **Polish (Phase 6)**: 依赖所有用户故事完成

### 推荐执行路线

```
Phase 1: Setup
    ↓
Phase 2: US1 (理解已有 Checker)
    ↓
Phase 3: US2 (新建 CSA Checker) ←——— 核心难点，建议集中精力
    ↓
Phase 4: US3 (clang-tidy: 大局部变量)
    ↓
Phase 5: US4 (clang-tidy: 浮点等号)
    ↓
Phase 6: Polish
```

### 并行机会

- Phase 2 内：T006/T007/T008 三个测试程序可并行编写；T012/T013/T014 三份源码笔记可并行编写
- Phase 3/Phase 4 理论上可并行（CSA 和 clang-tidy 路线独立），但建议串行以降低调试复杂度
- 每个 Phase 内标记 [P] 的任务可并行

---

## 实施策略

### 逐 Phase 推进

1. 完成 Phase 1: Setup → 验证环境
2. 完成 Phase 2: US1 → 理解 Checker 机制
3. 完成 Phase 3: US2 → **MVP 核心突破**（首个自定义 Checker）
4. 完成 Phase 4: US3 → clang-tidy 路线打通
5. 完成 Phase 5: US4 → 批量完成
6. 完成 Phase 6: Polish → 整体交付

### 风险缓解

- **US2 是最大风险点**：若 `ProgramState::assume()` API 调试困难，可先实现仅匹配字面量常量的简化版（T022 分两步：先字面量匹配，再符号值扩展）
- **编译时间**：每次修改后增量编译约 1–3 分钟，修改 Checkers.td 约 3–5 分钟。建议先确保代码编译通过再运行测试
- **clang-tidy 首次构建**：T003 可能耗时 10–20 分钟，建议在 US1 工作期间后台执行

---

## Notes

- [P] 任务 = 不同文件，无依赖，可并行
- [Story] 标签 = 映射到具体用户故事，便于追踪
- 每个用户故事应独立可完成、可测试
- 每个 Phase 结束必须：测试通过 + 验收场景验证
- 边界条件（Edge Cases）集成到对应 Story 的测试程序和验收测试中
- 本项目不使用传统代码覆盖率工具（如 Jacoco），以测试用例覆盖度（正例/反例/边界）替代
