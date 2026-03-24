# DereferenceChecker 源码分析

**源文件**: `clang/lib/StaticAnalyzer/Checkers/DereferenceChecker.cpp`  
**Checker 全名**: `core.NullDereference` / `core.FixedAddr` / `core.NullPointerArithm`

---

## 1. 继承结构

```cpp
class DereferenceChecker
    : public CheckerFamily<check::Location, check::Bind,
                           check::PreStmt<BinaryOperator>,
                           EventDispatcher<ImplicitNullDerefEvent>>
```

同样使用 `CheckerFamily` 模式，注册了三种回调：
- **`check::Location`** — 在内存位置被加载/存储时触发（核心检测点）
- **`check::Bind`** — 在值绑定到内存位置时触发（用于检查引用绑定）
- **`check::PreStmt<BinaryOperator>`** — 检查指针加法/减法中的空指针

此外还包含 `EventDispatcher<ImplicitNullDerefEvent>`，用于向其他 checker 广播隐式空指针解引用事件。

## 2. 空指针检测的路径敏感分析流程

核心检测在 `checkLocation` 中：

```cpp
void checkLocation(SVal l, bool isLoad, const Stmt *S, CheckerContext &C) const
```

**流程**：

1. **检查 undefined 值**：`l.isUndef()` → 报告 `UndefBug`
2. **转换为 DefinedOrUnknownSVal**：`l.castAs<DefinedOrUnknownSVal>()`
3. **确认是位置类型**：`isa<Loc>(location)`
4. **约束分叉**：
   ```cpp
   std::tie(notNullState, nullState) = state->assume(location);
   ```
   - `ProgramState::assume(DefinedOrUnknownSVal)` 将状态分为两支：值为零/值非零
5. **判定逻辑**：
   - `nullState && !notNullState` → 指针**必定为 null**（显式空指针解引用）→ 报告 `NullBug`
   - `nullState && notNullState` → 指针**可能为 null** → 生成 sink node 并广播 `ImplicitNullDerefEvent`
   - 固定地址检查：`location.isConstant()` → 报告 `FixedAddressBug`
6. **状态转移**：`C.addTransition(notNullState)` — 后续分析假设指针非 null

## 3. 跨过程分析

CSA 默认支持跨过程（interprocedural）分析——`foo()` 调用 `bar(nullptr)` 时，分析器会内联 `bar` 的函数体，在 `bar` 内部检测到 `*p` 时 `p` 已经被约束为 null。

从测试结果可以看到分析器输出：
```
note: Passing null pointer value via 1st parameter 'p'
note: Calling 'bar'
note: Dereference of null pointer (loaded from variable 'p')
```

这说明跨过程分析不需要 checker 侧做特殊处理——ExplodedGraph 的符号执行引擎自动跟踪函数调用链。

## 4. BugType 定义

该 checker 使用自定义 `DerefBugType`（继承 `BugType`），额外存储了数组访问和字段访问的消息模板：

```cpp
class DerefBugType : public BugType {
    StringRef ArrayMsg, FieldMsg;
    // ...
};
```

三个 `CheckerFrontend` 对应三个用户可见的 checker：
- `NullDerefChecker` → `core.NullDereference`
- `FixedDerefChecker` → `core.FixedAddr`
- `NullPointerArithmChecker` → `core.NullPointerArithm`

## 5. BugReport 生成

```cpp
void reportDerefBug(const DerefBugType &BT, ProgramStateRef State,
                    const Stmt *S, CheckerContext &C) const {
    ExplodedNode *N = C.generateErrorNode(State);
    // 构建诊断消息（根据 Stmt 类型定制）
    auto BR = std::make_unique<PathSensitiveBugReport>(BT, Msg, N);
    bugreporter::trackExpressionValue(N, bugreporter::getDerefExpr(S), *BR);
    C.emitReport(std::move(BR));
}
```

特殊之处：根据触发语句类型（`ArraySubscriptExpr`、`UnaryOperator`、`MemberExpr`、`ObjCIvarRefExpr`）生成不同的错误消息。

## 6. ProgramState::assume() 的使用

```cpp
ProgramStateRef notNullState, nullState;
std::tie(notNullState, nullState) = state->assume(location);
```

`assume(DefinedOrUnknownSVal V)` 是 `assume(V, true)` + `assume(V, false)` 的便捷版本：
- 返回 `{V 为真的状态, V 为假的状态}`
- 对于指针：真 = 非 null，假 = null
- 任一状态不可行时为 `nullptr`

## 7. 注册模式

```cpp
void ento::registerNullDereferenceChecker(CheckerManager &Mgr) {
    Mgr.getChecker<DereferenceChecker>()->NullDerefChecker.enable(Mgr);
}
bool ento::shouldRegisterNullDereferenceChecker(const CheckerManager &) { return true; }
```

与 DivZeroChecker 完全一致的 `CheckerFamily` 注册模式。

## 8. Phase 3 复用要点

| 关注点 | DereferenceChecker 用法 | MathDomainChecker 参考 |
|--------|------------------------|---------------------|
| `assume` 用法 | `state->assume(location)` 分叉 null/非null | 类似地对参数值分叉：`assume(arg < 0)` |
| 多前端支持 | 三个 `CheckerFrontend` 由同一类管理 | MathDomainChecker 可以只用一个简单的 `BugType` |
| 错误消息定制 | 按 Stmt 类型定制 | 按函数名定制（sqrt vs asin/acos） |
| 事件广播 | `ImplicitNullDerefEvent` | 不需要 |
