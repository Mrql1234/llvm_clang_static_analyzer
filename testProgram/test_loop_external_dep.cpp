// test_loop_external_dep.cpp — Phase 2 US6: bugprone-infinite-loop + bugprone-loop-external-dependency
// Run: clang-tidy -checks='-*,bugprone-infinite-loop,bugprone-loop-external-dependency' test_loop_external_dep.cpp

// External function declarations (opaque to static analysis)
extern bool get_status();
extern void process();
extern void process_events();

// --- Scene 1: while(true) with no break/return (bugprone-infinite-loop should detect) ---
void test_while_true_no_exit() {
    while (true) {
        process();
    }
}

// --- Scene 2: Loop variable not modified in body (bugprone-infinite-loop should detect) ---
void test_var_not_modified(int x) {
    int y = 0;
    while (x > 0) {  // x never modified inside loop
        y++;
    }
}

// --- Scene 3: Loop exit depends on external function call (new checker should hint) ---
void test_external_call_condition() {
    while (get_status()) {  // exit depends on external function call
        process();
    }
}

// --- Scene 4: Normal loop (should NOT warn) ---
void test_normal_loop(int n) {
    for (int i = 0; i < n; i++) {
        process();
    }
}

// --- Scene 5: Event loop (running modified externally) ---
volatile bool running = true;
void test_event_loop() {
    while (running) {  // volatile — should NOT be reported as infinite or external dep
        process_events();
    }
}

// --- Edge cases ---

// do-while form
void test_do_while_external() {
    do {
        process();
    } while (get_status());  // external dependency
}

// for(;;) with no break
void test_for_ever() {
    for (;;) {  // bugprone-infinite-loop should catch this
        process();
    }
}

// Unreachable break
void test_unreachable_break() {
    int x = 10;
    while (x > 0) {
        if (false) break;  // unreachable break — x is never modified
        process();
    }
}

// Nested loop: outer condition modified by inner loop
void test_nested_modification() {
    int x = 10;
    while (x > 0) {
        for (int i = 0; i < 5; i++) {
            x--;
        }
    }
}

// Volatile loop condition — should NOT warn (hardware scenario)
void test_volatile_condition() {
    volatile int flag = 1;
    while (flag) {  // volatile — should not be reported
        process();
    }
}

// Loop with condition variable modified via assignment in body
void test_modified_in_body() {
    bool done = false;
    while (!done) {
        process();
        done = true;  // direct assignment modifies condition variable
    }
}

int main() {
    test_while_true_no_exit();
    test_var_not_modified(5);
    test_external_call_condition();
    test_normal_loop(10);
    test_event_loop();
    test_do_while_external();
    test_for_ever();
    test_unreachable_break();
    test_nested_modification();
    test_volatile_condition();
    test_modified_in_body();
    return 0;
}
