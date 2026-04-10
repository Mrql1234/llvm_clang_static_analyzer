// main.cpp — 应用层，调用其他模块的函数
// 缺陷的根因分布在 data_provider.cpp 和 math_utils.cpp 中
// 普通单 TU 分析无法检出，CTU 分析应能检出
#include "math_utils.h"
#include "data_provider.h"

// 缺陷 1: 跨 3 个文件的除以零
//   main.cpp -> data_provider.cpp -> math_utils.cpp
//   fetch_value() 返回 50 -> compute_divisor(50) 返回 0 -> safe_divide(100, 0)
void defect_div_zero_cross_tu() {
    int val = fetch_value();           // 定义在 data_provider.cpp, 返回 50
    int divisor = compute_divisor(val); // 定义在 math_utils.cpp, 返回 0
    int result = safe_divide(100, divisor); // 定义在 math_utils.cpp, 除以 0
}

// 缺陷 2: 跨 2 个文件的数组越界
//   main.cpp -> data_provider.cpp
//   fetch_index() 返回 5 -> get_array_index(5) 返回 50 -> arr[50] 越界
void defect_array_oob_cross_tu() {
    int arr[10];
    int raw_idx = fetch_index();       // 定义在 data_provider.cpp, 返回 5
    int idx = get_array_index(raw_idx); // 定义在 math_utils.cpp, 返回 50
    arr[idx] = 99;                      // 越界访问
}

// 缺陷 3: 跨 2 个文件的未初始化变量
//   main.cpp -> data_provider.cpp
//   init_record(&record) 没有给 record 赋值
void defect_uninit_cross_tu() {
    int record;
    init_record(&record);              // 定义在 data_provider.cpp, 忘记赋值
    int x = record + 1;               // 使用未初始化的值
}

// 对照组: 同文件内的缺陷（单 TU 就能检出）
int local_get_zero() { return 0; }

void defect_div_zero_same_tu() {
    int d = local_get_zero();
    int x = 100 / d;                   // 单 TU 分析就能检出
}
