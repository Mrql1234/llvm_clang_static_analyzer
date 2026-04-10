// 情况二：不同 .cpp 文件的跨函数调用
// 只有声明，定义在 cross_tu_utils.cpp 中
// 测试 CSA 和 clang-tidy 能否检测到缺陷

extern int ext_return_zero();
extern int ext_return_negative();
extern int ext_compute_index(int n);
extern void ext_no_init(int *p);
extern int ext_chain_outer();

// ---------- 缺陷场景（与同文件版本完全对称） ----------

// S1: 除以零 — 1 层调用
void s1_div_zero_cross_tu() {
    int d = ext_return_zero();
    int x = 100 / d;
}

// S2: 除以零 — 多层调用链
void s2_div_zero_chain_cross_tu() {
    int d = ext_chain_outer();
    int x = 100 / d;
}

// S3: 数组越界 — 索引来自外部函数
void s3_array_oob_cross_tu() {
    int arr[10];
    int idx = ext_compute_index(5);  // 实际返回 50
    arr[idx] = 42;
}

// S4: 未初始化 — 外部函数未给输出参数赋值
void s4_uninit_cross_tu() {
    int val;
    ext_no_init(&val);
    int x = val + 1;
}

// S5: 数学域 — 负数来自外部函数
#include <cmath>
void s5_math_domain_cross_tu() {
    double v = ext_return_negative();
    double r = sqrt(v);
}
