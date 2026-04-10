# Phase 2 US1 — 未初始化变量检测能力验证报告

**日期**: 2026-03-27
**工具**: `clang++ --analyze` (Clang 23.0.0git)
**测试程序**: `testProgram/test_uninitialized.cpp`

---

## 验证的 Checker 清单

| Checker | 功能 | 验证结果 |
|---------|------|----------|
| `core.uninitialized.Assign` | 赋值未初始化值 | ✅ 检出 |
| `core.uninitialized.Branch` | 未初始化值用作分支条件 | ✅ 检出（通过 `core.UndefinedBinaryOperatorResult`） |
| `core.uninitialized.UndefReturn` | 返回未初始化值 | ✅ 检出 |
| `core.uninitialized.ArraySubscript` | 未初始化值用作数组下标 | ✅ 检出 |
| `core.uninitialized.NewArraySize` | new[] 大小未初始化 | ✅ 检出 |
| `core.uninitialized.CapturedBlockVariable` | Block 捕获未初始化变量 | ⬜ 未测试（macOS Block 特性） |

---

## Acceptance Scenarios 验证

| # | 场景 | 预期 | 实际输出 | 结果 |
|---|------|------|----------|------|
| AS1 | 未赋值直接使用 | `core.uninitialized.*` 告警 | `core.uninitialized.Assign` (line 11) | ✅ 通过 |
| AS2 | 未初始化变量用作 if 条件 | `core.uninitialized.Branch` 告警 | `core.UndefinedBinaryOperatorResult` (line 18) | ✅ 通过 |
| AS3 | 所有路径已初始化 | 不应产生告警 | 无告警 | ✅ 通过 |
| AS4 | 汇总所有场景 | 记录通过/未通过 | 9/9 告警正确检出 | ✅ 通过 |

---

## 边界条件验证

| 场景 | 预期 | 实际输出 | 结果 |
|------|------|----------|------|
| 变量在部分路径初始化 | 应告警 | `core.uninitialized.UndefReturn` (line 71) | ✅ |
| 结构体成员部分初始化 | 应告警 | `core.UndefinedBinaryOperatorResult` (line 85) | ✅ |
| 数组元素未初始化 | 应告警 | `core.uninitialized.UndefReturn` (line 94) | ✅ |
| 通过指针间接传递 | 应告警 | `core.uninitialized.UndefReturn` (line 101) | ✅ |

---

## 检测原理说明

`core.uninitialized.*` 系列 Checker 是 CSA 核心内置检查器，运行在 ExplodedGraph（路径敏感符号执行图）之上：

1. **符号执行**: CSA 对每个函数路径进行符号模拟，跟踪每个变量的初始化状态
2. **路径敏感**: 能够区分不同分支路径上的变量状态（如 `test_all_paths_initialized` 正确判断所有路径都已初始化）
3. **传播追踪**: 未初始化状态通过赋值和指针传递进行传播（如 `test_pointer_indirect`）

### AS2 说明

对于"未初始化变量用作 if 条件"的场景，CSA 报告了 `core.UndefinedBinaryOperatorResult` 而非 `core.uninitialized.Branch`。这是因为 CSA 在分析 `x > 0` 表达式时，先检测到操作数为垃圾值（比 Branch 检查更早触发）。两者都检测到了同一问题，属于正常的 Checker 优先级行为。

---

## 已知限制

1. **`CapturedBlockVariable`**: 仅适用于 Objective-C/C++ 的 Block 特性，本次未测试
2. **跨函数边界**: CSA 的路径分析有深度限制，极深的跨函数调用链中的未初始化传递可能遗漏
3. **联合体 (union)**: 联合体成员的初始化状态跟踪可能不精确

---

## 结论

`core.uninitialized.*` 系列 Checker 对变量使用前未初始化缺陷（#7）提供了全面覆盖，路径敏感分析能力优秀，能正确处理条件分支、结构体成员、数组元素和指针间接访问等复杂场景。**验证通过**。
