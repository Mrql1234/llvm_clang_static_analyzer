# C++ 缺陷检测器 — 17 种缺陷覆盖状态

- [x] 1. 数组越界：上界和下界, 下标为整数 — `core.ArrayBound` (Phase 1)
- [x] 2. 除以零 — `core.DivideZero` (Phase 1)
- [x] 3. 死循环 — `bugprone-infinite-loop` + `bugprone-loop-external-dependency` (Phase 2)
- [x] 4. 缓存区读写操作是否越界 (strcpy/strcat) — `alpha.unix.cstring.OutOfBounds` (Phase 2)
- [x] 5. 是否使用递归导致栈溢出 — `bugprone-unbounded-recursion` (Phase 2)
- [x] 6. 指针使用前是否进行非空判断 — `core.NullDereference` (Phase 1)
- [x] 7. 变量使用前是否初始化 — `core.uninitialized.*` (Phase 2)
- [x] 8. 是否对浮点数使用等号进行相等比较 — `bugprone-float-equal-comparison` (Phase 1)
- [x] 9. 是否出现无符号数负值 — `-Wsign-conversion` (Phase 2)
- [x] 10. 反三角函数是否判断参数范围 — `bugprone-math-domain-guard` (Phase 1)
- [x] 11. 浮点数转整数是否有溢出 — `bugprone-narrowing-conversions` (Phase 2)
- [x] 12. 是否存在函数内定义大内存的局部变量，导致栈溢出 — `bugprone-large-stack-variable` (Phase 1)
- [x] 13. memcpy函数拷贝，目标地址空间是否足够 — `alpha.unix.cstring.OutOfBounds` (Phase 2)
- [x] 14. memset函数，目标地址空间是否大于要填充的字节数 — `alpha.unix.cstring.OutOfBounds` (Phase 2)
- [x] 15. sqrt函数输入为负数 — `bugprone-math-domain-guard` (Phase 1)
- [x] 16. 浮点运算是否使用双精度浮点数 — `bugprone-float-precision-*` (Phase 2)
- [x] 17. 是否使用有符号数和无符号数进行比较 — `-Wsign-compare` (Phase 2)

**全部 17 种缺陷检测能力已完成 ✅**
