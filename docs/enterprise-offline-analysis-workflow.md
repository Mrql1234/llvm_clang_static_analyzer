# 第三方企业内网 Windows 环境下的大型 VS/C++ 项目离线缺陷检测流程

## 1. 适用场景

本文档面向如下场景：

- 在第三方企业内网环境中开展缺陷检测
- 目标机器为 Windows 系统，通常不联网
- 待分析项目为大型 Visual Studio / C++ 工程
- 使用当前项目中定制的 `clang` / `clang-tidy` / CSA Checker / CTU 能力

本文档的目标不是只说明“如何跑一个命令”，而是给出一套可落地的工程流程，便于现场实施、批量执行、结果交付和问题排查。

---

## 2. 总体策略

推荐采用“两套交付物”的方式进入企业现场：

1. **优先方案：预编译工具包**
   - 提前在外部联网环境构建好 Windows 版工具
   - 现场直接部署使用
   - 风险最低，实施效率最高

2. **备选方案：源码包 + 离线构建环境**
   - 用于企业要求必须现场源码安装或工具需要二次构建的情况
   - 需要额外准备 Visual Studio Build Tools、CMake、Ninja、Python 等离线安装资源

对 LLVM/Clang 这类大型工程，**不建议把“现场源码编译”作为首选方案**。离线环境下构建成本高、失败点多、排障效率低。

---

## 3. 准备阶段

### 3.1 需要提前准备的离线交付目录

建议整理为如下结构：

```text
offline-delivery/
├── tool-prebuilt-win/
│   ├── bin/
│   │   ├── clang.exe
│   │   ├── clang-cl.exe
│   │   ├── clang-tidy.exe
│   │   ├── clang-extdef-mapping.exe
│   │   └── 相关 dll
│   └── lib/
├── llvm-project-personal-src/
│   └── 当前定制版源码
├── installers/
│   ├── cmake-*.msi 或 zip
│   ├── ninja-win.zip
│   ├── python-3.x.exe
│   ├── git.exe（可选）
│   └── vs_buildtools_offline/
├── scripts/
│   ├── build_tool_windows.ps1
│   ├── run_warnings.ps1
│   ├── run_tidy.ps1
│   ├── run_csa_single_tu.ps1
│   ├── run_csa_ctu.ps1
│   └── summarize_results.ps1
├── configs/
│   ├── checks_warning.txt
│   ├── checks_tidy_fast.txt
│   ├── checks_csa_high_conf.txt
│   └── checks_csa_alpha.txt
└── docs/
    ├── 安装说明.md
    ├── 运行说明.md
    ├── CTU说明.md
    └── 缺陷清单与对应工具.md
```

### 3.2 必须提前带上的程序

至少应准备：

- `clang.exe`
- `clang-cl.exe`
- `clang-tidy.exe`
- `clang-extdef-mapping.exe`
- 已集成自定义 checker 的工具二进制
- CMake
- Ninja
- Python 3
- Visual Studio Build Tools 离线安装源，或确认对方机器已有完整 C++ 构建环境

### 3.3 现场前需要确认的信息

建议提前向企业确认：

- 项目源码是否完整可读
- 是否包含项目自有头文件
- 是否包含项目依赖的第三方头文件
- 是否包含构建过程中生成的头文件
- 是否能提供 `compile_commands.json`
- 项目构建方式是 CMake 还是 `.sln/.vcxproj`
- Visual Studio 版本和 MSVC 工具链版本
- Windows SDK 版本
- 是否使用预编译头（PCH）
- 是否允许在本地磁盘生成 AST、日志、中间结果
- 是否需要全项目扫描，还是只分析指定模块

---

## 4. 依赖说明

### 4.1 为什么必须拿到第三方头文件

所谓“项目编译时依赖的第三方头文件”，是指项目源码中 `#include` 的外部依赖头文件，例如：

```cpp
#include <boost/asio.hpp>
#include <opencv2/opencv.hpp>
#include <json/json.h>
#include "mysql/mysql.h"
```

如果这些头文件缺失，分析器在解析某个 `.cpp` 时就会直接报：

```text
fatal error: 'boost/asio.hpp' file not found
```

因此这类头文件是**静态分析能够成功启动的前提**。

### 4.2 为什么必须拿到生成头文件

“生成头文件”是指不是手工维护、而是构建时自动产生的头文件，例如：

- `config.h`
- `project_config.h`
- `version.h`
- `user.pb.h`
- `rpc_service.h`
- `generated/*.h`
- `moc_xxx.h`

这类文件通常由：

- CMake configure
- protobuf / thrift / gRPC
- Qt moc / uic / rcc
- 企业内部代码生成器

自动生成。

如果这些文件缺失，分析器同样会在预处理或语义分析阶段失败，或由于缺少关键宏与类型定义导致 AST 失真、误报、漏报。

### 4.3 为什么强烈建议提供 `compile_commands.json`

对大型项目来说，`compile_commands.json` 基本是最重要的接入资产之一，因为它通常包含：

- `-I` 第三方头文件目录
- `-I` 生成头文件目录
- `-D` 宏定义
- `/FI` 或强制包含头文件
- PCH 相关参数
- 编译语言标准
- 平台与架构参数

如果缺少它，就需要人工还原每个翻译单元的编译参数，现场成本很高。

---

## 5. 安装阶段

## 5.1 Windows 机器需要的环境

如果企业现场需要源码安装工具，目标机器建议具备：

- Visual Studio 2022 Build Tools 或完整 VS2022
- MSVC C++ 编译工具链
- Windows 10/11 SDK
- CMake
- Ninja
- Python 3

建议硬件配置：

- 内存：至少 16GB，推荐 32GB
- 磁盘：至少预留 50GB-100GB
- CPU：8 核以上更合适

## 5.2 Visual Studio Build Tools 离线安装

如果目标机器没有 C++ 构建环境，可提前在外部联网机器制作离线安装源：

```bat
vs_BuildTools.exe --layout D:\vs2022offline ^
  --add Microsoft.VisualStudio.Workload.VCTools ^
  --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
  --add Microsoft.VisualStudio.Component.Windows11SDK.22621 ^
  --add Microsoft.VisualStudio.Component.VC.CMake.Project ^
  --includeRecommended --lang en-US
```

现场安装命令：

```bat
D:\vs2022offline\vs_BuildTools.exe --noweb --wait --norestart ^
  --installPath C:\BuildTools ^
  --add Microsoft.VisualStudio.Workload.VCTools ^
  --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
  --add Microsoft.VisualStudio.Component.Windows11SDK.22621 ^
  --add Microsoft.VisualStudio.Component.VC.CMake.Project
```

## 5.3 CMake / Ninja / Python 安装

示例：

```bat
cmake-3.xx-windows-x86_64.msi
python-3.x-amd64.exe
```

Ninja 可直接解压：

```bat
tar -xf ninja-win.zip -C C:\tools\ninja
```

## 5.4 定制工具源码构建命令

在 `x64 Native Tools Command Prompt for VS 2022` 中执行：

```bat
cd C:\work\llvm-project-personal

cmake -S llvm -B build-win -G Ninja ^
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DLLVM_TARGETS_TO_BUILD="X86" ^
  -DLLVM_ENABLE_ASSERTIONS=OFF

ninja -C build-win clang clang-cl clang-tidy clang-extdef-mapping
```

若自定义 checker 已合入 `clang` / `clang-tools-extra`，本步骤会一起构建。

## 5.5 安装策略建议

推荐顺序如下：

1. 首选部署预编译工具包
2. 仅在必须现场构建时再启用源码安装方案

---

## 6. 项目接入阶段

### 6.1 对方项目至少需要提供什么

对于大型 VS/C++ 项目，通常至少需要：

- 项目源码
- 项目自有头文件
- 第三方头文件
- 生成头文件
- 编译宏定义
- 包含目录
- 平台相关参数
- 预编译头配置
- 最好提供 `compile_commands.json`

### 6.2 只拿 `.cpp` 文件为什么不够

因为静态分析不是简单读文本，而是需要像编译器一样完成：

1. 预处理
2. 头文件展开
3. 宏替换
4. 语法解析
5. 语义分析
6. 生成 AST
7. 再做 checker 分析

如果缺任何关键依赖，分析往往在前几步就失败。

### 6.3 针对 Visual Studio 项目的处理

对方项目若使用 `.sln/.vcxproj`，重点不在“是不是 VS 项目”，而在于能否获得每个翻译单元的完整编译参数。

推荐处理顺序：

1. 若项目支持 CMake，优先导出 `compile_commands.json`
2. 若项目只有 `.sln/.vcxproj`，则提取：
   - `AdditionalIncludeDirectories`
   - `PreprocessorDefinitions`
   - `LanguageStandard`
   - `ForcedIncludeFiles`
   - `PrecompiledHeader`
3. 基于以上信息生成 `compile_commands.json`

若没有编译数据库，`clang-tidy` 和 CTU 在大型项目上的运行成本会显著增加。

---

## 7. 运行策略

不建议把 17 种缺陷一次性全部打开进行全仓扫描。推荐分批执行，以控制：

- 性能开销
- 误报噪声
- 结果可读性
- 排查效率

建议分为 4 批。

---

## 8. 批次一：编译器 Warning，低成本全仓扫描

### 8.1 检测类型

- `#9` 无符号数赋负值
- `#17` 有符号数与无符号数比较

### 8.2 对应能力

- `-Wsign-conversion`
- `-Wsign-compare`

### 8.3 适用策略

- 先扫全项目
- 成本低
- 速度快
- 结果稳定

---

## 9. 批次二：clang-tidy AST 类检查

### 9.1 检测类型

- `#3` 死循环
- `#5` 递归栈溢出
- `#8` 浮点数等号比较
- `#10` 反三角函数参数范围
- `#11` 浮点转整数溢出
- `#12` 大局部变量栈溢出
- `#16` 浮点精度误用

### 9.2 对应检查

- `bugprone-infinite-loop`
- `bugprone-loop-external-dependency`
- `bugprone-unbounded-recursion`
- `bugprone-float-equal-comparison`
- `bugprone-math-domain-guard`
- `bugprone-narrowing-conversions`
- `bugprone-large-stack-variable`
- `bugprone-float-precision-promotion`
- `bugprone-float-precision-loss`
- `bugprone-float-literal-suffix`

### 9.3 适用策略

- 不依赖 CTU
- 适合全项目或指定模块批量运行
- 推荐作为第二批做全局扫描

---

## 10. 批次三：CSA 单 TU 高置信度检查

### 10.1 检测类型

- `#1` 数组越界
- `#2` 除以零
- `#4/#13/#14` cstring/memcpy/memset 越界
- `#6` 空指针解引用
- `#7` 未初始化变量

### 10.2 对应 checker

- `core.DivideZero`
- `core.NullDereference`
- `security.ArrayBound`
- `core.uninitialized.*`
- `alpha.unix.cstring.OutOfBounds`

### 10.3 适用策略

- 先不启用 CTU
- 先拿高置信度结果
- 推荐对全项目执行

---

## 11. 批次四：CSA + CTU 跨翻译单元深挖

### 11.1 适合开启 CTU 的缺陷

优先建议：

- 除以零
- 可能除以零
- 数组越界
- 可能数组越界
- 空指针解引用
- 未初始化变量
- cstring 越界

### 11.2 对应 checker

- `core.DivideZero`
- `alpha.core.PossibleDivideZero`
- `security.ArrayBound`
- `alpha.security.PossibleArrayBound`
- `core.NullDereference`
- `core.uninitialized.*`
- `alpha.unix.cstring.OutOfBounds`

### 11.3 使用策略

CTU 不建议直接全仓开启。更适合用于：

- 单 TU 分析已发现疑点的模块
- 核心业务模块
- 安全关键模块
- 改动较大模块
- 企业特别关注的子系统

---

## 12. 推荐执行顺序

建议采用如下顺序：

1. 环境验证
2. 生成或确认 `compile_commands.json`
3. 运行 Warning 批次
4. 运行 clang-tidy 批次
5. 运行 CSA 单 TU 批次
6. 对重点模块运行 CSA + CTU
7. 汇总并去重结果
8. 生成交付报告

---

## 13. 现场运行前验证

### 13.1 工具验证

建议先执行：

```bat
clang-tidy --version
clang-cl --version
clang-extdef-mapping --help
```

### 13.2 Demo 验证

建议现场先运行一套最小 demo，确认：

- warning 正常输出
- clang-tidy 可运行
- CSA 正常工作
- CTU 可成功导入外部 AST

### 13.3 项目接入验证

在企业项目中随机挑选一个 `.cpp`，先验证：

- 头文件是否完整
- 宏是否齐全
- PCH 是否处理正确
- Windows SDK 是否匹配
- 编译参数是否可复现

只有这一步通过，再进行大规模批处理。

---

## 14. 批量执行建议

### 14.1 必须使用脚本

对于大型项目，建议至少准备：

- `run_warnings.ps1`
- `run_tidy.ps1`
- `run_csa_single_tu.ps1`
- `run_csa_ctu.ps1`
- `merge_reports.ps1`

### 14.2 结果目录建议

```text
analysis-output/
├── warnings/
├── tidy/
├── csa-single-tu/
├── csa-ctu/
├── logs/
└── summary/
```

### 14.3 结果分组建议

建议结果按以下维度拆分：

- 工具类型
- 缺陷类型
- 模块
- 严重级别

便于与企业沟通、做误报确认与二次复核。

---

## 15. CTU 的工程注意事项

CTU 能显著增强部分 CSA checker 的跨文件分析能力，但不是万能方案。需要明确以下限制：

- 只能分析能拿到源码并成功生成 AST 的翻译单元
- 对闭源第三方库通常无法穿透
- 对虚调用、函数指针、多态分发的效果有限
- 性能开销明显高于单 TU
- 中间产物较多，需要磁盘空间
- 编译数据库不完整时极易失败

因此 CTU 更适合作为**第二阶段精准分析手段**，而不是默认全仓模式。

---

## 16. 推荐交付物

建议最终向企业交付以下内容：

### 16.1 工具包

包含：

- 定制版 `clang`
- 定制版 `clang-cl`
- 定制版 `clang-tidy`
- `clang-extdef-mapping`

### 16.2 运行脚本

至少包括：

- warning 批量脚本
- clang-tidy 批量脚本
- CSA 单 TU 批量脚本
- CSA CTU 批量脚本

### 16.3 配置文件

例如：

- `checks_tidy_fast.txt`
- `checks_csa_high_conf.txt`
- `checks_csa_alpha.txt`

### 16.4 使用说明文档

建议包含：

- 安装依赖
- 如何生成编译数据库
- 每批次检测哪些缺陷
- 如何解释输出结果
- 如何做误报确认

---

## 17. 现场实施的推荐工作流

如果由具备经验的工程师进入企业现场，推荐按如下方式执行：

1. 带上预编译工具包、源码包、安装包、脚本和 demo
2. 在企业 Windows 机器安装或确认 VS Build Tools、CMake、Ninja、Python 可用
3. 验证定制版 `clang/clang-tidy` 可运行
4. 获取企业项目源码、头文件、生成头文件和编译数据库
5. 先运行 warning 与 clang-tidy，做低成本全局扫描
6. 再运行 CSA 单 TU，拿高置信度问题
7. 最后仅对重点模块运行 CTU，深挖跨文件缺陷
8. 生成按工具类型、缺陷类型、模块分类的交付报告

---

## 18. 关键成功因素

这套工具要在第三方企业内网环境中稳定落地，最关键的不是单个 checker 本身，而是以下两点：

1. 能否稳定获取 `compile_commands.json`
2. 能否把 17 种缺陷拆解成合理的多批执行流程

如果这两点不到位，现场实施的复杂度会显著上升。

---

## 19. 附：17 种缺陷与执行批次建议

| 批次 | 检测类型 |
|------|----------|
| 批次一 | `#9`, `#17` |
| 批次二 | `#3`, `#5`, `#8`, `#10`, `#11`, `#12`, `#16` |
| 批次三 | `#1`, `#2`, `#4`, `#6`, `#7`, `#13`, `#14` |
| 批次四 | 对批次三中的重点模块启用 CTU 深挖 |

