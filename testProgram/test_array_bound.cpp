// test_array_bound.cpp — CSA security.ArrayBound 检测验证
// 需启用: -Xanalyzer -analyzer-checker=security.ArrayBound
// 场景1: 上界越界 → 应报警
// 场景2: 负数下标 → 应报警
// 场景3: 合法访问 → 不应报警

// 场景1: 上界越界 — 期望 security.ArrayBound 告警
void upper_bound_overflow() {
    int arr[10];
    arr[15] = 0; // expected-warning: Out of bound access
    (void)arr[0];
}

// 场景2: 负数下标 — 期望 security.ArrayBound 告警
void negative_index() {
    int arr[10];
    arr[-1] = 0; // expected-warning: Out of bound access (negative index)
    (void)arr[0];
}

// 场景3: 合法访问 — 不应报警
void legal_access() {
    int arr[10];
    arr[5] = 0; // no warning expected
    (void)arr[0];
}

int main() {
    upper_bound_overflow();
    negative_index();
    legal_access();
    return 0;
}
