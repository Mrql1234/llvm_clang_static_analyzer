// 模式 B 测试：#include 头文件包含函数定义（inline / 模板 / 类方法）
// CSA 可以看到函数体，因为 #include 把定义展开到了当前翻译单元
#include "utils_with_def.h"
#include <cmath>

void test_div_zero_pattern_b() {
    int d = get_zero_inline();   // inline 函数，定义在头文件中
    int x = 100 / d;             // CSA 能检出吗？
}

void test_uninit_pattern_b() {
    int val;
    no_init_inline(&val);        // inline 函数，定义在头文件中
    int x = val + 1;             // CSA 能检出吗？
}

void test_array_oob_pattern_b() {
    int arr[10];
    int idx = compute_index_inline(5); // inline 函数
    arr[idx] = 42;
}

void test_template_pattern_b() {
    int d = get_default_value<int>(); // 模板函数，定义在头文件中
    int x = 100 / d;                  // 返回 0，除以零
}

void test_class_method_pattern_b() {
    Calculator calc;
    int d = calc.get_result();  // 类方法，定义在头文件中
    int x = calc.divide(100, d); // d == 0
}
