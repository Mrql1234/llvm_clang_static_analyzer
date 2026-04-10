// test_cstring_bounds.cpp — Phase 2 US2: alpha.unix.cstring.OutOfBounds verification
// Run: clang++ --analyze -Xanalyzer -analyzer-checker=alpha.unix.cstring.OutOfBounds \
//      -isysroot $(xcrun --show-sdk-path) test_cstring_bounds.cpp

#include <cstring>
#include <cstdlib>

// --- Positive cases (should trigger warnings) ---

// strcpy: destination smaller than source
void test_strcpy_overflow() {
    char dst[4];
    char src[] = "Hello, World!";
    strcpy(dst, src);  // expected: out-of-bounds warning
}

// strcat: concatenation exceeds destination capacity
void test_strcat_overflow() {
    char dst[8] = "Hello";
    char src[] = ", World!";
    strcat(dst, src);  // expected: out-of-bounds warning (5 + 8 + 1 = 14 > 8)
}

// memcpy: n > sizeof(dst)
void test_memcpy_overflow() {
    char dst[4];
    char src[] = "0123456789ABCDEF";
    memcpy(dst, src, 16);  // expected: out-of-bounds warning
}

// memset: n > sizeof(buf)
void test_memset_overflow() {
    char buf[4];
    memset(buf, 0, 16);  // expected: out-of-bounds warning
}

// --- Negative cases (should NOT trigger warnings) ---

void test_strcpy_safe() {
    char dst[32];
    char src[] = "Hello";
    strcpy(dst, src);  // safe — no warning expected
}

void test_strcat_safe() {
    char dst[32] = "Hello";
    char src[] = ", World!";
    strcat(dst, src);  // safe — 5 + 8 + 1 = 14 < 32
}

void test_memcpy_safe() {
    char dst[16];
    char src[8] = "ABCDEFG";
    memcpy(dst, src, 8);  // safe — 8 <= 16
}

void test_memset_safe() {
    char buf[16];
    memset(buf, 0, 16);  // safe — exact size
}

// --- Edge cases ---

// Dynamic allocation via malloc
void test_malloc_dynamic() {
    char *dst = (char *)malloc(4);
    char src[] = "Hello, World!";
    if (dst) {
        strcpy(dst, src);  // expected: warning (4 < strlen("Hello, World!") + 1)
        free(dst);
    }
}

// Symbolic length from external input (CSA may not track)
void test_symbolic_length(int n) {
    char buf[10];
    if (n > 10) {
        memset(buf, 0, n);  // may or may not be detected (symbolic)
    }
}

// strncpy safe variant
void test_strncpy_safe() {
    char dst[8];
    char src[] = "Hello, World!";
    strncpy(dst, src, sizeof(dst) - 1);
    dst[sizeof(dst) - 1] = '\0';  // safe use of strncpy
}

// strncat safe variant
void test_strncat_safe() {
    char dst[16] = "Hello";
    char src[] = ", World!";
    strncat(dst, src, sizeof(dst) - strlen(dst) - 1);  // safe use
}

// sizeof with wrong type (common C pitfall)
void test_sizeof_wrong_type() {
    int arr[4];
    // sizeof(arr) = 16 bytes, but developer may think it's 4 elements
    memset(arr, 0, sizeof(arr));  // safe — sizeof gives byte count
}

// sizeof pointer vs array
void test_sizeof_pointer() {
    char buf[64];
    char *p = buf;
    memset(p, 0, sizeof(p));  // sizeof(p) = 8 on 64-bit, not 64 — logic bug but safe direction
}

int main() {
    test_strcpy_overflow();
    test_strcat_overflow();
    test_memcpy_overflow();
    test_memset_overflow();
    test_strcpy_safe();
    test_strcat_safe();
    test_memcpy_safe();
    test_memset_safe();
    test_malloc_dynamic();
    test_symbolic_length(20);
    test_strncpy_safe();
    test_strncat_safe();
    test_sizeof_wrong_type();
    test_sizeof_pointer();
    return 0;
}
