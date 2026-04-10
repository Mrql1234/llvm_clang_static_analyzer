# Phase 2 US6 — 死循环检测研究报告

**日期**: 2026-03-27（增强更新: 2026-03-27）
**工具**: `bugprone-infinite-loop` (已有) + `bugprone-loop-external-dependency` (新建+增强)
**测试程序**: `testProgram/test_loop_external_dep.cpp`

---

## 已有 bugprone-infinite-loop 覆盖分析

### 检测原理

`bugprone-infinite-loop` 使用 `ExprMutationAnalyzer` 分析循环条件中引用的变量，检查它们是否在循环体中被修改。核心逻辑：

1. 提取循环条件中的所有 `DeclRefExpr`（变量引用）
2. 使用 `ExprMutationAnalyzer` 检查每个变量在循环体中是否有可能被修改
3. 如果所有条件变量都未被修改 → 报告死循环

### 已有覆盖范围

| 场景 | 是否检出 | 说明 |
|------|----------|------|
| `while(x>0) { y++; }` 条件变量未修改 | ✅ | 核心检测能力 |
| `while(true)` / `for(;;)` 无 break | ❌ | 无条件变量可分析；由 `loop-external-dependency` 补充检测 |
| 条件变量通过外部函数修改 | ❌ | `ExprMutationAnalyzer` 可能视函数调用为潜在修改 |
| volatile 循环条件 | ✅ 不报 | 正确排除硬件场景 |

---

## 新建 bugprone-loop-external-dependency 设计原理

### 定位

补充 `bugprone-infinite-loop` 未覆盖的场景：循环退出条件完全依赖外部函数调用，以及常量真条件（`while(true)` / `for(;;)`）无退出路径的死循环。

### 检测算法

```
0. 常量真条件检测（增强）：
   a. 判断条件是否为常量真(while(true)/while(1)) — 使用 EvaluateAsBooleanCondition
   b. 判断条件是否缺失(for(;;)) — Cond 为 null
   c. 若为常量真/缺失条件：
      - 使用 ExitPathChecker（RecursiveASTVisitor）遍历循环体
      - 查找 break（仅当前循环层级，不进入嵌套循环/switch）
      - 查找 return / goto / throw（任何层级）
      - 无退出路径 → 报 "constant true, no exit path — infinite loop"
      - 有退出路径 → 有意设计模式（事件循环等），不报
      - 提前返回，不再执行后续外部依赖检查

1. 匹配 whileStmt / forStmt / doStmt
2. 分析条件表达式：
   a. 收集条件中的 DeclRefExpr（条件变量）和 CallExpr
   b. 如果条件是纯函数调用（无变量引用）→ 报 "exit depends on external call"
3. 如果条件引用变量：
   a. 排除 volatile 变量 → 不报
   b. 检查变量是否在循环体/for增量中被直接赋值/递增
   c. 如果有直接修改 → 正常循环，不报
   d. 如果无直接修改但体内有函数调用 → 报 "depends on external calls"
```

### 与 bugprone-infinite-loop 的关系

两个 Check 互补，不重叠：

| 场景 | infinite-loop | loop-external-dependency |
|------|--------------|--------------------------|
| 条件变量完全未修改、无函数调用 | ✅ 报 | ❌ 不报 |
| 条件变量未修改、有外部函数调用 | ❌ 不报（函数可能修改） | ✅ 报提示 |
| 条件为纯外部函数调用 | ❌ 不报 | ✅ 报提示 |
| **常量真条件无退出路径** | ❌ 不报 | **✅ 报死循环** |
| **常量真条件有退出路径** | ❌ 不报 | **❌ 不报（有意设计）** |
| 条件变量直接修改 | ❌ 不报 | ❌ 不报 |
| volatile 条件 | ❌ 不报 | ❌ 不报 |

---

## 检测覆盖范围矩阵

| 测试场景 | infinite-loop | ext-dependency | 结果 |
|----------|--------------|----------------|------|
| while(true) 无 break | — | ✅ | **报死循环（增强后）** |
| for(;;) 无 break | — | ✅ | **报死循环（增强后）** |
| while(true) 有 break | — | — | 不报（有意设计） |
| 条件变量未修改 | ✅ | — | 报死循环 |
| 条件为外部函数调用 | — | ✅ | 提示审查 |
| 正常 for 循环 | — | — | 不报 |
| volatile 事件循环 | — | — | 不报 |
| do-while 外部条件 | — | ✅ | 提示审查 |
| 不可达 break + 外部调用 | — | ✅ | 提示审查 |
| 嵌套循环内部修改外部变量 | — | — | 不报 |
| 直接赋值修改条件变量 | — | — | 不报 |

---

## 已知限制（停机问题的本质约束）

1. **停机问题不可判定**: 静态分析无法确定任意循环是否终止，只能检测常见模式
2. **`while(true)` / `for(;;)` 有退出路径**: 这些是 C/C++ 中常见的有意设计模式（服务器主循环、事件循环），检查器正确地不报告
3. **间接修改**: 如果条件变量通过指针或引用被函数间接修改，检查器无法跟踪
4. **多线程**: 条件变量可能被其他线程修改（如事件循环中的 `running` 标志），静态分析无法感知
5. **提示性 vs 确定性**: `bugprone-loop-external-dependency` 的外部依赖输出是提示，而非确定性错误——循环可能是正确的事件驱动设计
6. **ExitPathChecker 的保守性**: 只检查语法层面的退出语句，不分析退出条件的可达性（如 `if(false) break;` 会被视为有退出路径）

---

## 结论

通过 `bugprone-infinite-loop`（已有）和 `bugprone-loop-external-dependency`（新建+增强）的组合，实现了对死循环缺陷（#3）的实用覆盖：

- 已有 Check 处理条件变量确定未修改的场景
- 新 Check 补充了条件依赖外部调用的提示
- **增强后新增**：常量真条件（`while(true)` / `for(;;)`）无退出路径的死循环检测

受停机问题理论限制，无法做到完全覆盖，但常见模式均已处理。
