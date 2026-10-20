#!/usr/bin/env bash
# 从 CMake 构建目录生成 lcov HTML 覆盖率报告。
#   scripts/coverage.sh BUILD_DIR LCOV GENHTML CTEST
set -euo pipefail

BUILD_DIR="${1:?build dir}"
LCOV="${2:?lcov}"
GENHTML="${3:?genhtml}"
CTEST="${4:?ctest}"

cd "${BUILD_DIR}"

"${CTEST}" --output-on-failure --no-tests=error

ignore=(--ignore-errors source --ignore-errors graph --ignore-errors gcov)
# lcov 2.x 会把 mismatch、negative、empty 当作致命错误，而 Ubuntu 22.04 的 1.14 不会。
if "${LCOV}" --version 2>/dev/null | grep -Eq 'version 2'; then
    ignore+=(--ignore-errors negative --ignore-errors mismatch --ignore-errors unused --ignore-errors empty)
fi

"${LCOV}" --capture --directory "${BUILD_DIR}" --output-file coverage.info \
    --rc lcov_branch_coverage=0 "${ignore[@]}"
"${LCOV}" --remove coverage.info \
    '/usr/*' \
    '*/third_party/*' \
    '*/tests/framework/*' \
    --output-file coverage.info.cleaned \
    --rc lcov_branch_coverage=0 "${ignore[@]}"
"${GENHTML}" coverage.info.cleaned --output-directory coverage_report
