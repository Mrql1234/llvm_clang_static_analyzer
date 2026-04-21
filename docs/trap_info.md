17 种缺陷 CTU 矩阵
#	缺陷	当前实现	是否适用 CTU	说明
1	数组越界	security.ArrayBound	是	CSA checker，若下标/长度来自别的 .cpp，CTU 可能提升检出
2	除以零	core.DivideZero	是	CSA checker，若除数在别的 TU 中被计算为 0，CTU 有帮助
3	死循环	bugprone-infinite-loop + bugprone-loop-external-dependency	否	clang-tidy，不走 CSA CTU
4	缓冲区读写越界 (strcpy/strcat)	alpha.unix.cstring.OutOfBounds	是	CSA checker，若目标缓冲区大小/长度信息跨 TU 传播，CTU 可能有帮助
5	递归栈溢出	bugprone-unbounded-recursion	否	clang-tidy；虽然它自己做调用图分析，但不是 CSA CTU
6	空指针解引用 / 使用前非空判断	core.NullDereference	是	CSA checker，若空值来自别的 TU，CTU 可能提升检出
7	变量使用前未初始化	core.uninitialized.*	是	CSA checker，若初始化/未初始化状态跨 TU 传播，CTU 有帮助
8	浮点数等号比较	bugprone-float-equal-comparison	否	clang-tidy
9	无符号数赋负值	-Wsign-conversion	否	编译器 warning
10	反三角函数参数范围	bugprone-math-domain-guard	否	clang-tidy
11	浮点数转整数溢出	bugprone-narrowing-conversions	否	clang-tidy
12	函数内大局部变量栈溢出	bugprone-large-stack-variable	否	clang-tidy
13	memcpy 目标空间不足	alpha.unix.cstring.OutOfBounds	是	CSA checker，和 #4 同类
14	memset 目标空间不足	alpha.unix.cstring.OutOfBounds	是	CSA checker，和 #4 同类
15	sqrt 输入为负数	bugprone-math-domain-guard	否	clang-tidy
16	浮点运算使用双精度 / 精度误用	bugprone-float-precision-*	否	clang-tidy
17	有符号数与无符号数比较	-Wsign-compare	否	编译器 warning
额外补充：你们新增的两个“可能缺陷”增强 checker
这两个虽然不在 17 种原始 defect 编号里，但你前面专门实现过，单独说一下：

Checker	是否适用 CTU	说明
alpha.core.PossibleDivideZero	是	本质仍是 CSA checker，CTU 可以让“可能为 0”的值跨 TU 传播
alpha.security.PossibleArrayBound	是	本质仍是 CSA checker，CTU 可以让跨 TU 的下标/长度关系更完整
哪几类最值得上 CTU
如果你要优先把 CTU 用在真实项目里，最有价值的是这些：

core.DivideZero
alpha.core.PossibleDivideZero
security.ArrayBound
alpha.security.PossibleArrayBound
core.NullDereference
core.uninitialized.*
alpha.unix.cstring.OutOfBounds
原因很简单：这些都是 “值流/路径条件”驱动 的问题，而 CTU 正是补“跨 .cpp 的值流断点”。

哪几类即使开 CTU 也基本没变化
这些开 CTU 基本不会变强：

bugprone-math-domain-guard
bugprone-float-equal-comparison
bugprone-large-stack-variable
bugprone-narrowing-conversions
bugprone-float-precision-*
bugprone-infinite-loop
bugprone-loop-external-dependency
bugprone-unbounded-recursion
-Wsign-conversion
-Wsign-compare
因为它们压根不是 CSA CTU 的执行链路。

一个更实用的结论
如果你问的是“我们之前做的能力里，哪些值得投入 CTU 工程化”，答案是：

优先做 CTU 的应该是 7 类 CSA 能力：

除以零
可能除以零
数组越界
可能数组越界
空指针解引用
未初始化变量
cstring 越界