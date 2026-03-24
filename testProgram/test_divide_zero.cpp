// test_divide_zero.cpp — CSA core.DivideZero 检测验证
// 场景1: 字面量除零 → 应报警
// 场景2: 变量除零（路径敏感）→ 应报警
// 场景3: 安全除法 → 不应报警

int get_value();

// 场景1: 字面量除零 — 期望 core.DivideZero 告警
void literal_divide_by_zero() {
    int x = 10;
    int y = 0;
    int z = x / y; // expected-warning: Division by zero
    (void)z;
}

// 场景2: 变量除零（路径敏感分析）— 期望通过路径约束检出
void variable_divide_by_zero() {
    int divisor = get_value();
    if (divisor == 0) {
        int result = 100 / divisor; // expected-warning: Division by zero (path-sensitive)
        (void)result;
    }
}

// 场景3: 安全除法 — 不应报警
void safe_division() {
    int a = 10;
    int b = 5;
    int c = a / b; // no warning expected
    (void)c;
}

int main() {
    literal_divide_by_zero();
    variable_divide_by_zero();
    safe_division();
    return 0;
}
