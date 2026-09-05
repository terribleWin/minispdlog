#!/usr/bin/env bash
# 将 Qt 安装到项目根目录 third_party/qt（aqtinstall）
# 用法：
#   ./scripts/setup_qt.sh
#   ./scripts/setup_qt.sh 6.5.3 linux desktop gcc_64
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT_ROOT="${ROOT_DIR}/third_party/qt"
VERSION="${1:-6.5.3}"
HOST="${2:-linux}"
TARGET="${3:-desktop}"
ARCH="${4:-gcc_64}"

echo "==> Installing Qt ${VERSION} (${HOST}/${TARGET}/${ARCH}) into ${QT_ROOT}"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required" >&2
  exit 1
fi

python3 -m pip install --user -q aqtinstall

mkdir -p "${QT_ROOT}"
python3 -m aqt install-qt "${HOST}" "${TARGET}" "${VERSION}" "${ARCH}" -O "${QT_ROOT}"

echo "==> Done. Reconfigure CMake with:"
echo "  cmake -S . -B build -DMINISPDLOG_WITH_QT=ON"
echo "  # CMAKE_PREFIX_PATH 会自动指向 ${QT_ROOT}/${VERSION}/${ARCH}"
