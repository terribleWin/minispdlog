# minispdlog

> 从零构建的轻量级 C++ 异步日志库，核心设计参考 [spdlog](https://github.com/gabime/spdlog)。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()

更细的架构说明见 [`explanation.md`](explanation.md)。

---

## 目录

- [快速 Demo（怎么用这个库）](#快速-demo怎么用这个库)
- [构建](#构建)
- [测试体系怎么用](#测试体系怎么用)
- [examples 目录怎么用](#examples-目录怎么用)
- [核心特性](#核心特性)
- [更多用法](#更多用法)
- [项目结构](#项目结构)
- [致谢](#致谢)

---

## 快速 Demo（怎么用这个库）

最小可运行程序：彩色控制台 + 全局 API。

```cpp
#include <minispdlog/minispdlog.h>

int main() {
    auto logger = minispdlog::stdout_color_mt("app");
    minispdlog::set_default_logger(logger);

    minispdlog::info("Hello, {}!", "World");
    minispdlog::warn("Warning: {}", 42);
    minispdlog::error("Error: {}", "something went wrong");
    return 0;
}
```

典型输出：

```
[2026-05-31 10:30:45] [info] Hello, World!
[2026-05-31 10:30:45] [warn] Warning: 42
[2026-05-31 10:30:45] [error] Error: something went wrong
```

滚动文件（磁盘不无限涨）：

```cpp
#include <minispdlog/minispdlog.h>
#include <filesystem>

int main() {
    std::filesystem::create_directories("logs");
    // 单文件上限 1MB，最多再保留 3 个归档：logs/app.log, app.1.log, ...
    auto lg = minispdlog::rotating_logger_mt("app", "logs/app.log", 1024 * 1024, 3);
    lg->info("rotated file logging");
    lg->flush();
    return 0;
}
```

按天切文件（默认本地 00:00 翻日，文件名带 `%Y-%m-%d`）：

```cpp
#include <minispdlog/minispdlog.h>
#include <filesystem>

int main() {
    std::filesystem::create_directories("logs");
    // logs/app.2026-09-05.log ；最多保留最近 7 个日期文件
    auto lg = minispdlog::daily_logger_mt("app", "logs/app.log", 0, 0, false, 7);
    lg->info("daily file logging");
    lg->flush();
    return 0;
}
```

异步文件（业务线程只入队）：

```cpp
#include <minispdlog/async.h>

int main() {
    minispdlog::init_thread_pool(8192, 2);                 // 或 init_lockfree_thread_pool(8192)
    auto lg = minispdlog::async_file_mt("async", "async.log", true);
    lg->info("Fast async logging!");
    return 0;
}
```

把上面片段链到本仓库的 `minispdlog` 静态库即可（见下方构建）。不想手写工程时，直接跑 [`examples/`](#examples-目录怎么用)。

---

## 构建

**依赖：** CMake ≥ 3.11 · C++20 编译器 · 已包含的 `third_party/fmt`

```bash
git clone --recursive <本仓库 URL>
cd minispdlog

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)          # Windows: cmake --build build --config Release
```

常用产物：

| 目标 | 路径（大致） | 说明 |
|------|----------------|------|
| 静态库 | `build/src/libminispdlog.a`（或对应 `.lib`） | 给业务链接 |
| 单测 | `build/tests/minispdlog_tests` | 见下一节 |
| 滚动 Demo | `build/examples/rotating_log_demo` | 见 examples |
| Qt 窗口（可选） | `build/examples/qt_log_viewer/...` | 需 `-DMINISPDLOG_WITH_QT=ON` |

---

## 测试体系怎么用

本仓库用 **doctest** 做单元测试：**一个可执行文件** `minispdlog_tests` 包含全部用例（`tests/unit/*.cpp`），比「每个文件一个 exe」编译更快。

### 跑全部测试

```bash
cmake --build build --target minispdlog_tests -j$(nproc)

# 直接跑
./build/tests/minispdlog_tests

# 或用 CTest
ctest --test-dir build --output-on-failure
# 等价：ctest --test-dir build -R minispdlog_tests
```

### 按标签 / 名字过滤（常用）

用例带标签，例如 `[queue]`、`[async]`、`[lockfree]`、`[sink]`。

```bash
# 只跑带某标签的用例（注意给参数加引号）
./build/tests/minispdlog_tests "[queue]"
./build/tests/minispdlog_tests "[async]"
./build/tests/minispdlog_tests "[lockfree]"

# 按用例名子串过滤
./build/tests/minispdlog_tests -tc="*spsc*"
./build/tests/minispdlog_tests -tc="*rotating*"

# 列出帮助
./build/tests/minispdlog_tests --help
```

### 目录约定

| 路径 | 作用 |
|------|------|
| `tests/test_main.cpp` | doctest 入口 |
| `tests/unit/*.cpp` | 按组件拆分的用例 |
| `tests/framework/doctest.h` | 测试框架 |
| `tests/framework/mock_sink.h` | 内存捕获 Sink，断言「写了什么」 |
| `tests/framework/test_fixture.h` | 临时目录等辅助 |

旧的 `tests/test_*.cpp`（根下独立 main）多数已废弃，**以 `unit/` + `minispdlog_tests` 为准**。

### 可选：Benchmark / 覆盖率 / 消毒器

```bash
# Google Benchmark（需系统已安装 benchmark 开发包）
# 配置成功后会生成：
#   build/tests/minispdlog_bench_async
#   build/tests/minispdlog_bench_queue

# 覆盖率（GCC/Clang）
cmake -S . -B build -DMINISPDLOG_ENABLE_COVERAGE=ON
cmake --build build --target coverage

# ASan / TSan 构建类型（见根 CMakeLists）
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelASan
cmake --build build -j$(nproc)
./build/tests/minispdlog_tests

# clang-format / clang-tidy（读取仓库根目录配置文件）
./scripts/lint.sh format          # clang-format --style=file --dry-run --Werror
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
./scripts/lint.sh tidy            # clang-tidy --config-file=.clang-tidy
# 或：cmake --build build --target format / format-check
```

---

## examples 目录怎么用

`examples/` 下是**可直接构建运行的演示程序**，用来看库行为，而不是业务模板库。

### 1. `examples/rotating_log` — 滚动文件（默认会编）

演示 `rotating_file_sink`：把单文件上限设得很小，跑完即可在 `logs/` 看到多卷文件。

```bash
cmake --build build --target rotating_log_demo -j$(nproc)

# 务必在 build 目录下运行（日志写在「当前工作目录」/logs）
cd build
./examples/rotating_log_demo
ls -la logs/demo_rotating*
```

预期类似：

```
logs/demo_rotating.log      # 当前正在写
logs/demo_rotating.1.log    # 归档
logs/demo_rotating.2.log
logs/demo_rotating.3.log
```

源码入口：`examples/rotating_log/main.cpp`。

### 2. `examples/qt_log_viewer` — Qt 窗口看日志（可选）

把日志刷到 `QTextEdit` / `QPlainTextEdit`，依赖 Qt Widgets。

```bash
# 建议先装 SDK（仓库不提交完整 Qt）
./scripts/setup_qt.sh          # Linux / WSL
# 或 scripts/setup_qt.ps1      # Windows

cmake -S . -B build -DMINISPDLOG_WITH_QT=ON
cmake --build build --target qt_log_viewer -j$(nproc)

# 可执行文件路径以本机生成结果为准，一般在 build/examples/qt_log_viewer/
```

说明：

- 无显示器时，单测里的 Qt 用例可用 offscreen；**看窗口**请用本机图形环境（纯 WSL 常缺 GUI）。
- 未开 `-DMINISPDLOG_WITH_QT=ON` 或不装 Qt 时，**不会**编这个 example，不影响核心库与 `rotating_log_demo`。

### examples 一览

| 目录 | CMake 目标 | 默认构建？ | 用途 |
|------|------------|------------|------|
| `examples/rotating_log/` | `rotating_log_demo` | 是 | 滚动日志落盘 Demo |
| `examples/qt_log_viewer/` | `qt_log_viewer` | 需 Qt 选项 | GUI 实时看日志 |

---

## 核心特性

### 同步日志

- **多级别**：trace / debug / info / warn / error / critical，支持全局与单 logger 过滤
- **多 Sink**：控制台、文件、按大小滚动、按天切分、回调；可选 Qt
- **可扩展格式**：`pattern_formatter`（`%Y %m %d %H %M %S %e %f %l %L %v %t %P %n %s %# %! %@ %^ %$` 等）
- **彩色输出**：ANSI SGR
- **线程安全**：`base_sink<Mutex>` → `_mt` / `_st`

### 异步日志

- **MPMC 阻塞队列**（默认）：多 worker，`block` / `overrun_oldest` / `discard_new`
- **无锁 MPSC**（可选）：`init_lockfree_thread_pool`，单 worker；只支持 `block` / `discard_new`，`overrun_oldest` 会抛异常
- **接口透明**：`async_logger` 继承 `logger`；也可直接用 `lockfree_queue.h`

### 工程化

- Registry、工厂函数、`minispdlog::info()` 全局 API
- doctest 单入口测试、可选 Benchmark / ASan / TSan CI

---

## 更多用法

### 多 Sink

```cpp
auto console = std::make_shared<minispdlog::sinks::console_sink_mt>();
auto file = std::make_shared<minispdlog::sinks::file_sink_mt>("app.log");

auto logger = std::make_shared<minispdlog::logger>("multi");
logger->add_sink(console);
logger->add_sink(file);
logger->info("This goes to both console and file");
```

### 自定义 pattern

```cpp
auto sink = std::make_shared<minispdlog::sinks::console_sink_mt>();
// set_pattern：用 pattern 字符串（编译成 pattern_formatter）
sink->set_pattern("[%H:%M:%S.%e] [%L] [%s:%#] %v");

auto logger = std::make_shared<minispdlog::logger>("app", sink);
logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] [%s:%#] %v");
logger->log(minispdlog::level::info, "hello {}", name);  // 一条 log 即可输出

// set_formatter：安装自定义 formatter（每 sink 一份 clone）
// logger->set_formatter(std::make_unique<MyFormatter>());
```

同一场景用同一套分隔：日期 `%Y-%m-%d`，时间 `%H:%M:%S.%e`（微秒用 `%f` 替代 `%e`），源码 `%s:%#` / `%@`，字段 `[..] [..]`。`log()` / `info()` 会填充时间、线程、进程和源码位置。

分层与异步热路径细节见 [`explanation.md`](explanation.md)。

---

## 项目结构

```
minispdlog/
├── CMakeLists.txt
├── explanation.md              # 架构与用法长文
├── include/minispdlog/         # 对外头文件
│   ├── minispdlog.h            # 用户 API / 工厂
│   ├── async.h / async_config.h
│   ├── lockfree_queue.h
│   ├── logger.h / registry.h / level.h
│   ├── details/                # 队列、线程池、消息
│   └── sinks/                  # console / file / rotating / daily / callback / qt
├── src/                        # 静态库实现
├── tests/
│   ├── test_main.cpp           # 单测入口
│   ├── unit/                   # 组件用例
│   └── framework/              # doctest + mock_sink
├── examples/
│   ├── rotating_log/           # 滚动文件 Demo
│   └── qt_log_viewer/          # Qt Demo（可选）
├── scripts/                    # setup_qt 等
└── third_party/fmt/
```

---

## 致谢

- [spdlog](https://github.com/gabime/spdlog) — 架构参考  
- [fmt](https://github.com/fmtlib/fmt) — 格式化  
- [doctest](https://github.com/doctest/doctest) — 单元测试  
