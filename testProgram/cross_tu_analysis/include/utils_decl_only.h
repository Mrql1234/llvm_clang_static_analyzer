// 模式 A：头文件里只有声明，定义在 .cpp 中
// 这是大型 C++ 项目最常见的做法
#pragma once

int get_zero();
int get_negative();
int compute_index(int n);
void no_init(int *p);
