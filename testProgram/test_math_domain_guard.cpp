// Test cases for bugprone-math-domain-guard
// Run: $CLANG_TIDY -checks='-*,bugprone-math-domain-guard' \
//        testProgram/test_math_domain_guard.cpp -- $CSA_SDK_FLAGS

#include <cmath>

extern double get_value();

// Scenario 1: Reverse guard — sqrt inside x < 0 branch — should WARN
void test_sqrt_reverse_guard(double x) {
    if (x < 0) {
        double y = sqrt(x);  // WARN: reverse guard
        (void)y;
    }
}

// Scenario 2: Unguarded sqrt — should WARN
void test_sqrt_unguarded(double x) {
    double y = sqrt(x);  // WARN: not validated
    (void)y;
}

// Scenario 3: Positive guard — should NOT warn
void test_sqrt_positive_guard(double x) {
    if (x >= 0) {
        double y = sqrt(x);  // OK
        (void)y;
    }
}

// Scenario 4: Guard with > 0 — should NOT warn
void test_sqrt_strict_positive_guard(double x) {
    if (x > 0) {
        double y = sqrt(x);  // OK
        (void)y;
    }
}

// Scenario 5: fabs protection — should NOT warn
void test_sqrt_fabs(double x) {
    double y = sqrt(fabs(x));  // OK — fabs result is non-negative
    (void)y;
}

// Scenario 6: Compile-time constant — should NOT warn (MathDomainChecker handles)
void test_sqrt_constant() {
    double y = sqrt(4.0);  // OK — constant, handled by CSA checker
    (void)y;
}

// Scenario 7: Unguarded asin — should WARN
void test_asin_unguarded(double x) {
    double y = asin(x);  // WARN: not validated
    (void)y;
}

// Scenario 8: acos with range guard — should NOT warn
void test_acos_guarded(double x) {
    if (x >= -1.0 && x <= 1.0) {
        double y = acos(x);  // OK
        (void)y;
    }
}

// Scenario 9: Negated condition guard — should NOT warn
void test_sqrt_negated_guard(double x) {
    if (!(x < 0)) {
        double y = sqrt(x);  // OK — !(x < 0) means x >= 0
        (void)y;
    }
}

// Scenario 10: Else branch positive guard — should NOT warn
void test_sqrt_else_guard(double x) {
    if (x < 0) {
        return;
    } else {
        double y = sqrt(x);  // OK — in else branch where x >= 0
        (void)y;
    }
}

// Scenario 11: Complex expression argument — should WARN (can't determine guard)
void test_sqrt_complex_arg(double a, double b) {
    double y = sqrt(a + b);  // WARN: argument not validated
    (void)y;
}
