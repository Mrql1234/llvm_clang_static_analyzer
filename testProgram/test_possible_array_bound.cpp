// Test cases for alpha.security.PossibleArrayBound
// Run: $CLANG --analyze $CSA_SDK_FLAGS \
//        -Xanalyzer -analyzer-checker=alpha.security.PossibleArrayBound \
//        -Xanalyzer -analyzer-output=text \
//        testProgram/test_possible_array_bound.cpp

extern int get_value();

// Scenario 1: External input as index — should WARN
void test_external_input_index() {
    int index = get_value();
    int arr[10];
    arr[index] = 0;  // WARN: may be negative or out of bounds
    (void)arr[0];
}

// Scenario 2: Known negative index — should WARN (or defer to security.ArrayBound)
void test_known_negative_index() {
    int index = -1;
    int arr[10];
    arr[index] = 0;  // Definitely negative — we skip, ArrayBound handles
    (void)arr[0];
}

// Scenario 3: Validated range — should NOT warn
void test_validated_range() {
    int index = get_value();
    int arr[10];
    if (index >= 0 && index < 10) {
        arr[index] = 0;  // OK — proven in bounds
    }
    (void)arr[0];
}

// Scenario 4: Partial validation (lower only) — should WARN for upper
void test_partial_validation() {
    int index = get_value();
    int arr[10];
    if (index >= 0) {
        arr[index] = 0;  // WARN: upper bound not validated
    }
    (void)arr[0];
}

// Scenario 5: Literal valid index — should NOT warn
void test_literal_valid_index() {
    int arr[10];
    arr[5] = 0;  // OK
    (void)arr[0];
}

// Scenario 6: Loop variable — should NOT warn (CSA tracks constraints)
void test_loop_variable() {
    int arr[10];
    for (int i = 0; i < 10; i++) {
        arr[i] = i;  // OK — CSA tracks loop variable
    }
    (void)arr[0];
}
