# DivZeroChecker 源码分析

**源文件**: `clang/lib/StaticAnalyzer/Checkers/DivZeroChecker.cpp`  
**Checker 全名**: `core.DivideZero` / `alpha.security.taint.TaintedDivChecker`

---

## 1. 继承结构

```cpp
class DivZeroChecker : public CheckerFamily<check::PreStmt<BinaryOperator>>
```

**注意**：当前代码库使用 `CheckerFamily<...>` 而非旧版 `Checker<...>`。`CheckerFamily` 允许一个 C++ 类承载多个用户可见的 checker 前端（通过 `CheckerFrontendWithBugType` 成员）。

该类包含两个前端：
- `DivideZeroChecker` — 标准除零检测（`core.DivideZero`）
- `TaintedDivChecker` — 污染数据导致的除零（`alpha.security.taint.TaintedDiv`）

每个前端通过 `isEnabled()` 独立控制是否启用。

## 2. checkPreStmt 回调机制

`checkPreStmt(const BinaryOperator *B, CheckerContext &C)` 在二元运算符**求值前**被调用。

**执行流程**：

1. **过滤操作符**：仅处理 `BO_Div`、`BO_Rem`、`BO_DivAssign`、`BO_RemAssign`
2. **类型检查**：确认右操作数为标量类型
3. **获取除数符号值**：`SVal Denom = C.getSVal(B->getRHS())`，尝试转换为 `DefinedSVal`
4. **约束求解**：通过 `ConstraintManager::assumeDual()` 将状态分叉为 `stateNotZero` 和 `stateZero`
5. **判定逻辑**：
   - `stateNotZero == nullptr`（除数必定为零）→ 报告 bug
   - 两者都存在 → 检查是否为污染数据，若是则报告 taint bug
   - 最终转移到 `stateNotZero` 状态（即假设除数非零继续分析）

## 3. BugType 注册模式

```cpp
CheckerFrontendWithBugType DivideZeroChecker{"Division by zero"};
CheckerFrontendWithBugType TaintedDivChecker{"Division by zero", categories::TaintedData};
```

`CheckerFrontendWithBugType` 既是 `CheckerFrontend`（用户可见的 checker 标识），又自带一个 `BugType`。这是 `CheckerFamily` 模式下的惯用做法——无需单独声明 `BugType` 成员。

## 4. ConstraintManager 使用

```cpp
ConstraintManager &CM = C.getConstraintManager();
ProgramStateRef stateNotZero, stateZero;
std::tie(stateNotZero, stateZero) = CM.assumeDual(C.getState(), *DV);
```

`assumeDual(State, V)` 同时求解 `V ≠ 0` 和 `V == 0` 两种状态：
- 返回 `{非零状态, 零状态}`
- 若某状态不可行则返回 `nullptr`
- **与 `ProgramState::assume()` 的关系**：`assumeDual` 是更高效的写法，等价于分别调用 `State->assume(V, true)` 和 `State->assume(V, false)`

## 5. BugReport 生成

```cpp
void reportBug(StringRef Msg, ProgramStateRef StateZero, CheckerContext &C) const {
    if (!DivideZeroChecker.isEnabled()) return;
    if (ExplodedNode *N = C.generateErrorNode(StateZero)) {
        auto R = std::make_unique<PathSensitiveBugReport>(DivideZeroChecker, Msg, N);
        bugreporter::trackExpressionValue(N, getDenomExpr(N), *R);
        C.emitReport(std::move(R));
    }
}
```

关键步骤：
1. `C.generateErrorNode(StateZero)` — 在 ExplodedGraph 上生成错误节点（sink node，停止该路径的后续分析）
2. `PathSensitiveBugReport` — 路径敏感 bug 报告，附带 ExplodedGraph 路径信息
3. `trackExpressionValue()` — 追踪除数表达式的值来源，用于生成诊断路径说明
4. `C.emitReport()` — 提交报告

## 6. 注册模式

```cpp
void ento::registerDivZeroChecker(CheckerManager &Mgr) {
    Mgr.getChecker<DivZeroChecker>()->DivideZeroChecker.enable(Mgr);
}
bool ento::shouldRegisterDivZeroChecker(const CheckerManager &) { return true; }
```

`CheckerFamily` 模式下的注册方式：
- `Mgr.getChecker<T>()` 获取（或首次创建）checker 实例
- 通过 `.enable(Mgr)` 启用特定前端
- `shouldRegister` 返回 `true` 表示无条件可注册

这意味着同一 `DivZeroChecker` 实例被两个 checker 共享，`registerTaintedDivChecker` 仅启用其 `TaintedDivChecker` 前端。

## 7. Phase 3 复用要点

| 模式 | DivZeroChecker 中的用法 | MathDomainChecker 可复用 |
|------|----------------------|----------------------|
| 基类 | `CheckerFamily<check::PreStmt<BinaryOperator>>` | 改用 `Checker<check::PreCall>` 或 `CheckerFamily<check::PreCall>`，因为需要匹配函数调用 |
| 约束求解 | `CM.assumeDual(State, DV)` | 改用 `State->assume(比较表达式)` 判断参数是否满足域约束 |
| Bug 报告 | `PathSensitiveBugReport` + `trackExpressionValue` | 同样使用，追踪参数表达式 |
| 注册 | `register/shouldRegister` 函数对 | 相同模式 |
| `getDebugTag()` | 返回 checker 名 | 需要实现（`CheckerFamily` 要求） |
