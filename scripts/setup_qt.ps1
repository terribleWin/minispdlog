# 将 Qt 安装到项目根目录 third_party/qt（aqtinstall）
# 用法：
#   .\scripts\setup_qt.ps1
#   .\scripts\setup_qt.ps1 -Version 6.5.3 -Arch win64_msvc2019_64
param(
    [string]$Version = "6.5.3",
    [string]$HostOs = "windows",
    [string]$Target = "desktop",
    [string]$Arch = "win64_msvc2019_64"
)

$ErrorActionPreference = "Stop"
$RootDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$QtRoot = Join-Path $RootDir "third_party\qt"

Write-Host "==> Installing Qt $Version ($HostOs/$Target/$Arch) into $QtRoot"

python -m pip install --user -q aqtinstall
New-Item -ItemType Directory -Force -Path $QtRoot | Out-Null
python -m aqt install-qt $HostOs $Target $Version $Arch -O $QtRoot

Write-Host "==> Done. Reconfigure CMake with:"
Write-Host "  cmake -S . -B build -DMINISPDLOG_WITH_QT=ON"
