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

默认 pattern 是 `[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] %v`。彩色 sink 只给 `%^`…`%$` 之间（级别名）上色，方括号保持终端默认色：

```
[2026-05-31 10:30:45.123] [app] [info] Hello, World!
[2026-05-31 10:30:45.123] [app] [warn] Warning: 42
[2026-05-31 10:30:45.123] [app] [error] Error: something went wrong
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

结构化 JSON Lines（每行一个对象，给 Filebeat / ELK / Loki；UTC 时间 + 资源字段）：

```cpp
#include <minispdlog/minispdlog.h>
#include <filesystem>

int main() {
    std::filesystem::create_directories("logs");
    minispdlog::json_formatter fields;
    fields.add("service", "checkout").add("env", "prod").with_host();
    auto lg = minispdlog::json_logger_mt("app", "logs/app.json.log", true, {}, fields);
    lg->info("user {} login", 42);
    lg->flush();
    return 0;
}
```

每行核心字段：`time`（UTC ISO-8601，`YYYY-MM-DDTHH:MM:SS.mmmZ`）、`ts`（Unix epoch 毫秒，方便数值过滤）、`level`、`level_num`（与 `minispdlog::level` 相同：trace=0 … off=6）、`logger`、`msg`、`tid`、`pid`；有源码位置时带 `source`。字符串按 RFC 8259 转义，并额外转义 U+2028 / U+2029，保证一行仍是一个 JSON 值。`add` / `add_int` / `add_bool` / `add_null` / `with_host()` 写入静态资源字段（service、env、host 等），采集侧可当 ECS `service.name` / `host.name` 用。不要占用 `time`/`ts`/`level`/`level_num`/`logger`/`msg`/`tid`/`pid`/`source`。也可 `sink->json().add(...)`，在开始打日志前配置。

任意 sink 也可只换 formatter：`sink->set_formatter(std::make_unique<minispdlog::json_formatter>());`。生产路径用专用 sink，`set_pattern` / `set_formatter` 都锁在 JSON Lines，且会保留已配置的资源字段：`json_logger_mt`（双缓冲批量写、LF 换行）、`rotating_json_logger_mt`、`daily_json_logger_mt`，容器 stdout/stderr 用 `stdout_json_mt` / `stderr_json_mt`。异步：`async_json_file_mt` / `async_json_rotating_mt`。JSON 也可走内核网络：`json_udp_logger_mt` / `json_tcp_logger_mt`（Linux）。

批量写入 + 双缓冲 + 崩溃找回（`buffered_file_sink`；`json_file_sink` 走同一套）：

```cpp
#include <minispdlog/minispdlog.h>

int main() {
    minispdlog::install_crash_flush();  // SIGSEGV/SIGABRT 时尽量把 front buffer 写出
    minispdlog::batch_config cfg;       // 默认 64KiB / 256 条 / 50ms，fflush
    cfg.commit = minispdlog::durability::fflush;  // 或 fsync
    auto lg = minispdlog::buffered_logger_mt("app", "logs/app.log", true, cfg);
    lg->info("batched {}", 1);
    lg->flush();  // 空闲时也要靠 flush / shutdown / 析构才落盘
    return 0;
}
```

打开已有文件时会：回放 `*.minispdlog-wal`（按文件偏移去重，避免半截 fwrite 重复），再按最后 `\n` 截掉半截行。进程崩溃时 front buffer 仍在内存里，靠 `install_crash_flush()` / `dump_buffered_logs()` 尽力写出；`kill -9` 保不住未提交的 front。异步队列里的消息同样不在这套 WAL 里。

回调（不写新 Sink 类：告警、计数、把行交给业务；可与控制台/文件并用）：

```cpp
#include <minispdlog/minispdlog.h>

int main() {
    auto lg = minispdlog::callback_logger_mt("app",
        [](const minispdlog::details::log_msg& msg, const std::string& line) {
            if (msg.lvl >= minispdlog::level::error) {
                // line 是已拥有的字符串，可存；payload / logger_name 只在回调期内有效
                (void)line;
            }
        },
        [] { /* flush 时汇总 */ });
    lg->error("disk full");
    lg->flush();
    minispdlog::drop("app");
}
```

`callback_logger_st` 单线程版；异步用 `async_callback_mt`（`#include <minispdlog/async.h>`）。回调跑在 sink 锁内，不要对同一 sink 再 `log`/`flush`。只要格式化行时用单参数 `void(const std::string&)`。无 Qt 时用回调；有窗口时用 `qt_sink`。

Linux 内核网络（真实 `socket`/`bind`/`connect`/`send`/`recv`，不是模）：

```cpp
#include <minispdlog/minispdlog.h>
#include <minispdlog/network.h>

int main() {
    minispdlog::network_listener collector(minispdlog::network_protocol::udp);
    auto lg = minispdlog::udp_logger_mt("app", collector.local().host, collector.local().port);
    lg->info("shipped over UDP");
    lg->flush();
    const auto line = collector.recv();  // 内核回环收到的数据报
    (void)line;
    return 0;
}
```

TCP 用 `tcp_logger_mt` / `json_tcp_logger_mt`（失败会按 `network_config::reconnect` 重连）。采集进程可用 `network_listener`，或把 host/port 指到 syslog / Vector / Fluent Bit。异步：`async_udp_mt` / `async_tcp_mt` / `async_json_udp_mt` / `async_json_tcp_mt`。非 Linux 构造会抛错。

异步文件（业务线程只入队；进程退出前要 `shutdown()`）：

```cpp
#include <minispdlog/async.h>

int main() {
    minispdlog::init_thread_pool(8192, 2);  // 或 init_lockfree_thread_pool(8192)
    auto lg = minispdlog::async_file_mt("async", "async.log", true);
    // 结构化异步：async_json_file_mt / async_json_rotating_mt
    lg->info("Fast async logging!");
    minispdlog::shutdown();  // 排空队列、停线程池、drop 已注册 logger
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
| 静态库 | `build/src/libminispdlog.a`（MSVC：`build/src/<Config>/minispdlog.lib`） | 给业务链接 |
| 单测 | `build/tests/minispdlog_tests`（MSVC：`build/tests/<Config>/`） | 见下一节 |
| 滚动 Demo | `build/examples/rotating_log_demo` | 见 examples |
| JSON Demo | `build/examples/json_log_demo` | 见 examples |
| 回调 Demo | `build/examples/callback_log_demo` | 见 examples |
| 批量/找回 Demo | `build/examples/buffered_log_demo` | 见 examples |
| 彩色 Demo | `build/examples/color_log_demo` | 见 examples |
| 网络 Demo（Linux） | `build/examples/network_log_demo` | 见 examples |
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

用例带标签，例如 `[queue]`、`[async]`、`[lockfree]`、`[sink]`、`[daily]`、`[json]`、`[color]`、`[network]`。

```bash
# 只跑带某标签的用例（注意给参数加引号；PowerShell 里 [json] 可能被当成通配符，改用 -tc）
./build/tests/minispdlog_tests "[queue]"
./build/tests/minispdlog_tests "[async]"
./build/tests/minispdlog_tests -tc="*json*"
./build/tests/minispdlog_tests -tc="*daily*"

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

`examples/` 下是**可直接构建运行的演示程序**，作为简单 Demo 目录，其中的程序可以观察相关功能的行为。

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

### 2. `examples/json_log` — JSON Lines 文件（默认会编）

演示 `json_file_sink`（`FILE*` 二进制 NDJSON）和 `rotating_json_logger_mt`：每条一行 JSON（UTC `time`、`ts`、`level_num`、资源字段），滚动后仍是 JSON，不会被 `set_pattern` 改回文本。

```bash
cmake --build build --target json_log_demo -j$(nproc)
cd build
./examples/json_log_demo
cat logs/demo.json.log
```

源码入口：`examples/json_log/main.cpp`。

### 3. `examples/callback_log` — 回调旁路（默认会编）

演示控制台 + `callback_sink`：人看终端，业务钩子按级别计数、保留最近错误，并在 `flush` 时打印汇总。

```bash
cmake --build build --target callback_log_demo -j$(nproc)
cd build
./examples/callback_log_demo
```

源码入口：`examples/callback_log/main.cpp`。工厂：`callback_logger_mt/st`、`async_callback_mt`。

### 4. `examples/buffered_log` — 批量写 / 双缓冲 / 崩溃找回（默认会编）

演示 `buffered_logger_mt`：小阈值批量落盘；再打开带半截行的文件做 salvage；用 `.minispdlog-wal` 模拟崩溃后回放。入口调用 `install_crash_flush()`。

```bash
cmake --build build --target buffered_log_demo -j$(nproc)
cd build
./examples/buffered_log_demo
```

源码入口：`examples/buffered_log/main.cpp`。工厂：`buffered_logger_mt/st`、`async_buffered_file_mt`。

### 5. `examples/color_log` — 终端彩色 / 局部着色 / 同时写文件（默认会编）

演示 `stdout_color_mt` / `stderr_color_mt`：用 pattern 里的 `%^` … `%$` 标记一段着色区间（默认只给级别名上色）；同一 logger 再挂 `file_sink` 时，终端带 ANSI，文件是纯文本。

```bash
cmake --build build --target color_log_demo -j$(nproc)

# 务必在 build 目录下运行（日志写在「当前工作目录」/logs）
cd build
./examples/color_log_demo
# Windows MSVC 多配置生成器：
#   .\examples\Debug\color_log_demo.exe
cat logs/demo.color.log
```

源码入口：`examples/color_log/main.cpp`。工厂：`stdout_color_mt`、`stderr_color_mt`、`basic_logger_mt`。

### 6. `examples/network_log` — Linux 内核 UDP/TCP（仅 Linux 默认编）

演示 `udp_sink` / `json_tcp_logger_mt` 经内核协议栈把日志发到本机 `network_listener`（`bind` + `recv`），并让 UDP sink `try_recv` 读回对端 ACK。

```bash
cmake --build build --target network_log_demo -j$(nproc)
cd build
./examples/network_log_demo
```

源码入口：`examples/network_log/main.cpp`。工厂：`udp_logger_mt/st`、`tcp_logger_mt/st`、`json_udp_logger_mt/st`、`json_tcp_logger_mt/st`、`network_logger_mt`；异步 `async_udp_mt` / `async_tcp_mt` / `async_json_udp_mt` / `async_json_tcp_mt`。Windows 不编此 example，单测会断言构造抛错。

### 7. `examples/qt_log_viewer` — Qt 窗口看日志（可选
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
- 未开 `-DMINISPDLOG_WITH_QT=ON` 或不装 Qt 时，**不会**编这个 example，不影响核心库与 `rotating_log_demo` / `json_log_demo` / `callback_log_demo` / `buffered_log_demo` / `color_log_demo` / `network_log_demo`。

### examples 一览

| 目录 | CMake 目标 | 默认构建？ | 用途 |
|------|------------|------------|------|
| `examples/rotating_log/` | `rotating_log_demo` | 是 | 滚动日志落盘 Demo |
| `examples/json_log/` | `json_log_demo` | 是 | JSON Lines 落盘 Demo |
| `examples/callback_log/` | `callback_log_demo` | 是 | 回调旁路（计数 / 告警） |
| `examples/buffered_log/` | `buffered_log_demo` | 是 | 批量写、双缓冲、WAL/salvage |
| `examples/color_log/` | `color_log_demo` | 是 | 终端彩色、局部着色、同时写文件 |
| `examples/network_log/` | `network_log_demo` | 仅 Linux | 内核 UDP/TCP 发送与接收 |
| `examples/qt_log_viewer/` | `qt_log_viewer` | 需 Qt 选项 | GUI 实时看日志 |

---

## 核心特性

### 同步日志

- **多级别**：trace / debug / info / warn / error / critical，支持全局与单 logger 过滤
- **多 Sink**：控制台、文件、按大小滚动、按天切分、JSON Lines、批量双缓冲文件、回调、Linux UDP/TCP 网络；可选 Qt
- **落盘耐久**：`buffered_file_sink` / `json_file_sink` 双缓冲批量 `fwrite`，WAL 回放 + 半截行 salvage；可选 `durability::fsync` 与 `install_crash_flush()`
- **两种 formatter**：`pattern_formatter` 拼文本行；`json_formatter` 拼 JSON Lines。Sink 通过 `set_pattern` / `set_formatter` 安装（`json_*` sink 固定 JSON）
- **占位符**：`%Y %m %d %H %M %S %e %f %l %L %v %t %P %n %s %# %! %@`；`%^` / `%$` 只标记着色区间，不输出字符
- **彩色输出**：`color_*_sink` 按 `log_msg::color_range_*` 给 `[start, end)` 套 ANSI；区间为空则整行着色
- **源码位置**：`lg.info("n={}", 7)` 经 `sourced_fmt` 在调用点捕获 file/line/func；宏 `MINISPDLOG_INFO` 用 `MINISPDLOG_LOC`
- **线程安全**：`base_sink<Mutex>` → `_mt` / `_st`；logger 的 sink 列表 copy-on-write，热路径不加 logger 锁做 I/O

### 异步日志

- **MPMC 阻塞队列**（默认）：多 worker，`block` / `overrun_oldest` / `discard_new`
- **无锁 MPSC**（可选）：`init_lockfree_thread_pool`，单 worker；只支持 `block` / `discard_new`，`overrun_oldest` 会抛异常
- **接口透明**：`async_logger` 继承 `logger`，worker 走 `backend_sink_it_`（不再入队）；`async_file_mt` / `async_buffered_file_mt` / `async_json_file_mt` / `async_json_rotating_mt` / `async_callback_mt` / `async_udp_mt` / `async_tcp_mt` / `async_json_udp_mt` / `async_json_tcp_mt`
- **关闭**：`minispdlog::shutdown()` 先 flush，再停全局线程池，再 `drop_all`

### 工程化

- Registry、工厂函数、`minispdlog::info()` 全局 API
- doctest 单入口、CTest；CI：lint（`.clang-format` / `.clang-tidy`）+ g++/clang × Release/ASan/TSan + Windows MSVC

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

### 回调旁路

不必写新 Sink 类。单参数只拿格式化行；双参数还能读 `level` / `payload`。`line` 可保存；`msg` 里的 view 只在回调期间有效。不要在回调里对同一 sink 再打日志（持有 sink 锁）。

```cpp
auto console = std::make_shared<minispdlog::sinks::color_console_sink_mt>();
auto hook = std::make_shared<minispdlog::sinks::callback_sink_mt>(
    [](const minispdlog::details::log_msg& msg, const std::string& line) {
        if (msg.lvl >= minispdlog::level::error) { /* 告警 / 计数 */ (void)line; }
    });
auto logger = std::make_shared<minispdlog::logger>(
    "app", minispdlog::logger::sink_list{console, hook});
```

工厂：`callback_logger_mt/st`；异步：`async_callback_mt`。完整演示见 `examples/callback_log`。

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

同一场景用同一套分隔：日期 `%Y-%m-%d`，时间 `%H:%M:%S.%e`（微秒用 `%f` 替代 `%e`），源码 `%s:%#` / `%@`，字段 `[..] [..]`。`%^`…`%$` 包住要上色的片段（默认只包 `%L`）。`log()` / `info()` 会填充时间、线程、进程和源码位置。

结构化输出用 `json_formatter`（字段：UTC `time` / `ts` / `level` / `level_num` / `logger` / `msg` / `tid` / `pid`，有源码位置时带 `source`；资源字段用 `add` / `with_host`），不要用 pattern 去「手拼 JSON」。

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
│   ├── json_formatter.h / pattern_formatter.h
│   ├── details/                # 队列、线程池、消息
│   └── sinks/                  # console / file / rotating / daily / json / network / callback / qt
├── src/                        # 静态库实现
├── tests/
│   ├── test_main.cpp           # 单测入口
│   ├── unit/                   # 组件用例
│   └── framework/              # doctest + mock_sink
├── examples/
│   ├── rotating_log/           # 滚动文件 Demo
│   ├── json_log/               # JSON Lines Demo
│   ├── callback_log/           # 回调旁路 Demo
│   ├── buffered_log/           # 批量写 / 找回 Demo
│   ├── color_log/              # 终端彩色 Demo
│   ├── network_log/            # Linux UDP/TCP Demo
│   └── qt_log_viewer/          # Qt Demo（可选）
├── scripts/                    # setup_qt 等
└── third_party/fmt/
```

---

## 致谢

- [spdlog](https://github.com/gabime/spdlog) — 架构参考  
- [fmt](https://github.com/fmtlib/fmt) — 格式化  
- [doctest](https://github.com/doctest/doctest) — 单元测试  
