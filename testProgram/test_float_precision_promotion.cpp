// test_float_precision_promotion.cpp — Phase 2 US5-A: bugprone-float-precision-promotion
// Run: clang-tidy -checks='-*,bugprone-float-precision-promotion' test_float_precision_promotion.cpp

// --- Positive cases ---

float test_float_plus_double_literal() {
    float x = 1.0f;
    float y = x + 2.0;  // expected: float promoted to double in arithmetic
    return y;
}

float test_float_times_double() {
    float a = 3.0f;
    double b = 4.0;
    float c = a * b;  // expected: float promoted to double
    return c;
}

// --- Negative cases ---

float test_all_float() {
    float x = 1.0f;
    float y = x + 2.0f;  // all float — no warning
    return y;
}

double test_all_double() {
    double x = 1.0;
    double y = x + 2.0;  // all double — no warning
    return y;
}

// --- Edge cases ---

// printf: float auto-promoted to double — standard C behavior, should NOT warn
#include <cstdio>
void test_printf_promotion() {
    float x = 3.14f;
    printf("%f\n", x);  // standard variadic promotion, no warning expected
}

// Explicit cast — user expressed intent
float test_explicit_cast() {
    float x = 1.0f;
    float y = x + static_cast<float>(2.0);  // explicit cast — no warning
    return y;
}

int main() {
    test_float_plus_double_literal();
    test_float_times_double();
    test_all_float();
    test_all_double();
    test_printf_promotion();
    test_explicit_cast();
    return 0;
}
