// test_float_literal_suffix.cpp — Phase 2 US5-C: bugprone-float-literal-suffix
// Run: clang-tidy -checks='-*,bugprone-float-literal-suffix' test_float_literal_suffix.cpp

// --- Positive cases ---

float test_missing_suffix() {
    float x = 3.14;  // expected: add 'f' suffix
    return x;
}

float test_missing_suffix_init() {
    float y = 2.718;  // expected: add 'f' suffix
    return y;
}

// --- Negative cases ---

float test_correct_suffix() {
    float x = 3.14f;  // correct suffix — no warning
    return x;
}

double test_double_context() {
    double d = 3.14;  // double context — no warning
    return d;
}

// --- Edge cases ---

// Zero value — precision identical, should NOT warn
float test_zero() {
    float x = 0.0;  // zero — no warning expected
    return x;
}

int main() {
    test_missing_suffix();
    test_missing_suffix_init();
    test_correct_suffix();
    test_double_context();
    test_zero();
    return 0;
}
