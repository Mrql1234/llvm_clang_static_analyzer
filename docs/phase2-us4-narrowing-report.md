# Phase 2 US4 — 浮点数转整数溢出检测评估报告

**日期**: 2026-03-27
**工具**: `clang-tidy -checks='bugprone-narrowing-conversions'` (Clang 23.0.0git)
**测试程序**: `testProgram/test_narrowing_conversion.cpp`

---

## Acceptance Scenarios 验证

| # | 场景 | 预期 | 实际输出 | 结果 |
|---|------|------|----------|------|
| AS1 | double d = 1e18; int i = d; | 告警 | `narrowing conversion from 'double' to 'int'` (line 10) | ✅ 通过 |
| AS2 | double d = 3.14; int i = d; | 告警 | `narrowing conversion from 'double' to 'int'` (line 17) | ✅ 通过 |
| AS3 | int i = 42; double d = i; | 不应告警 | 无告警 | ✅ 通过 |
| AS4 | 覆盖不足评估 | 评估增强可行性 | 见下方分析 | ✅ 通过 |

---

## 覆盖场景矩阵

| 场景 | 是否检出 | 说明 |
|------|----------|------|
| double → int 隐式赋值 | ✅ | 核心场景，可靠检出 |
| double → int 溢出值 (1e18) | ✅ | 能检测潜在溢出 |
| float → short 双重窄化 | ✅ | 浮点到更小整数类型 |
| 函数返回 double, int 接收 | ✅ | 跨函数隐式窄化 |
| (int)d C-style cast | ❌ | 视为显式意图，不报 |
| static_cast\<int\>(d) | ❌ | 视为显式意图，不报 |
| 模板 T result = val (T=int) | ❌ | 模板实例化中未检测 |
| int → double 安全方向 | ✅ 不报 | 正确行为 |

---

## 增强评估

### 未覆盖场景分析

1. **显式 cast (C-style / static_cast)**: 不检出是合理设计——用户已通过 cast 表达意图。如需严格检测，可启用 `-Wfloat-conversion` 编译器 warning 作为补充。

2. **模板实例化**: `bugprone-narrowing-conversions` 在模板实例化点未进行检查，这是一个已知局限。可通过 `-Wfloat-conversion` 编译器 warning 覆盖。

### 增强结论

| 方案 | 评估 | 建议 |
|------|------|------|
| 增强 bugprone-narrowing-conversions | 改动 LLVM 社区 check，风险高 | ❌ 不推荐 |
| 新建补充 check | 与已有 check 功能重叠 | ❌ 不推荐 |
| 使用 `-Wfloat-conversion` 补充 | 零开发成本，覆盖模板和更多场景 | ✅ 推荐 |

**结论**: 已有 `bugprone-narrowing-conversions` 覆盖了核心隐式窄化场景，配合 `-Wfloat-conversion` 可覆盖显式 cast 和模板场景。无需新建 check。

---

## 建议的工具组合

```bash
# 基础检测
clang-tidy -checks='bugprone-narrowing-conversions'

# 补充编译器 warning（覆盖 cast 和模板场景）
clang++ -Wfloat-conversion -Wdouble-promotion
```

---

## 已知限制

1. **显式 cast 不报**: 设计如此，非缺陷
2. **模板实例化不检查**: 已有局限，可通过编译器 warning 弥补
3. **constexpr 上下文**: 编译期已知值的窄化在 C++11 起已是编译错误，无需额外检测

---

## 结论

`bugprone-narrowing-conversions` 对浮点数转整数溢出缺陷（#11）的核心场景提供了可靠检测。显式 cast 和模板场景的缺口可通过 `-Wfloat-conversion` 编译器 warning 弥补。总体评估为**已有工具组合能充分覆盖**，无需新建 check。**验证通过**。
