// test_uninitialized.cpp — Phase 2 US1: core.uninitialized.* verification
// Run: clang++ --analyze -isysroot $(xcrun --show-sdk-path) test_uninitialized.cpp

#include <cstdlib>

// --- Positive cases (should trigger warnings) ---

// core.uninitialized.Assign: use uninitialized value in assignment
int test_assign() {
    int x;
    int y = x;  // expected: core.uninitialized.Assign
    return y;
}

// core.uninitialized.Branch: uninitialized value as branch condition
int test_branch() {
    int x;
    if (x > 0) {  // expected: core.uninitialized.Branch
        return 1;
    }
    return 0;
}

// core.uninitialized.UndefReturn: return uninitialized value
int test_undef_return() {
    int x;
    return x;  // expected: core.uninitialized.UndefReturn
}

// core.uninitialized.ArraySubscript: uninitialized value as array index
int test_array_subscript() {
    int arr[10] = {0};
    int idx;
    return arr[idx];  // expected: core.uninitialized.ArraySubscript
}

// core.uninitialized.NewArraySize: uninitialized value as new[] size
void test_new_array_size() {
    int n;
    int *p = new int[n];  // expected: core.uninitialized.NewArraySize
    delete[] p;
}

// --- Negative cases (should NOT trigger warnings) ---

int test_all_paths_initialized(int cond) {
    int x;
    if (cond) {
        x = 1;
    } else {
        x = 2;
    }
    return x;  // all paths initialize x — no warning expected
}

int test_initialized_before_use() {
    int x;
    x = 42;
    return x;  // initialized before use — no warning expected
}

// --- Edge cases ---

// Partial path initialization
int test_partial_path(int cond) {
    int x;
    if (cond) {
        x = 10;
    }
    // x may be uninitialized if cond is false
    return x;  // expected: warning on some path
}

// Struct with partial member initialization
struct Point {
    int x;
    int y;
    int z;
};

int test_struct_partial_init() {
    Point p;
    p.x = 1;
    // p.y and p.z not initialized
    return p.x + p.y;  // expected: warning for p.y
}

// Array element uninitialized
int test_array_element_uninit() {
    int arr[5];
    arr[0] = 1;
    arr[1] = 2;
    // arr[2], arr[3], arr[4] not initialized
    return arr[3];  // expected: warning
}

// Indirect uninitialized via pointer
int test_pointer_indirect() {
    int x;
    int *p = &x;
    return *p;  // x is uninitialized, accessed via pointer — expected: warning
}

int main() {
    test_assign();
    test_branch();
    test_undef_return();
    test_array_subscript();
    test_new_array_size();
    test_all_paths_initialized(1);
    test_initialized_before_use();
    test_partial_path(0);
    test_struct_partial_init();
    test_array_element_uninit();
    test_pointer_indirect();
    return 0;
}
