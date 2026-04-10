// data_provider.cpp — 模拟数据提供层
#include "data_provider.h"

int fetch_value() {
    return 50;  // 总是返回 <= 100 的值，导致 compute_divisor 返回 0
}

int fetch_index() {
    return 5;  // 返回 5，经 get_array_index(5) 变为 50，导致越界
}

void init_record(int *out) {
    // BUG: 忘记给 *out 赋值
    // 应该写: *out = 0;
}
