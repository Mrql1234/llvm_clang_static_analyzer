# ArrayBoundChecker 源码分析

**源文件**: `clang/lib/StaticAnalyzer/Checkers/ArrayBoundChecker.cpp`  
**Checker 全名**: `security.ArrayBound`

---

## 1. 继承结构

```cpp
class ArrayBoundChecker : public Checker<check::PostStmt<ArraySubscriptExpr>,
                                         check::PostStmt<UnaryOperator>,
                                         check::PostStmt<MemberExpr>>
```

**注意**：与 DivZeroChecker 和 DereferenceChecker 不同，ArrayBoundChecker 使用**旧版** `Checker<...>` 模板而非 `CheckerFamily`。这说明两种模式在当前代码库中并存。

使用 `PostStmt` 而非 `PreStmt` 的原因：当前实现需要调用 `C.getSVal(E)` 获取整个表达式的值，而该值在 `PostStmt` 阶段才可用。

三个回调入口最终都调用 `performCheck(const Expr *E, CheckerContext &C)`。

## 2. 内存区域与偏移量计算

核心函数 `computeOffset` 将一个内存位置 SVal 分解为**基础区域**和**字节偏移量**：

```cpp
static std::optional<std::pair<const SubRegion *, NonLoc>>
computeOffset(ProgramStateRef State, SValBuilder &SVB, SVal Location)
```

**计算过程**：

1. 从 `Location` 获取 `ElementRegion`
2. 逐层遍历嵌套的 `ElementRegion`（处理多维数组 `arr[i][j]`）
3. 每层计算 `Delta = Index * sizeof(ElemType)`
4. 累加 `Offset += Delta`
5. 返回 `{OwnerRegion（基础区域）, ByteOffset（总字节偏移）}`

## 3. 越界检测流程

`performCheck` 分两步检查：

### 3.1 下界检查（负数偏移）

```cpp
auto [PrecedesLowerBound, WithinLowerBound] = compareValueToThreshold(
    State, ByteOffset, SVB.makeZeroArrayIndex(), SVB);
```

- 判断 `ByteOffset < 0` 是否成立
- 若偏移必定为负 → 报告越界
- 若可能为负也可能非负 → 假设非负继续分析，记录假设

### 3.2 上界检查（超出范围）

```cpp
DefinedOrUnknownSVal Size = getDynamicExtent(State, Reg, SVB);
auto [WithinUpperBound, ExceedsUpperBound] =
    compareValueToThreshold(State, ByteOffset, *KnownSize, SVB);
```

- `getDynamicExtent` 获取区域的动态大小（支持 VLA 和 malloc 分配）
- 判断 `ByteOffset >= Size` 是否成立
- 若偏移必定越界 → 报告越界
- 若越界但数据被污染 → 报告 taint bug

### 3.3 简化算法

`getSimplifiedOffsets` 对偏移量表达式进行数学简化：
- `(x * 4) < 40` → `x < 10`（除法简化）
- `(x + 4) < 40` → `x < 36`（减法简化）

这种简化在纯数学意义上正确，但在 C++ 溢出语义下可能不精确。仅适用于内存偏移这种远小于 SIZE_MAX 的场景。

## 4. compareValueToThreshold

```cpp
static std::pair<ProgramStateRef, ProgramStateRef>
compareValueToThreshold(ProgramStateRef State, NonLoc Value, NonLoc Threshold,
                        SValBuilder &SVB, bool CheckEquality = false)
```

这是一个通用的值比较工具：
1. 先应用 `getSimplifiedOffsets` 简化
2. 处理符号类型不匹配的边界情况（负数 vs 无符号）
3. 通过 `SVB.evalBinOpNN` 求值比较运算
4. 返回 `{满足条件的状态, 不满足的状态}`

## 5. BugType 与 BugReport

```cpp
BugType BT{this, "Out-of-bound access"};
BugType TaintBT{this, "Out-of-bound access", categories::TaintedData};
```

使用基础 `BugType`（非 `CheckerFrontendWithBugType`），因为 ArrayBoundChecker 用的是旧版 `Checker<...>` 模式。

报告生成：

```cpp
void reportOOB(CheckerContext &C, ProgramStateRef ErrorState, Messages Msgs,
               NonLoc Offset, std::optional<NonLoc> Extent, bool IsTaintBug) const {
    ExplodedNode *ErrorNode = C.generateErrorNode(ErrorState);
    auto BR = std::make_unique<PathSensitiveBugReport>(
        IsTaintBug ? TaintBT : BT, Msgs.Short, Msgs.Full, ErrorNode);
    markPartsInteresting(*BR, ErrorState, Offset, IsTaintBug);
    C.emitReport(std::move(BR));
}
```

`markPartsInteresting` 将偏移量表达式中的所有符号标记为 interesting，使路径诊断能追踪这些符号的来源。

## 6. 注册模式

```cpp
void ento::registerArrayBoundChecker(CheckerManager &mgr) {
    mgr.registerChecker<ArrayBoundChecker>();
}
bool ento::shouldRegisterArrayBoundChecker(const CheckerManager &mgr) { return true; }
```

旧版 `Checker<...>` 的注册方式更简洁：直接 `registerChecker<T>()` 即可。无需管理 `CheckerFrontend` 的启用。

## 7. Phase 3 复用要点

| 模式 | ArrayBoundChecker 用法 | MathDomainChecker 参考 |
|------|----------------------|---------------------|
| 基类选择 | `Checker<check::PostStmt<...>>` | 若只有一个 BugType，可用 `Checker<check::PreCall>` |
| 值比较 | `compareValueToThreshold` + `getSimplifiedOffsets` | 域检查需要 `assume(arg >= 0)` 和 `assume(arg <= 1)` |
| 动态大小 | `getDynamicExtent` | 不需要（参数域是固定常量） |
| BugType | `BugType{this, "..."}` | 同样方式，可直接构造 |
| 注册 | `registerChecker<T>()` | 若用 `Checker<...>` 可直接使用此方式 |

## 8. 两种基类模式对比（关键）

| | `Checker<check::XXX>` | `CheckerFamily<check::XXX>` |
|--|----------------------|---------------------------|
| 用户可见 checker 数 | 1 个 | 多个（通过 `CheckerFrontend`） |
| BugType 声明 | `BugType{this, ...}` | `CheckerFrontendWithBugType{...}` |
| 注册方式 | `registerChecker<T>()` | `getChecker<T>()->Frontend.enable(Mgr)` |
| `getDebugTag()` | 不需要 | **必须实现** |
| 当前代码中 | ArrayBoundChecker | DivZeroChecker, DereferenceChecker |

**建议**：MathDomainChecker 只需一个用户可见 checker (`alpha.security.MathDomain`)，可选择用 `Checker<check::PreCall>` 更简洁。但若未来想拆分 sqrt 和 asin/acos 为独立 checker，则应用 `CheckerFamily`。
