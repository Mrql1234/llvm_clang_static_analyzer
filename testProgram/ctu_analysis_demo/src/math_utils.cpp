// math_utils.cpp — 数学工具函数
#include "math_utils.h"

int safe_divide(int a, int b) {
    return a / b;  // 调用者应保证 b != 0，但这里没检查
}

int compute_divisor(int input) {
    if (input > 100)
        return input - 100;
    return 0;  // 当 input <= 100 时返回 0 — 潜在除以零
}

int get_array_index(int n) {
    return n * 10;  // 当 n >= 1 时返回 >= 10，可能越界
}

void process_buffer(int *buf, int size, int index) {
    buf[index] = 42;  // 不检查 index 是否在 [0, size) 范围内
}
