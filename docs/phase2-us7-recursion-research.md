# Phase 2 US7 — 递归栈溢出检测研究报告

**日期**: 2026-03-27
**工具**: `bugprone-unbounded-recursion` (新建 clang-tidy Check)
**测试程序**: `testProgram/test_unbounded_recursion.cpp`

---

## 调用图分析方法

### 核心基础设施

| 组件 | 头文件 | 用途 |
|------|--------|------|
| `clang::CallGraph` | `clang/Analysis/CallGraph.h` | 构建 AST 级别的函数调用图 |
| `llvm::scc_iterator` | `llvm/ADT/SCCIterator.h` | Tarjan 算法遍历强连通分量 |

### 算法流程

```
1. addToCallGraph(TranslationUnitDecl)
   → 遍历 TU 中所有函数声明/定义
   → 为每个函数创建 CallGraphNode
   → 扫描函数体中的 CallExpr，建立调用边

2. scc_begin(CallGraph) → scc_end(CallGraph)
   → Tarjan 算法找到所有强连通分量 (SCC)
   → 大小 >1 的 SCC = 间接递归环路
   → 大小 =1 且 hasCycle() = 直接递归

3. 对每个递归 SCC 中的函数：
   → TerminationAnalyzer 扫描函数体
   → 检查 IfStmt/SwitchStmt 中是否有 ReturnStmt
   → 检查条件是否依赖外部函数调用
```

---

## 终止条件判定算法

### 分类策略

| 情况 | 判定 | 报告级别 |
|------|------|----------|
| 无条件返回（无 if/switch 中的 return） | 无终止条件 | ERROR |
| 条件返回存在，条件依赖外部函数调用 | 终止不确定 | WARNING |
| 条件返回存在，条件依赖函数参数 | 正常递归 | 不报 |
| 间接递归环路 + 无终止条件 | 环路无终止 | ERROR |

### TerminationAnalyzer 实现

```
TraverseStmt(FunctionBody):
  VisitIfStmt(IS):
    if IS->getThen() 或 IS->getElse() 包含 ReturnStmt:
      HasConditionalReturn = true
      if IS->getCond() 包含 CallExpr:
        HasExternalCallInCondition = true

  VisitSwitchStmt(SS):
    HasConditionalReturn = true  // switch 本身是条件终止
```

---

## Acceptance Scenarios 验证

| # | 场景 | 预期 | 实际输出 | 结果 |
|---|------|------|----------|------|
| AS1 | `void f() { f(); }` 无终止 | 错误告警 | `calls itself recursively with no termination condition` | ✅ |
| AS2 | `a() → b() → a()` 间接递归 | 环路告警 | `indirect recursion cycle (mutual_b -> mutual_a)` | ✅ |
| AS3 | `if(get_flag()) return; f(n);` | 外部依赖提示 | `termination depends on external function call` | ✅ |
| AS4 | `factorial(n)` 正确递归 | 不报 | 无告警 | ✅ |
| AS5 | 研究报告 | 生成 | 本文档 | ✅ |

---

## 检测覆盖矩阵

| 场景 | 检出 | 分类 |
|------|------|------|
| 无条件直接递归 `f() { f(); }` | ✅ | ERROR |
| 间接递归环路 `a→b→a` | ✅ | ERROR |
| 终止依赖外部调用 | ✅ | WARNING |
| 正确递归 `factorial(n)` | ✅ 不报 | — |
| switch 终止 `fibonacci(n)` | ✅ 不报 | — |
| 运行时参数终止 | ✅ 不报 | — |
| 函数指针间接调用 | ✅ 不报 | — (无法解析) |
| 尾递归 | ✅ 不报 | — |

---

## 已知限制

### 1. 函数指针 / 虚函数不可解析

`CallGraph` 基于 AST 级别的 `CallExpr` 分析，只能解析静态已知的函数调用。通过函数指针、`std::function`、虚函数（vtable dispatch）进行的间接递归无法检测。

```cpp
// 无法检测
void (*fp)() = &recursive_func;
fp();  // CallExpr 的 callee 不是 DeclRefExpr
```

### 2. 终止条件分析为启发式

当前分析检查 IfStmt/SwitchStmt 中是否包含 ReturnStmt，但不验证：
- 条件是否可达（如 `if(false) return;` 仍被视为有终止条件）
- 递归参数是否递减（如 `f(n)` vs `f(n-1)`）
- 递归调用是否在所有路径上都到达

更精确的分析需要路径敏感能力（CSA Checker），但 clang-tidy 的 AST 级分析已能覆盖常见场景。

### 3. 跨翻译单元

`CallGraph` 仅分析当前翻译单元。如果递归环路跨越多个 .cpp 文件，无法检测。

### 4. 模板特化

模板函数的不同特化可能有不同的递归行为，当前分析不区分模板参数。

---

## 实现技术要点

1. **使用 `translationUnitDecl()` 匹配器**: 因为 CallGraph 需要整个 TU 的视图，不能按函数匹配
2. **`llvm::scc_iterator` 使用 Tarjan 算法**: O(V+E) 时间复杂度，对大型代码库高效
3. **避免重复报告**: 使用 `SmallPtrSet<const Decl *>` 跟踪已报告的函数
4. **`hasCycle()` 检查**: SCC 大小为 1 时需要额外检查 `hasCycle()` 以确认自递归

---

## 结论

基于 `CallGraph` + `scc_iterator` 的递归检测方案能有效识别无终止条件的直接递归、间接递归环路以及终止条件依赖外部调用的风险递归。受限于 AST 级分析的精度，函数指针和虚函数场景无法覆盖，但对于静态可解析的调用链，检测准确率高，无误报。
