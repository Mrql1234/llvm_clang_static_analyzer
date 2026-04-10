// 情况二的辅助文件：定义在另一个 .cpp 中的函数
// 这些函数的实现对 main 文件的分析器不可见

int ext_return_zero() { return 0; }
int ext_return_negative() { return -1; }
int ext_compute_index(int n) { return n * 10; }

void ext_no_init(int *p) {
    // 故意不给 *p 赋值
}

static int ext_chain_inner() { return 0; }
int ext_chain_outer() { return ext_chain_inner(); }
