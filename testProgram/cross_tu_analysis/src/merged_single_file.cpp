// 直接把两个 .cpp 的内容合并成一个文件（不用 #include .cpp）
#include <cmath>

// ===== 原来在 utils_decl_only.cpp 里的定义 =====
int get_zero() { return 0; }
int get_negative() { return -1; }
int compute_index(int n) { return n * 10; }
void no_init(int *p) { /* 故意不赋值 */ }

// ===== 原来在 main_pattern_a.cpp 里的调用者 =====
void test_div_zero() {
    int d = get_zero();
    int x = 100 / d;
}

void test_uninit() {
    int val;
    no_init(&val);
    int x = val + 1;
}

void test_array_oob() {
    int arr[10];
    int idx = compute_index(5);
    arr[idx] = 42;
}

void test_math_domain() {
    double v = get_negative();
    double r = sqrt(v);
}
