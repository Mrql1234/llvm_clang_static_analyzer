# MVP 后增强方案总结

**Workspace**: `csa-defect-checkers`
**Created**: 2026-03-17
**Status**: Draft
**前提**: MVP（7 种缺陷检测）已完成并提交

---

## 1. 背景

MVP 阶段已完成以下 7 种缺陷检测能力：

| # | 缺陷 | 实现方式 | 策略 |
|---|------|---------|------|
| 1 | 除以零 | `core.DivideZero` (已有) | 必定缺陷 |
| 2 | 空指针解引用 | `core.NullDereference` (已有) | 必定缺陷 |
| 3 | 数组越界 | `security.ArrayBound` (已有) | 必定缺陷 |
| 4 | sqrt 负数 / 反三角函数越域 | `alpha.security.MathDomain` (新建 CSA) | 编译期常量 |
| 5 | 大局部变量栈溢出 | `bugprone-large-stack-variable` (新建 clang-tidy) | AST 模式 |
| 6 | 浮点数等号比较 | `bugprone-float-equal-comparison` (新建 clang-tidy) | AST 模式 |

MVP 验证中发现三类场景**超出当前检测能力**，均涉及"外部输入 / 未约束符号值"：

1. `100 / get_value()` — 除数可能为零但未被证明
2. `arr[get_value()]` — 下标可能越界但未被证明
3. `sqrt(x)` 其中 x 为变量 — CSA 不支持浮点符号化

---

## 2. 三项增强方案概览

| 增强项 | 缺陷类型 | 实现方式 | 新 Checker/Check 名称 | 复杂度 |
|--------|---------|---------|----------------------|--------|
| E1 | 可能除以零 | CSA Checker | `alpha.core.PossibleDivideZero` | 低 |
| E2 | 可能数组越界 | CSA Checker | `alpha.security.PossibleArrayBound` | 中 |
| E3 | 数学函数域未保护 | clang-tidy Check | `bugprone-math-domain-guard` | 中 |

### 策略对比

```
                    MVP 策略                    增强策略
检测哲学            只报必定缺陷                  报可能缺陷 / 缺少保护
误报率              极低                          中等（可控）
适用场景            通用软件                      安全关键系统
启用方式            默认 / core 包                alpha.* / 显式启用
```

---

## 3. E1: 可能除以零 (`alpha.core.PossibleDivideZero`)

**详细需求**: [除以零.md](除以零.md)

### 核心变更

在 `DivZeroChecker` 的基础上，将判定条件从"除数必定为零"放宽到"除数可能为零"：

```
DivZeroChecker:             !stateNotZero && stateZero    → 报
PossibleDivideZero:         stateNotZero && stateZero     → 报（关键区别）
```

### 工作量估计

| 项目 | 预估 |
|------|------|
| 新建 Checker 代码 | 约 80 行（DivZeroChecker 变体） |
| 注册 + CMakeLists | 约 10 行修改 |
| 测试程序 | 约 50 行 |
| 编译验证 | 增量编译 1-3 分钟 |
| **总计** | 约 2-3 小时 |

### 风险

- 低风险。核心逻辑是现有 DivZeroChecker 的简单变体
- 误报可控，通过 `alpha.` 前缀让用户自主选择

---

## 4. E2: 可能数组越界 (`alpha.security.PossibleArrayBound`)

**详细需求**: [数组越界.md](数组越界.md)

### 核心变更

简化版 ArrayBoundChecker，仅聚焦 `ArraySubscriptExpr`，将判定条件从"下标必定越界"放宽到"下标未被证明在合法范围内"。

### 工作量估计

| 项目 | 预估 |
|------|------|
| 新建 Checker 代码 | 约 150 行 |
| 注册 + CMakeLists | 约 10 行修改 |
| 测试程序 | 约 80 行 |
| 编译验证 | 增量编译 1-3 分钟 |
| **总计** | 约 4-6 小时 |

### 风险

- 中等风险。ArrayBound 的逻辑比 DivZero 复杂，需要正确处理偏移量和 Extent 获取
- 误报可能偏高（数组访问比除法更普遍），需要仔细设计过滤条件

---

## 5. E3: 数学函数域保护 (`bugprone-math-domain-guard`)

**详细需求**: [数学域.md](数学域.md)

### 核心变更

新建 clang-tidy Check，检查 `sqrt`/`asin`/`acos` 调用前是否有域保护（if 守卫、assert、fabs 等）。与现有 `alpha.security.MathDomain` 形成互补。

### 工作量估计

| 项目 | 预估 |
|------|------|
| 脚手架生成 (add_new_check.py) | 5 分钟 |
| Check 核心逻辑（Phase 1 基础版） | 约 200 行 |
| 保护模式匹配逻辑 | 约 100 行 |
| 注册（自动生成） | 0 |
| 测试程序 | 约 80 行 |
| 编译验证 | 增量编译 1-3 分钟 |
| **总计** | 约 6-8 小时（Phase 1） |

### 风险

- 中等风险。AST 层保护模式匹配可能遗漏非标准保护模式（如 early return、嵌套条件）
- 建议分阶段实现，Phase 1 先覆盖直接 if 包裹的场景

---

## 6. 新增文件汇总

```text
llvm-project-personal/
├── clang/lib/StaticAnalyzer/Checkers/
│   ├── PossibleDivZeroChecker.cpp                    # [新建] E1
│   └── PossibleArrayBoundChecker.cpp                 # [新建] E2
│
├── clang/include/clang/StaticAnalyzer/Checkers/
│   └── Checkers.td                                   # [修改] 注册 E1, E2
│
├── clang-tools-extra/clang-tidy/bugprone/
│   ├── MathDomainGuardCheck.h                        # [新建] E3
│   ├── MathDomainGuardCheck.cpp                      # [新建] E3
│   ├── CMakeLists.txt                                # [修改]
│   └── BugproneTidyModule.cpp                        # [修改]
│
└── testProgram/
    ├── test_possible_div_zero.cpp                    # [新建] E1 测试
    ├── test_possible_array_bound.cpp                 # [新建] E2 测试
    └── test_math_domain_guard.cpp                    # [新建] E3 测试
```

| 类型 | 数量 |
|------|------|
| 新建源文件 (.h + .cpp) | 4 个 |
| 修改已有文件 | 4 个 |
| 新建测试程序 | 3 个 |
| **总计** | 11 个文件 |

---

## 7. 实施顺序建议

```
推荐顺序: E1 → E2 → E3

E1 (PossibleDivideZero)     — 最简单，DivZeroChecker 的直接变体
     ↓
E2 (PossibleArrayBound)     — 中等，借鉴 E1 的 "可能缺陷" 模式
     ↓
E3 (MathDomainGuard)        — 独立于 E1/E2，clang-tidy 路线
```

**理由**:
- E1 复杂度最低，可以最快出成果，同时验证"可能缺陷"策略的可行性
- E2 和 E1 共享相同的设计哲学，E1 的经验可直接复用
- E3 是独立的 clang-tidy Check，不依赖 E1/E2，可以并行开发

---

## 8. 总工时估计

| 增强项 | 工时估计 | 累计 |
|--------|---------|------|
| E1 PossibleDivideZero | 2-3 小时 | 2-3 小时 |
| E2 PossibleArrayBound | 4-6 小时 | 6-9 小时 |
| E3 MathDomainGuard (Phase 1) | 6-8 小时 | 12-17 小时 |
| 集成测试 + 文档更新 | 2-3 小时 | 14-20 小时 |
| **总计** | **约 2-3 个工作日** | |

---

## 9. 使用方式预览

三项增强完成后，用户可以这样使用：

### CSA 增强检测

```bash
# 同时启用必定 + 可能除零检测
$CLANG --analyze $CSA_SDK_FLAGS \
  -Xanalyzer -analyzer-checker=alpha.core.PossibleDivideZero \
  target.cpp

# 同时启用必定 + 可能数组越界检测
$CLANG --analyze $CSA_SDK_FLAGS \
  -Xanalyzer -analyzer-checker=alpha.security.PossibleArrayBound \
  target.cpp
```

### clang-tidy 增强检测

```bash
# 数学函数域保护检查
$CLANG_TIDY \
  -checks='-*,bugprone-math-domain-guard' \
  target.cpp -- $CSA_SDK_FLAGS
```

### 全量检测（MVP + 增强）

```bash
# CSA: 原有 + 增强
$CLANG --analyze $CSA_SDK_FLAGS \
  -Xanalyzer -analyzer-checker=alpha.security.MathDomain \
  -Xanalyzer -analyzer-checker=alpha.core.PossibleDivideZero \
  -Xanalyzer -analyzer-checker=alpha.security.PossibleArrayBound \
  target.cpp

# clang-tidy: 原有 + 增强
$CLANG_TIDY \
  -checks='-*,bugprone-large-stack-variable,bugprone-float-equal-comparison,bugprone-math-domain-guard' \
  target.cpp -- $CSA_SDK_FLAGS
```
