// test_cross_function.cpp — 验证 CSA 的跨函数分析能力
// 测试场景：缺陷的根因在被调函数中，调用者处触发

// ========== 场景 1: 除以零 — 零值来自被调函数 ==========
int get_zero() { return 0; }

void test_div_zero_cross_func() {
    int d = get_zero();
    int x = 100 / d;  // CSA 能否追踪到 d == 0?
}

// ========== 场景 2: 除以零 — 零值经过两层调用 ==========
int inner_zero() { return 0; }
int middle_func() { return inner_zero(); }

void test_div_zero_two_levels() {
    int d = middle_func();
    int x = 100 / d;  // 两层调用深度
}

// ========== 场景 3: 数组越界 — 索引来自被调函数 ==========
int get_bad_index() { return 100; }

void test_array_oob_cross_func() {
    int arr[10];
    int idx = get_bad_index();
    arr[idx] = 42;  // idx == 100，越界
}

// ========== 场景 4: 未初始化 — 被调函数未初始化输出参数 ==========
void init_value(int *out) {
    // 忘记赋值给 *out
}

void test_uninit_cross_func() {
    int val;
    init_value(&val);
    int x = val + 1;  // val 是否被初始化取决于 init_value 的实现
}

// ========== 场景 5: 跨函数但定义在不同翻译单元 ==========
extern int external_get_divisor();  // 定义在另一个 .cpp 文件中

void test_div_zero_cross_tu() {
    int d = external_get_divisor();
    int x = 100 / d;  // CSA 看不到 external_get_divisor 的实现
}

// ========== 场景 6: 条件路径 — 只有某些路径导致零 ==========
int maybe_zero(int flag) {
    if (flag > 0) return flag;
    return 0;
}

void test_div_zero_conditional(int flag) {
    int d = maybe_zero(flag);
    int x = 100 / d;  // 只有 flag <= 0 时才除以零
}
