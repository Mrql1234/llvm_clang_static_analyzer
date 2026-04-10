// 模式 B：头文件里包含函数定义
// 常见于：inline 函数、模板函数、类内定义的方法
#pragma once

inline int get_zero_inline() { return 0; }
inline int get_negative_inline() { return -1; }
inline int compute_index_inline(int n) { return n * 10; }
inline void no_init_inline(int *p) { /* 故意不赋值 */ }

// 模板函数 — 定义必须在头文件
template <typename T>
T get_default_value() { return T(0); }

// 类内定义的方法
class Calculator {
public:
    int divide(int a, int b) { return a / b; }
    int get_result() { return 0; }
};
