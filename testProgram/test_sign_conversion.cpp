// test_sign_conversion.cpp — Phase 2 US3: -Wsign-conversion / -Wsign-compare verification
// Run: clang++ -fsyntax-only -Wsign-conversion -Wsign-compare \
//      -isysroot $(xcrun --show-sdk-path) test_sign_conversion.cpp

#include <cstddef>
#include <vector>

// --- Positive cases: -Wsign-conversion ---

void test_negative_to_unsigned() {
    int neg = -1;
    unsigned int u = neg;  // expected: -Wsign-conversion
    (void)u;
}

void test_signed_literal_to_unsigned() {
    unsigned int u = -5;  // expected: -Wsign-conversion
    (void)u;
}

// --- Positive cases: -Wsign-compare ---

bool test_signed_unsigned_compare(int a, unsigned int b) {
    return a < b;  // expected: -Wsign-compare
}

bool test_signed_unsigned_equality(int x, unsigned int y) {
    return x == y;  // expected: -Wsign-compare
}

bool test_signed_unsigned_greater(int a, unsigned int b) {
    return a > b;  // expected: -Wsign-compare
}

// --- Negative cases (should NOT trigger warnings) ---

void test_matching_types() {
    int a = 1, b = 2;
    bool r = a < b;  // same type — no warning
    unsigned int u1 = 1, u2 = 2;
    bool r2 = u1 < u2;  // same type — no warning
    (void)r; (void)r2;
}

void test_safe_unsigned_assignment() {
    unsigned int u = 42;  // positive literal — no warning expected
    (void)u;
}

// --- Edge cases ---

// Implicit type promotion: char participating in unsigned int arithmetic
void test_char_unsigned_promotion() {
    char c = 'A';
    unsigned int u = 100;
    unsigned int result = u + c;  // expected: -Wsign-conversion (char is signed on most platforms)
    (void)result;
}

// Template with signed/unsigned comparison
template <typename T>
bool compare_with_size(T val, std::size_t sz) {
    return val < sz;  // expected: -Wsign-compare when T=int
}

void test_template_compare() {
    int x = 5;
    std::size_t sz = 10;
    compare_with_size(x, sz);
}

// size_t vs int loop counter — extremely common C++ pattern
void test_loop_counter_compare() {
    std::vector<int> vec = {1, 2, 3};
    for (int i = 0; i < vec.size(); i++) {  // expected: -Wsign-compare
        (void)vec[i];
    }
}

// static_cast<unsigned>(-1) — explicit intent
void test_explicit_cast() {
    unsigned int max_val = static_cast<unsigned int>(-1);  // explicit cast — may or may not warn
    (void)max_val;
}

int main() {
    test_negative_to_unsigned();
    test_signed_literal_to_unsigned();
    test_signed_unsigned_compare(1, 2);
    test_signed_unsigned_equality(1, 2);
    test_signed_unsigned_greater(1, 2);
    test_matching_types();
    test_safe_unsigned_assignment();
    test_char_unsigned_promotion();
    test_template_compare();
    test_loop_counter_compare();
    test_explicit_cast();
    return 0;
}
