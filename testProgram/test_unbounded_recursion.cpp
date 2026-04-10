// test_unbounded_recursion.cpp — Phase 2 US7: bugprone-unbounded-recursion
// Run: clang-tidy -checks='-*,bugprone-unbounded-recursion' test_unbounded_recursion.cpp

// External function declarations
extern bool get_flag();
extern void do_something(int);

// --- Positive cases ---

// No termination condition — unconditional self-call
void unconditional_recursion() {
    unconditional_recursion();  // expected: error — no termination condition
}

// Indirect recursion: mutual_a → mutual_b → mutual_a (no termination)
void mutual_b();
void mutual_a() {
    mutual_b();  // expected: indirect recursion cycle
}
void mutual_b() {
    mutual_a();  // expected: indirect recursion cycle
}

// Termination depends on external function call
void external_termination(int n) {
    if (get_flag()) return;
    external_termination(n);  // expected: termination depends on external call
}

// --- Negative cases ---

// Correct recursion with parameter-based termination
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);  // proper termination — no warning
}

// Correct recursion with switch-based termination
int fibonacci(int n) {
    switch (n) {
    case 0: return 0;
    case 1: return 1;
    default: return fibonacci(n - 1) + fibonacci(n - 2);
    }
}

// --- Edge cases ---

// Termination depends on runtime parameter (proper recursion)
void runtime_param_recursion(int depth) {
    if (depth <= 0) return;
    do_something(depth);
    runtime_param_recursion(depth - 1);  // no warning — has parameter-based termination
}

// Callback/function pointer (cannot resolve statically, should NOT report)
typedef void (*callback_t)(int);
void indirect_via_pointer(callback_t cb, int n) {
    if (n <= 0) return;
    cb(n - 1);  // function pointer — cannot resolve, no warning
}

// Tail recursion
int tail_sum(int n, int acc) {
    if (n <= 0) return acc;
    return tail_sum(n - 1, acc + n);  // proper tail recursion — no warning
}

int main() {
    unconditional_recursion();
    mutual_a();
    external_termination(5);
    factorial(5);
    fibonacci(10);
    runtime_param_recursion(5);
    indirect_via_pointer(nullptr, 5);
    tail_sum(10, 0);
    return 0;
}
