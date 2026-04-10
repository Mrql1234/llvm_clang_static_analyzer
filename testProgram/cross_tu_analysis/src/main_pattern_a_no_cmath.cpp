// 模式 A 测试（不含 cmath，减少代码量）
#include "utils_decl_only.h"

void test_div_zero_pattern_a() {
    int d = get_zero();
    int x = 100 / d;
}

void test_uninit_pattern_a() {
    int val;
    no_init(&val);
    int x = val + 1;
}
