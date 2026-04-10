// test_float_precision_loss.cpp — Phase 2 US5-B: bugprone-float-precision-loss
// Run: clang-tidy -checks='-*,bugprone-float-precision-loss' test_float_precision_loss.cpp

#include <cmath>

// --- Positive cases ---

float test_double_to_float_assign() {
    double d = sin(1.0);
    float f = d;  // expected: precision loss warning
    return f;
}

float test_double_literal_to_float() {
    double d = 3.14159265358979;
    float f = d;  // expected: precision loss warning
    return f;
}

// --- Negative cases ---

float test_float_to_float() {
    float f1 = 1.0f;
    float f2 = f1;  // same type — no warning
    return f2;
}

// --- Edge cases ---

// Explicit static_cast — user expressed intent, should NOT warn
float test_explicit_cast() {
    double d = 3.14;
    float f = static_cast<float>(d);  // explicit — no warning
    return f;
}

// Zero value — precision is identical
float test_zero_value() {
    float f = 0.0;  // zero — no warning expected
    return f;
}

int main() {
    test_double_to_float_assign();
    test_double_literal_to_float();
    test_float_to_float();
    test_explicit_cast();
    test_zero_value();
    return 0;
}
