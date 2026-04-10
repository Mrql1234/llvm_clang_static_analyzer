# Phase 2 US3 — 整数符号类检测能力验证报告

**日期**: 2026-03-27
**工具**: `clang++ -fsyntax-only -Wsign-conversion -Wsign-compare` (Clang 23.0.0git)
**测试程序**: `testProgram/test_sign_conversion.cpp`

---

## Acceptance Scenarios 验证

| # | 场景 | 预期 | 实际输出 | 结果 |
|---|------|------|----------|------|
| AS1 | 负数赋给 unsigned | `-Wsign-conversion` 告警 | `implicit conversion changes signedness` (line 12, 17) | ✅ 通过 |
| AS2 | int 与 unsigned 比较 | `-Wsign-compare` 告警 | `comparison of integers of different signs` (line 24, 28, 32) | ✅ 通过 |
| AS3 | 类型匹配正确 | 不应产生告警 | 无告警 | ✅ 通过 |
| AS4 | 汇总覆盖范围 | 记录建议 | 见下方分析 | ✅ 通过 |

---

## 边界条件验证

| 场景 | 预期 | 实际输出 | 结果 |
|------|------|----------|------|
| char 参与 unsigned 运算 | `-Wsign-conversion` | 检出 (line 56) | ✅ |
| 模板中 signed/unsigned 比较 | `-Wsign-compare` | 检出 (line 63) | ✅ |
| size_t 与 int 循环计数器比较 | `-Wsign-compare` | 检出 (line 75) | ✅ |
| static_cast\<unsigned\>(-1) | 可能告警 | 无告警（显式 cast 被认可） | ✅ |

---

## 告警输出汇总

共产生 **9 个告警**（4 个 `-Wsign-conversion` + 5 个 `-Wsign-compare`），全部为预期告警，无误报。

### -Wsign-conversion 覆盖范围

| 场景 | 是否检出 |
|------|----------|
| int 变量赋给 unsigned int | ✅ |
| 负整数字面量赋给 unsigned int | ✅ |
| char 参与 unsigned int 运算 | ✅ |
| int 下标访问 vector (size_type) | ✅ |
| static_cast 显式转换 | ✅ 不报（正确行为） |

### -Wsign-compare 覆盖范围

| 场景 | 是否检出 |
|------|----------|
| int < unsigned int | ✅ |
| int == unsigned int | ✅ |
| int > unsigned int | ✅ |
| 模板函数中 T vs size_t | ✅ |
| int vs vector::size() 循环 | ✅ |

---

## 建议的编译选项组合

```bash
# 基础检测（推荐默认启用）
clang++ -Wsign-compare -Wsign-conversion

# 严格模式（配合 -Werror 使用）
clang++ -Werror=sign-compare -Werror=sign-conversion

# 与其他整数安全选项组合
clang++ -Wsign-compare -Wsign-conversion -Wconversion -Wimplicit-int-conversion
```

---

## 结论

`-Wsign-conversion` 和 `-Wsign-compare` 对整数符号类缺陷（#9 无符号数赋负值、#17 有符号数与无符号数比较）提供了完整覆盖。作为编译器内置 warning，它们在任何 CSA 部署环境中都可用（CSA 与 clang++ 是同一二进制），且零额外开发成本。所有 4 个 Acceptance Scenarios 均验证通过。**验证通过**。
