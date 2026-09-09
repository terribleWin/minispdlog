#!/usr/bin/env bash
# Use the repo-root .clang-format / .clang-tidy.
#   ./scripts/lint.sh format   # clang-format --dry-run --Werror (allowlisted files)
#   ./scripts/lint.sh tidy     # clang-tidy --config-file=.clang-tidy -p build
#   ./scripts/lint.sh all
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

# Files kept clang-format clean against .clang-format (clang-format 23.1.0).
FORMAT_FILES=(
    include/minispdlog/sinks/daily_file_sink.h
    src/daily_file_sink.cpp
    tests/unit/test_daily_file.cpp
)

collect_sources() {
    git ls-files 'src/*.cpp' 'src/**/*.cpp'
}

cmd="${1:-all}"

if [[ "${cmd}" == "format" || "${cmd}" == "all" ]]; then
    # --style=file loads the nearest .clang-format (repo root).
    clang-format --style=file --dry-run --Werror "${FORMAT_FILES[@]}"
fi

if [[ "${cmd}" == "tidy" || "${cmd}" == "all" ]]; then
    if [[ ! -f build/compile_commands.json ]]; then
        echo "lint.sh: build/compile_commands.json missing (cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)" >&2
        exit 1
    fi
    mapfile -t sources < <(collect_sources)
    if [[ ${#sources[@]} -eq 0 ]]; then
        echo "lint.sh: no src/*.cpp files" >&2
        exit 1
    fi
    tidy_bin="${CLANG_TIDY:-}"
    if [[ -z "${tidy_bin}" ]]; then
        for candidate in clang-tidy-18 clang-tidy-16 clang-tidy-15 clang-tidy; do
            if command -v "${candidate}" >/dev/null 2>&1; then
                tidy_bin="${candidate}"
                break
            fi
        done
    fi
    if [[ -z "${tidy_bin}" ]]; then
        echo "lint.sh: clang-tidy not found" >&2
        exit 1
    fi
    echo "lint.sh: using ${tidy_bin}"
    "${tidy_bin}" --config-file="${ROOT}/.clang-tidy" -p build \
        --extra-arg=-std=c++20 "${sources[@]}"
fi
