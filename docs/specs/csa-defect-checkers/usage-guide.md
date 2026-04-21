# CSA & clang-tidy 使用命令参考

**适用环境**: macOS (arm64), 自编译 LLVM/Clang 23.0.0git  
**仓库根目录**: `/Users/yqg/codex_chat/llvm-project-personal/`

---

## 一、环境变量（建议写入 ~/.zshrc）

```bash
export LLVM_BUILD="/Users/yqg/codex_chat/llvm-project-personal/build-csa"
export CLANG="$LLVM_BUILD/bin/clang++"
export CLANG_TIDY="$LLVM_BUILD/bin/clang-tidy"
export SDK_PATH="$(xcrun --show-sdk-path)"

# macOS SDK 相关参数（自编译 clang 不自动查找系统头文件）
export CSA_SDK_FLAGS="-isysroot $SDK_PATH -I$SDK_PATH/usr/include/c++/v1"
```

---

## 二、Clang Static Analyzer (CSA)

### 2.1 基本分析命令

```bash
# 分析单个 C++ 文件
$CLANG --analyze $CSA_SDK_FLAGS -Xanalyzer -analyzer-output=text <file.cpp>

# 不显示反例路径
 $CLANG --analyze $CSA_SDK_FLAGS  testProgram/demo.cpp
 
 $和CLANG之间不能有空格

# 示例
$CLANG --analyze $CSA_SDK_FLAGS -Xanalyzer -analyzer-output=text testProgram/test_divide_zero.cpp
```

### 2.2 启用特定 Checker

```bash
# 启用 security.ArrayBound（默认未启用）
$CLANG --analyze $CSA_SDK_FLAGS \
  -Xanalyzer -analyzer-output=text \
  -Xanalyzer -analyzer-checker=security.ArrayBound \
  <file.cpp>

# 启用自定义 MathDomainChecker（alpha 前缀，需显式启用）
$CLANG --analyze $CSA_SDK_FLAGS \
  -Xanalyzer -analyzer-output=text \
  -Xanalyzer -analyzer-checker=alpha.security.MathDomain \
  <file.cpp>

# 同时启用多个 Checker
$CLANG --analyze $CSA_SDK_FLAGS \
  -Xanalyzer -analyzer-output=text \
  -Xanalyzer -analyzer-checker=security.ArrayBound \
  -Xanalyzer -analyzer-checker=alpha.security.MathDomain \
  <file.cpp>
```

### 2.3 查看可用 Checker 列表

```bash
# 查看所有已注册的 Checker
$CLANG -cc1 -analyzer-checker-help

# 查看 alpha（实验性）Checker
$CLANG -cc1 -analyzer-checker-help-alpha

# 搜索特定 Checker
$CLANG -cc1 -analyzer-checker-help-alpha 2>&1 | grep MathDomain
```

### 2.4 输出格式

```bash
# 文本输出（终端友好）
-Xanalyzer -analyzer-output=text

# plist 输出（可被 IDE 消费）
-Xanalyzer -analyzer-output=plist

# HTML 报告（生成到指定目录）
-Xanalyzer -analyzer-output=html -o /tmp/csa-report/
```

### 2.5 CSA 已有的常用 Checker

| Checker | 默认启用 | 检测内容 |
|---------|----------|----------|
| `core.DivideZero` | ✅ | 除以零 |
| `core.NullDereference` | ✅ | 空指针解引用 |
| `core.uninitialized.Assign` | ✅ | 使用未初始化变量 |
| `security.ArrayBound` | ❌ 需显式 | 数组越界 |
| `alpha.security.MathDomain` | ❌ 需显式 | sqrt/asin/acos 域错误（自定义） |

---

## 三、clang-tidy

### 3.1 基本分析命令

```bash
# 运行指定 Check 分析文件
$CLANG_TIDY -checks='-*,bugprone-large-stack-variable' <file.cpp> \
  -- $CSA_SDK_FLAGS

# 示例
$CLANG_TIDY -checks='-*,bugprone-large-stack-variable' testProgram/test_large_stack_var.cpp \
  -- $CSA_SDK_FLAGS
```

**注意**: `--` 后面是传给编译器的参数（如 SDK 路径），`--` 前面是 clang-tidy 自身的参数。

### 3.2 运行多个 Check

```bash
# 同时运行两个自定义 Check
$CLANG_TIDY -checks='-*,bugprone-large-stack-variable,bugprone-float-equal-comparison' \
  <file.cpp> -- $CSA_SDK_FLAGS

# 运行所有 bugprone 类 Check
$CLANG_TIDY -checks='-*,bugprone-*' <file.cpp> -- $CSA_SDK_FLAGS
```

### 3.3 配置 Check 选项

```bash
# 设置大局部变量阈值为 64KB（嵌入式场景）
$CLANG_TIDY \
  -checks='-*,bugprone-large-stack-variable' \
  -config="{CheckOptions: [{key: bugprone-large-stack-variable.LargeStackVariableThreshold, value: 65536}]}" \
  <file.cpp> -- $CSA_SDK_FLAGS

# 设置阈值为 1KB
$CLANG_TIDY \
  -checks='-*,bugprone-large-stack-variable' \
  -config="{CheckOptions: [{key: bugprone-large-stack-variable.LargeStackVariableThreshold, value: 1024}]}" \
  <file.cpp> -- $CSA_SDK_FLAGS
```

### 3.4 查看可用 Check 列表

```bash
# 列出所有已注册的 Check
$CLANG_TIDY -list-checks -checks='*'

# 列出 bugprone 类
$CLANG_TIDY -list-checks -checks='bugprone-*'

# 搜索自定义 Check
$CLANG_TIDY -list-checks -checks='bugprone-*' 2>&1 | grep -E 'large-stack|float-equal'
```

### 3.5 自定义 Check 说明

| Check | 检测内容 | 可配选项 |
|-------|----------|----------|
| `bugprone-large-stack-variable` | 局部变量栈空间过大、VLA、alloca | `LargeStackVariableThreshold`（默认 1048576 = 1MB） |
| `bugprone-float-equal-comparison` | 浮点数 == / != 比较 | 无（自动排除 `x != x` NaN 检测写法） |

---

## 四、实际使用示例

### 4.1 分析一个项目文件

```bash
# 对某个 C++ 文件同时运行 CSA 和 clang-tidy
echo "=== CSA 分析 ==="
$CLANG --analyze $CSA_SDK_FLAGS \
  -Xanalyzer -analyzer-output=text \
  -Xanalyzer -analyzer-checker=alpha.security.MathDomain \
  my_project/src/calculator.cpp

echo ""
echo "=== clang-tidy 分析 ==="
$CLANG_TIDY \
  -checks='-*,bugprone-large-stack-variable,bugprone-float-equal-comparison' \
  my_project/src/calculator.cpp \
  -- $CSA_SDK_FLAGS
```

### 4.2 使用 compile_commands.json（推荐用于项目）

如果项目有 `compile_commands.json`（CMake 生成），clang-tidy 可以自动读取编译参数：

```bash
# 不需要手动传 -- 后的参数
$CLANG_TIDY -p build/ \
  -checks='-*,bugprone-large-stack-variable,bugprone-float-equal-comparison' \
  src/calculator.cpp
```

### 4.3 批量分析目录下所有文件

```bash
# 分析 src/ 下所有 .cpp 文件（clang-tidy）
find src/ -name '*.cpp' | xargs -I{} $CLANG_TIDY \
  -checks='-*,bugprone-large-stack-variable,bugprone-float-equal-comparison' \
  {} -- $CSA_SDK_FLAGS

# 分析 src/ 下所有 .cpp 文件（CSA）
find src/ -name '*.cpp' -exec \
  $CLANG --analyze $CSA_SDK_FLAGS \
  -Xanalyzer -analyzer-output=text \
  -Xanalyzer -analyzer-checker=alpha.security.MathDomain \
  {} \;
```

### 4.4 运行回归测试

```bash
cd /Users/yqg/codex_chat/llvm-project-personal
bash testProgram/run_all_tests.sh
```

---

## 五、重新编译（修改源码后）

```bash
cd /Users/yqg/codex_chat/llvm-project-personal/build-csa

# 修改 CSA Checker 后，重新编译 clang
ninja clang        # 增量编译约 1-3 分钟

# 修改 clang-tidy Check 后，重新编译 clang-tidy
ninja clang-tidy   # 增量编译约 1-3 分钟

# 修改 Checkers.td 后（注册表），需要重新生成 TableGen
ninja clang        # 约 3-5 分钟（含 TableGen 重生成）
```

---

## 六、常见问题

### Q: `fatal error: 'iostream' file not found`
自编译的 clang 不自动查找 macOS SDK 头文件，需添加：
```bash
-isysroot "$(xcrun --show-sdk-path)" -I"$(xcrun --show-sdk-path)/usr/include/c++/v1"
```

### Q: alpha Checker 没有出现在默认分析中
alpha 前缀的 Checker 需要显式启用：
```bash
-Xanalyzer -analyzer-checker=alpha.security.MathDomain
```

### Q: clang-tidy 的 alloca 告警不显示
macOS 上 `alloca()` 是系统头文件中的宏，需添加 `-system-headers` 标志：
```bash
$CLANG_TIDY -checks='-*,bugprone-large-stack-variable' -system-headers <file> -- $CSA_SDK_FLAGS
```

### Q: 如何只看 warning 不看 note（路径信息）
CSA 的 `-analyzer-output=text` 会输出详细的路径推导信息（note 行）。如果只想看 warning 行：
```bash
$CLANG --analyze $CSA_SDK_FLAGS -Xanalyzer -analyzer-output=text <file> 2>&1 | grep warning
```
