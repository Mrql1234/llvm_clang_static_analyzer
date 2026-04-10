#!/bin/bash
# CTU (Cross Translation Unit) 分析演示脚本
# 用法: cd llvm-project-personal && bash testProgram/ctu_analysis_demo/run_ctu_demo.sh

set -e

CLANG=build-csa/bin/clang
EXTDEF=build-csa/bin/clang-extdef-mapping
SDK_PATH=$(xcrun --show-sdk-path)
INCDIR="testProgram/ctu_analysis_demo/include"
SRCDIR="testProgram/ctu_analysis_demo/src"
CTU_DIR=$(pwd)/testProgram/ctu_analysis_demo/ctu-dir

echo "============================================="
echo " 第一部分: 普通单 TU 分析（对照组）"
echo "============================================="
echo ""
$CLANG --analyze \
  -Xanalyzer -analyzer-checker=core \
  -I "$INCDIR" -isysroot "$SDK_PATH" \
  "$SRCDIR/main.cpp" 2>&1
echo ""

echo "============================================="
echo " 第二部分: CTU 分析（跨翻译单元）"
echo "============================================="
echo ""

echo "--- 步骤 1: 为每个 TU 生成 AST dump ---"
rm -rf "$CTU_DIR"
mkdir -p "$CTU_DIR"
for src in "$SRCDIR"/*.cpp; do
    echo "  emit-ast: $src"
    $CLANG -emit-ast -I "$INCDIR" -isysroot "$SDK_PATH" \
      -o "$CTU_DIR/$(basename "$src").ast" "$src"
done
echo ""

echo "--- 步骤 2: 生成外部定义映射表 ---"
$EXTDEF "$SRCDIR/math_utils.cpp" "$SRCDIR/data_provider.cpp" "$SRCDIR/main.cpp" \
  -- -I "$INCDIR" -isysroot "$SDK_PATH" 2>/dev/null \
  | awk '/^[0-9]+:/ { n = split($2, p, "/"); print $1, p[n] ".ast" }' \
  > "$CTU_DIR/externalDefMap.txt"
echo "  映射表内容:"
sed -n '1,20p' "$CTU_DIR/externalDefMap.txt"
echo ""

echo "--- 步骤 3: 带 CTU 运行 CSA ---"
$CLANG --analyze \
  -Xanalyzer -analyzer-checker=core \
  -Xanalyzer -analyzer-config -Xanalyzer experimental-enable-naive-ctu-analysis=true \
  -Xanalyzer -analyzer-config -Xanalyzer ctu-dir="$CTU_DIR" \
  -Xanalyzer -analyzer-config -Xanalyzer display-ctu-progress=true \
  -I "$INCDIR" -isysroot "$SDK_PATH" \
  "$SRCDIR/main.cpp" 2>&1

echo ""
echo "============================================="
echo " 完成"
echo "============================================="
