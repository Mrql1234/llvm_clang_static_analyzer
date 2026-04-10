# Phase 2 US2 — 缓冲区/内存操作越界检测能力验证报告

**日期**: 2026-03-27
**工具**: `clang++ --analyze -Xanalyzer -analyzer-checker=alpha.unix.cstring.OutOfBounds` (Clang 23.0.0git)
**测试程序**: `testProgram/test_cstring_bounds.cpp`

---

## Acceptance Scenarios 验证

| # | 场景 | 预期 | 实际输出 | 结果 |
|---|------|------|----------|------|
| AS1 | strcpy(dst, src) dst 小于 src 长度 | 告警 | `String copy function overflows the destination buffer` (line 14) | ✅ 通过 |
| AS2 | strcat(dst, src) 拼接后超过 dst 容量 | 告警 | `String concatenation function overflows the destination buffer` (line 21) | ✅ 通过 |
| AS3 | memcpy(dst, src, 16) n > sizeof(dst) | 告警 | `Memory copy function overflows the destination buffer` (line 28) | ✅ 通过 |
| AS4 | memset(buf, 0, 16) n > sizeof(buf) | 告警 | `Memory set function overflows the destination buffer` (line 34) | ✅ 通过 |
| AS5 | 安全调用 | 不应产生告警 | 无告警 | ✅ 通过 |
| AS6 | 未检出场景记录 | Known Limitation 清单 | 无未检出场景（见下方） | ✅ 通过 |

---

## 边界条件验证

| 场景 | 预期 | 实际输出 | 结果 |
|------|------|----------|------|
| malloc 动态分配 + strcpy 越界 | 应告警 | `String copy function overflows` (line 69) | ✅ |
| 符号值长度 (n > 10, memset n) | 可能检出 | `Memory set function overflows` (line 78) | ✅ 超预期 |
| strncpy 安全变体 | 不应报 | 无告警 | ✅ |
| strncat 安全变体 | 不应报 | 无告警 | ✅ |
| sizeof 正确使用 | 不应报 | 无告警 | ✅ |
| sizeof(pointer) vs sizeof(array) | 不应报 | 无告警 | ✅ |

---

## alpha Checker 行为分析

### 检测能力

`alpha.unix.cstring.OutOfBounds` 依赖 `CStringModeling` 对 C 字符串操作函数建模，具备以下能力：

1. **静态大小缓冲区**: 精确检测栈上固定大小数组的越界写入
2. **动态分配缓冲区**: 能追踪 `malloc` 返回的缓冲区大小并检测越界
3. **符号值分析**: 在约束求解器能推导出 n > buf_size 的条件下，能检测符号大小的越界
4. **安全变体识别**: 正确处理 `strncpy`/`strncat` 的长度限制参数

### 检测质量

- **误报率**: 0/10 测试场景（无误报）
- **漏报率**: 0/6 应检出场景（无漏报）

---

## Known Limitations

1. **alpha 状态**: 该 Checker 标记为 `alpha`（实验性），可能在复杂程序中产生误报或漏报
2. **跨函数**: 缓冲区大小通过多层函数传递时，路径分析深度限制可能导致遗漏
3. **C++ 容器**: 不适用于 `std::string`/`std::vector` 等 C++ 容器，仅分析 C 风格字符串操作
4. **自定义包装**: 对用户自定义的字符串操作包装函数无感知

---

## 结论

`alpha.unix.cstring.OutOfBounds` Checker 对 strcpy/strcat/memcpy/memset 越界（缺陷 #4, #13, #14）提供了全面覆盖，甚至在符号值和动态分配场景中也能正确检测。所有 6 个 Acceptance Scenarios 均验证通过。虽然是 alpha 状态，但在本测试中表现稳定。**验证通过**。
