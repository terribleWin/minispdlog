# Qt（项目内安装目录）

本目录用于存放 **aqtinstall** 下载的 Qt SDK，供 `qt_sink` / 示例程序使用。

## 安装

Linux / WSL：

```bash
chmod +x scripts/setup_qt.sh
./scripts/setup_qt.sh          # 默认 Qt 6.5.3 gcc_64
```

Windows（PowerShell）：

```powershell
.\scripts\setup_qt.ps1
```

安装后目录类似：

```text
third_party/qt/6.5.3/gcc_64/
third_party/qt/6.5.3/msvc2019_64/
```

SDK 体积较大，**不要提交到 git**（见根目录 `.gitignore`）。

## 启用构建

```bash
cmake -S . -B build -DMINISPDLOG_WITH_QT=ON
cmake --build build
```

CMake 会优先在本目录查找 Qt；若未安装则回退系统 Qt（`apt install qt6-base-dev` 等）。
