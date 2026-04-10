// 情况一：同一个 .cpp 文件内的跨函数调用
// 测试 CSA 和 clang-tidy 的检测能力

// ---------- 辅助函数 ----------
int return_zero() { return 0; }
int return_negative() { return -1; }
int compute_index(int n) { return n * 10; }

void no_init(int *p) {
    // 故意不给 *p 赋值
}

int chain_a() { return return_zero(); }
int chain_b() { return chain_a(); }
int chain_c() { return chain_b(); }  // 4 层调用链

// ---------- 缺陷场景 ----------

// S1: 除以零 — 1 层调用
void s1_div_zero_1_level() {
    int d = return_zero();
    int x = 100 / d;
}

// S2: 除以零 — 4 层调用链
void s2_div_zero_4_levels() {
    int d = chain_c();
    int x = 100 / d;
}

// S3: 数组越界 — 索引来自函数返回值
void s3_array_oob() {
    int arr[10];
    int idx = compute_index(5);  // 返回 50
    arr[idx] = 42;
}

// S4: 未初始化 — 输出参数未赋值
void s4_uninit_output_param() {
    int val;
    no_init(&val);
    int x = val + 1;
}

// S5: 数学域 — 负数来自函数返回值
#include <cmath>
void s5_math_domain() {
    double v = return_negative();
    double r = sqrt(v);  // v == -1, 数学域错误
}
