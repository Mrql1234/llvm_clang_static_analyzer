// 测试 1：没有前置声明 — 直接定义后调用
int get_zero_v1() { return 0; }

void test_no_forward_decl() {
    int d = get_zero_v1();
    int x = 100 / d;
}

// 测试 2：有前置声明 — 先声明、再定义、再调用
int get_zero_v2();                    // 前置声明
int get_zero_v2() { return 0; }      // 定义

void test_with_forward_decl() {
    int d = get_zero_v2();
    int x = 100 / d;
}

// 测试 3：声明和定义之间有距离（模拟 Unity Build 的情况）
int get_zero_v3();                    // 声明（模拟头文件里的声明）
void no_init_v3(int *p);             // 声明

int get_zero_v3() { return 0; }      // 定义（模拟另一个 cpp）
void no_init_v3(int *p) { }          // 定义

void test_separated_decl_def() {
    int d = get_zero_v3();
    int x = 100 / d;
}

void test_separated_uninit() {
    int val;
    no_init_v3(&val);
    int x = val + 1;
}
