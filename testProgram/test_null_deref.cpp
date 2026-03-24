// test_null_deref.cpp — CSA core.NullDereference 检测验证
// 场景1: 直接 nullptr 解引用 → 应报警
// 场景2: 条件分支后解引用 → 应报警（null 路径）
// 场景3: 多层函数传递 null → 应报警（跨过程分析）

bool some_condition();

// 场景1: 直接 nullptr 解引用 — 期望 core.NullDereference 告警
void direct_null_deref() {
    int *p = nullptr;
    int x = *p; // expected-warning: Dereference of null pointer
    (void)x;
}

// 场景2: 条件分支后解引用 — p 在部分路径上仍为 null
void conditional_null_deref() {
    int value = 42;
    int *p = nullptr;
    if (some_condition()) {
        p = &value;
    }
    int x = *p; // expected-warning on path where p is still null
    (void)x;
}

// 场景3: 多层函数传递 null — 跨过程分析
void bar(int *p) {
    *p = 42; // expected-warning: Dereference of null pointer (passed from foo)
}

void foo() {
    bar(nullptr);
}

int main() {
    direct_null_deref();
    foo();
    return 0;
}
