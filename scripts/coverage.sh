#!/usr/bin/env bash
# Generate lcov HTML from a CMake build tree.
#   scripts/coverage.sh BUILD_DIR LCOV GENHTML CTEST
set -euo pipefail

BUILD_DIR="${1:?build dir}"
LCOV="${2:?lcov}"
GENHTML="${3:?genhtml}"
CTEST="${4:?ctest}"

cd "${BUILD_DIR}"

"${CTEST}" --output-on-failure --no-tests=error

ignore=(--ignore-errors source --ignore-errors graph --ignore-errors gcov)
# lcov 2.x treats mismatch/negative/empty as fatal; 1.14 on Ubuntu 22.04 does not.
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
