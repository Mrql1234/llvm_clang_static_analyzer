// Test cases for alpha.core.PossibleDivideZero
// Run: $CLANG --analyze $CSA_SDK_FLAGS \
//        -Xanalyzer -analyzer-checker=alpha.core.PossibleDivideZero \
//        -Xanalyzer -analyzer-output=text \
//        testProgram/test_possible_div_zero.cpp

extern int get_value();
extern bool condition();

// Scenario 1: External input as divisor — should WARN
void test_external_input_divisor() {
    int divisor = get_value();
    int result = 100 / divisor;  // WARN: may be zero
    (void)result;
}

// Scenario 2: Modulo with external input — should WARN
void test_external_input_modulo() {
    int divisor = get_value();
    int result = 100 % divisor;  // WARN: may be zero
    (void)result;
}

// Scenario 3: Validated non-zero — should NOT warn
void test_validated_nonzero() {
    int divisor = get_value();
    if (divisor != 0) {
        int result = 100 / divisor;  // OK
        (void)result;
    }
}

// Scenario 4: Conditional assignment, some paths may be zero — should WARN
void test_conditional_assignment() {
    int d = 0;
    if (condition())
        d = get_value();
    int r = 100 / d;  // WARN: d may be zero on some paths
    (void)r;
}

// Scenario 5: Literal non-zero divisor — should NOT warn
void test_literal_divisor() {
    int r = 100 / 7;  // OK
    (void)r;
}

// Scenario 6: Literal zero divisor — should NOT warn (core.DivideZero handles)
void test_literal_zero() {
    int r = 100 / 0;  // core.DivideZero reports this; we skip
    (void)r;
}
