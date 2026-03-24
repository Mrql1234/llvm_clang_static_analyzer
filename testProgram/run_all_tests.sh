#!/bin/bash
set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLANG="$REPO_ROOT/build-csa/bin/clang++"
TIDY="$REPO_ROOT/build-csa/bin/clang-tidy"
SDK_PATH="$(xcrun --show-sdk-path)"
CSA_FLAGS="--analyze -isysroot $SDK_PATH -I$SDK_PATH/usr/include/c++/v1 -Xanalyzer -analyzer-output=text"

PASS=0
FAIL=0

run_csa() {
    local file="$1"
    local extra_args="$2"
    local expected_pattern="$3"
    echo -n "CSA: $(basename $file) ... "
    output=$($CLANG $CSA_FLAGS $extra_args "$file" 2>&1 || true)
    if echo "$output" | grep -q "$expected_pattern"; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (expected pattern: $expected_pattern)"
        FAIL=$((FAIL + 1))
    fi
}

run_tidy() {
    local file="$1"
    local check="$2"
    local expected_pattern="$3"
    echo -n "clang-tidy: $(basename $file) [$check] ... "
    output=$($TIDY -checks="-*,$check" "$file" -- -isysroot "$SDK_PATH" -I"$SDK_PATH/usr/include/c++/v1" 2>&1 || true)
    if echo "$output" | grep -q "$expected_pattern"; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL (expected pattern: $expected_pattern)"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== CSA Defect Checker MVP — Regression Tests ==="
echo ""

# US1: Existing CSA checkers
run_csa "$REPO_ROOT/testProgram/test_divide_zero.cpp" "" "core.DivideZero"
run_csa "$REPO_ROOT/testProgram/test_null_deref.cpp" "" "core.NullDereference"
run_csa "$REPO_ROOT/testProgram/test_array_bound.cpp" "-Xanalyzer -analyzer-checker=security.ArrayBound" "security.ArrayBound"

# US2: MathDomainChecker
run_csa "$REPO_ROOT/testProgram/test_math_domain.cpp" "-Xanalyzer -analyzer-checker=alpha.security.MathDomain" "alpha.security.MathDomain"

# US3: Large stack variable
run_tidy "$REPO_ROOT/testProgram/test_large_stack_var.cpp" "bugprone-large-stack-variable" "exceeding threshold"

# US4: Float equal comparison
run_tidy "$REPO_ROOT/testProgram/test_float_equal.cpp" "bugprone-float-equal-comparison" "comparing floating-point"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
