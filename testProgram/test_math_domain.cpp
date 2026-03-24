#include <cmath>
#include <cstdlib>

// Positive cases - should warn
void test_sqrt_literal_negative() {
    double x = sqrt(-1.0);  // WARN: sqrt of negative
}

void test_asin_out_of_range() {
    double x = asin(2.0);   // WARN: asin domain error
}

void test_acos_out_of_range() {
    double x = acos(-1.5);  // WARN: acos domain error
}

// Negative cases - should NOT warn
void test_sqrt_positive() {
    double x = sqrt(4.0);   // OK
}

void test_sqrt_zero() {
    double x = sqrt(0.0);   // OK: boundary legal value
}

void test_asin_boundary_legal() {
    double x = asin(1.0);   // OK: boundary legal
}

void test_acos_boundary_legal() {
    double x = acos(-1.0);  // OK: boundary legal
}

// Path-sensitive cases
void test_sqrt_path_constrained_negative(double x) {
    if (x < 0) {
        double y = sqrt(x); // WARN ideally, but CSA can't track float constraints
    }
}

void test_sqrt_path_constrained_nonneg(double x) {
    if (x >= 0) {
        double y = sqrt(x); // OK: x is constrained non-negative
    }
}

void test_sqrt_unconstrained(double x) {
    double y = sqrt(x);     // Should NOT warn (conservative: may be negative or not)
}

int main() { return 0; }
