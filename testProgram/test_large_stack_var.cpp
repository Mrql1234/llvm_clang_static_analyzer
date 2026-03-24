#include <cstdlib>
#include <alloca.h>

// WARN: Large array (~4MB exceeds default 1MB threshold)
void test_large_array() {
    int arr[1000000];  // 4,000,000 bytes
    arr[0] = 1;
}

// OK: Small buffer (256 bytes, well under threshold)
void test_small_buffer() {
    char buf[256];
    buf[0] = 'a';
}

// WARN: VLA with runtime size (unpredictable stack usage)
void test_vla(int n) {
    int arr[n];
    arr[0] = 1;
}

// OK: Multiple small variables that individually are under threshold
void test_multiple_small() {
    char a[1024];   // 1KB
    char b[1024];   // 1KB
    char c[1024];   // 1KB
    a[0] = b[0] = c[0] = 0;
}

// WARN with custom threshold 1024: 512 ints = 2048 bytes > 1024
void test_threshold_small() {
    int arr[512];   // 2,048 bytes
    arr[0] = 1;
}

// WARN: alloca usage
void test_alloca(int n) {
    void *p = alloca(n);
    (void)p;
}

int main() { return 0; }
