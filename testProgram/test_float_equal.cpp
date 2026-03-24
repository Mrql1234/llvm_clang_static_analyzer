#include <cmath>

// WARN: float == literal
void test_double_eq_literal(double a) {
    if (a == 0.1) {}  // WARN
}

// WARN: float != literal
void test_float_ne_literal(float a) {
    if (a != 0.0f) {} // WARN
}

// OK: integer comparison
void test_int_comparison(int a, int b) {
    if (a == b) {}    // OK: no warning for integers
}

// OK: NaN detection pattern (x != x)
void test_nan_detection(double x) {
    if (x != x) {}    // OK: standard NaN check, should NOT warn
}

// WARN: double == 0.0 (conservative strategy)
void test_double_eq_zero(double a) {
    if (a == 0.0) {}  // WARN: even though sometimes safe
}

// WARN: two float variables
void test_two_floats(float a, float b) {
    if (a == b) {}    // WARN
}

int main() { return 0; }
