// 模式 A 的实现文件：函数定义在 .cpp 中
#include "utils_decl_only.h"

int get_zero() { return 0; }
int get_negative() { return -1; }
int compute_index(int n) { return n * 10; }
void no_init(int *p) { /* 故意不赋值 */ }
