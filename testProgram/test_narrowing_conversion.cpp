// test_narrowing_conversion.cpp — Phase 2 US4: bugprone-narrowing-conversions verification
// Run: clang-tidy -checks='-*,bugprone-narrowing-conversions' \
//      -p build-csa test_narrowing_conversion.cpp -- -std=c++17

// --- Positive cases (should trigger warnings) ---

// double → int overflow
int test_double_overflow() {
    double d = 1e18;
    int i = d;  // expected: narrowing warning (overflow)
    return i;
}

// double → int truncation
int test_double_truncation() {
    double d = 3.14;
    int i = d;  // expected: narrowing warning (truncation)
    return i;
}

// C-style cast double → int
int test_c_cast() {
    double d = 3.14;
    int i = (int)d;  // expected: narrowing warning (C-style cast)
    return i;
}

// --- Negative cases (should NOT trigger warnings) ---

// int → double (safe widening)
double test_safe_widening() {
    int i = 42;
    double d = i;  // safe direction — no warning expected
    return d;
}

// Same-type assignment
int test_same_type() {
    int a = 42;
    int b = a;  // same type — no warning
    return b;
}

// --- Edge cases ---

// float → short (double narrowing)
short test_float_to_short() {
    float f = 3.14f;
    short s = f;  // expected: narrowing warning
    return s;
}

// Function returning double, caller uses int
double compute_value() { return 3.14159; }

int test_function_return() {
    int result = compute_value();  // expected: narrowing warning
    return result;
}

// Template with implicit conversion
template <typename T>
T narrow_convert(double val) {
    T result = val;  // expected: narrowing warning when T=int
    return result;
}

void test_template_narrowing() {
    int x = narrow_convert<int>(3.14);
    (void)x;
}

// static_cast<int>(d) — explicit intent
int test_static_cast() {
    double d = 3.14;
    int i = static_cast<int>(d);  // explicit cast — may or may not warn
    return i;
}

int main() {
    test_double_overflow();
    test_double_truncation();
    test_c_cast();
    test_safe_widening();
    test_same_type();
    test_float_to_short();
    test_function_return();
    test_template_narrowing();
    test_static_cast();
    return 0;
}
