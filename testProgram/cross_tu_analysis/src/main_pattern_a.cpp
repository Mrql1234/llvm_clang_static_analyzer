// 模式 A 测试：#include 头文件只有声明，定义在另一个 .cpp 中
// 这和直接写 extern 在 CSA 看来完全一样 —— 看不到函数体
#include "utils_decl_only.h"
#include <cmath>

void test_div_zero_pattern_a() {
    int d = get_zero();       // 头文件里只有声明
    int x = 100 / d;          // CSA 能检出吗？
}

void test_uninit_pattern_a() {
    int val;
    no_init(&val);             // 头文件里只有声明
    int x = val + 1;          // CSA 能检出吗？
}

void test_array_oob_pattern_a() {
    int arr[10];
    int idx = compute_index(5); // 头文件里只有声明
    arr[idx] = 42;              // CSA 能检出吗？
}

void test_math_domain_pattern_a() {
    double v = get_negative();  // 头文件里只有声明
    double r = sqrt(v);
}
