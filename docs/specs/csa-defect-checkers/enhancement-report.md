# 增强实施报告: E1 / E2 / E3

**Workspace**: `csa-defect-checkers`
**Date**: 2026-03-17
**基线**: MVP 已完成（commit d19f99eb2）

---

## 1. 概述

基于 `enhancement-summary.md` 中定义的三项增强方案，全部实现并通过测试：

| 编号 | 名称 | 类型 | 注册名 | 状态 |
|------|------|------|--------|------|
| E1 | 可能除以零 | CSA Checker | `alpha.core.PossibleDivideZero` | ✅ 完成 |
| E2 | 可能数组越界 | CSA Checker | `alpha.security.PossibleArrayBound` | ✅ 完成 |
| E3 | 数学函数域保护 | clang-tidy Check | `bugprone-math-domain-guard` | ✅ 完成 |

---

## 2. E1: `alpha.core.PossibleDivideZero`

**源文件**: `clang/lib/StaticAnalyzer/Checkers/PossibleDivZeroChecker.cpp`

**设计**: 基于 `DivZeroChecker` 的变体。核心区别在于判断条件：当 `assumeDual` 返回 `stateNotZero && stateZero`（即除数可能为零也可能非零）时报警，而非仅在 `!stateNotZero` 时报。使用 `generateNonFatalErrorNode` 产生 warning 级别报告，不中断后续分析。跳过浮点除法和确定为零的情况（后者交给 `core.DivideZero`）。

### 验收结果

| # | 场景 | 代码 | 预期 | 结果 |
|---|------|------|------|------|
| 1 | 外部输入除法 | `100 / get_value()` | WARN | ✅ |
| 2 | 外部输入取模 | `100 % get_value()` | WARN | ✅ |
| 3 | 已验证非零 | `if (d != 0) 100/d` | 不报 | ✅ |
| 4 | 条件赋值部分路径为零 | `d=0; if(c()) d=get(); 100/d` | WARN | ✅ |
| 5 | 字面量非零除数 | `100 / 7` | 不报 | ✅ |
| 6 | 字面量零（交给 DivZero） | `100 / 0` | 不报(本 Checker) | ✅ |

---

## 3. E2: `alpha.security.PossibleArrayBound`

**源文件**: `clang/lib/StaticAnalyzer/Checkers/PossibleArrayBoundChecker.cpp`

**设计**: 聚焦 `ArraySubscriptExpr`，分别执行下界和上界检查。下界检查：对有符号整型下标，判断是否可能为负。上界检查：将数组 extent（字节）转换为元素个数后，直接与下标符号值比较（`index >= elemCount`），避免乘法导致约束求解器无法传播路径约束的问题。确定越界的情况跳过（交给 `security.ArrayBound`）。

### 验收结果

| # | 场景 | 代码 | 预期 | 结果 |
|---|------|------|------|------|
| 1 | 外部输入做下标 | `arr[get_value()]` | WARN | ✅ "may be negative" |
| 2 | 确定负值下标 | `int i=-1; arr[i]` | 跳过 | ✅ 交给 ArrayBound |
| 3 | 完整范围验证 | `if (i>=0 && i<10) arr[i]` | 不报 | ✅ |
| 4 | 仅下界验证 | `if (i>=0) arr[i]` | WARN | ✅ "may exceed upper bound" |
| 5 | 字面量合法下标 | `arr[5]` | 不报 | ✅ |
| 6 | 循环变量 | `for(i=0;i<10) arr[i]` | 不报 | ✅ |

**实现中修复的问题**: 初始版本将下标转换为字节偏移 (`index * sizeof(int)`) 与 extent 比较，导致约束求解器无法利用已有的 `index < 10` 约束来推导 `index * 4 < 40`，产生误报。修复方案：将 extent 转换为元素个数 (`extent / sizeof(int)`)，直接与下标比较 (`index >= elemCount`)，使约束求解器能正确传播。

---

## 4. E3: `bugprone-math-domain-guard`

**源文件**:
- `clang-tools-extra/clang-tidy/bugprone/MathDomainGuardCheck.h`
- `clang-tools-extra/clang-tidy/bugprone/MathDomainGuardCheck.cpp`

**设计**: 基于 AST 模式匹配，检查 `sqrt`/`asin`/`acos` 调用前是否有域保护。向上遍历 AST（最多 8 层）查找包含调用的 `IfStmt`，分析条件表达式是否构成正向保护（参数在合法域内）或反向保护（参数在违反域中）。支持的保护模式：

- 直接 `if` 保护: `if (x >= 0) sqrt(x)`
- 否定条件: `if (!(x < 0)) sqrt(x)`
- else 分支: `if (x < 0) ... else sqrt(x)`
- `&&` 组合: `if (x >= -1 && x <= 1) acos(x)`
- `fabs` 保护: `sqrt(fabs(x))`
- 编译期常量跳过: `sqrt(4.0)` 交给 `alpha.security.MathDomain`

### 验收结果

| # | 场景 | 代码 | 预期 | 结果 |
|---|------|------|------|------|
| 1 | 反向保护 | `if (x<0) sqrt(x)` | WARN | ✅ "guaranteed to violate" |
| 2 | 无保护 | `sqrt(x)` | WARN | ✅ "not validated" |
| 3 | 正向保护 `>=0` | `if (x>=0) sqrt(x)` | 不报 | ✅ |
| 4 | 正向保护 `>0` | `if (x>0) sqrt(x)` | 不报 | ✅ |
| 5 | fabs 保护 | `sqrt(fabs(x))` | 不报 | ✅ |
| 6 | 编译期常量 | `sqrt(4.0)` | 不报 | ✅ |
| 7 | asin 无保护 | `asin(x)` | WARN | ✅ "not validated [-1,1]" |
| 8 | acos 范围保护 | `if (x>=-1 && x<=1) acos(x)` | 不报 | ✅ |
| 9 | 否定条件 | `if (!(x<0)) sqrt(x)` | 不报 | ✅ |
| 10 | else 分支 | `if (x<0) return; else sqrt(x)` | 不报 | ✅ |
| 11 | 复杂表达式 | `sqrt(a+b)` | WARN | ✅ |

---

## 5. 新增/修改文件清单

| 操作 | 文件路径 |
|------|---------|
| 新建 | `clang/lib/StaticAnalyzer/Checkers/PossibleDivZeroChecker.cpp` |
| 新建 | `clang/lib/StaticAnalyzer/Checkers/PossibleArrayBoundChecker.cpp` |
| 修改 | `clang/include/clang/StaticAnalyzer/Checkers/Checkers.td` |
| 修改 | `clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt` |
| 新建 | `clang-tools-extra/clang-tidy/bugprone/MathDomainGuardCheck.h` |
| 新建 | `clang-tools-extra/clang-tidy/bugprone/MathDomainGuardCheck.cpp` |
| 修改 | `clang-tools-extra/clang-tidy/bugprone/BugproneTidyModule.cpp` |
| 修改 | `clang-tools-extra/clang-tidy/bugprone/CMakeLists.txt` |
| 新建 | `testProgram/test_possible_div_zero.cpp` |
| 新建 | `testProgram/test_possible_array_bound.cpp` |
| 新建 | `testProgram/test_math_domain_guard.cpp` |
| 新建 | `specs/csa-defect-checkers/enhancement-summary.md` |
| 更新 | `specs/csa-defect-checkers/除以零.md` |
| 更新 | `specs/csa-defect-checkers/数组越界.md` |
| 更新 | `specs/csa-defect-checkers/数学域.md` |

---

## 6. 使用方式

```bash
CLANG="build-csa/bin/clang++"
CLANG_TIDY="build-csa/bin/clang-tidy"
SDK_PATH="$(xcrun --show-sdk-path)"

# E1: 可能除以零
$CLANG --analyze -isysroot "$SDK_PATH" -I"$SDK_PATH/usr/include/c++/v1" \
  -Xanalyzer -analyzer-checker=alpha.core.PossibleDivideZero \
  target.cpp

# E2: 可能数组越界
$CLANG --analyze -isysroot "$SDK_PATH" -I"$SDK_PATH/usr/include/c++/v1" \
  -Xanalyzer -analyzer-checker=alpha.security.PossibleArrayBound \
  target.cpp

# E3: 数学函数域保护
$CLANG_TIDY -checks='-*,bugprone-math-domain-guard' \
  target.cpp -- -isysroot "$SDK_PATH" -I"$SDK_PATH/usr/include/c++/v1"
```

---

## 7. 已知限制

- **E1**: 对所有未经零值检查的除法都会报警，误报率中等。以 `alpha.` 前缀注册，用户可选择性启用。
- **E2**: 仅覆盖 `ArraySubscriptExpr`，不处理指针算术 `*(p+i)` 和成员访问。误报率中等偏高。
- **E3**: 仅识别直接 `if` 包裹、否定条件、else 分支、`fabs` 等保护模式。不识别 early return 模式（`if (x<0) return; sqrt(x);`）和 assert 模式。后续可增强。
