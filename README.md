# minispdlog

```text
 __  __ _       _ ____            _ _
|  \/  (_)_ __ (_) ___| _ __   __| | | ___   __ _
| |\/| | | '_ \| \___ \| '_ \ / _` | |/ _ \ / _` |
| |  | | | | | | |___) | |_) | (_| | | (_) | (_| |
|_|  |_|_|_| |_|_|____/| .__/ \__,_|_|\___/ \__, |
                       |_|                   |___/
   ┌────────────────────────────────────────────────┐
   │  SYNC · ASYNC · CORO · DURABLE · STRUCTURED   │
   └──────── explicit flow · clear boundaries ─────┘
```

> 面向可解释数据路径的 C++20 日志库：同步直写、异步解耦、批量可恢复、结构化可观测。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
![Platform](https://img.shields.io/badge/core-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)

`minispdlog` 同时保留同步日志与异步队列：简单场景直接写 Sink，高吞吐场景切换到阻塞 MPMC 或无锁 MPSC 后端；需要更强落盘语义时，可使用双缓冲、WAL 与断尾修复；需要采集时，可直接输出 JSON Lines 或投递到回调、Qt 和 Linux UDP/TCP。

项目把逐条日志视为数据面，把 `flush` / `shutdown` 视为控制面：普通调用保持同步直写或队列投递；C++20 协程可以等待低频控制操作，而不会在当前事件循环线程执行阻塞的 Sink I/O。当前版本为 `0.1.0`，公开 API 仍可能随工程化完善而演进。

### 可追踪的数据面与控制面

```text
sync     call ─────────► format ─────────────► sink
async    call ─────────► queue ──► worker ──► sink
durable  sink ─────────► batch ──► WAL ─────► file
control  co_await ─────► FIFO control thread ─► flush / shutdown
```

| 设计支柱 | 本项目的选择 |
|----------|--------------|
| 同步与异步并存 | 同步路径不依赖线程池；异步路径可选 blocking MPMC 或 lock-free MPSC |
| 可说明的过载策略 | `block`、`overrun_oldest`、`discard_new`，并提供覆盖/丢弃计数 |
| 耐久批处理 | 双缓冲批量写、WAL 回放、半截行修复，以及 `fflush` / `fsync` 档位 |
| 结构化与旁路输出 | RFC 8259 JSON Lines、资源字段、回调、Qt，以及 Linux UDP/TCP |
| 协程控制面 | `coro::flush` / `flush_all` / `shutdown` 在单线程 FIFO 控制执行器执行，异常回到等待点 |
| 采集友好格式 | `%q` 保持单条记录单行，`%u` 输出 UTC RFC 3339，`%E` 输出 Unix 毫秒时间戳 |

## 五分钟上手

### 最小同步示例

```cpp
#include <minispdlog/minispdlog.h>

int main() {
    auto logger = minispdlog::stdout_color_mt("app");
    minispdlog::set_default_logger(logger);
    minispdlog::info("Hello, {}!", "World");
}
```

### 最小异步示例

```cpp
#include <minispdlog/async.h>

int main() {
    minispdlog::init_thread_pool(8192, 1);
    auto logger = minispdlog::async_file_mt("app", "app.log", true);
    logger->info("request_id={}", 42);
    minispdlog::shutdown(); ///< 排空队列、停止工作线程并释放已注册 logger。
}
```

### 在协程中等待刷新

```cpp
#include <minispdlog/coro.h>

task<void> finish_request(std::shared_ptr<minispdlog::logger> logger) {
    logger->info("request finished");
    co_await minispdlog::coro::flush(logger);
}
```

`task<void>` 由应用使用的协程运行时提供；minispdlog 只公开标准 C++20 Awaitable，不绑定某一种事件循环。默认在日志控制线程恢复，也可传入调度函数把 continuation 投递回业务执行器。完整语义见[协程控制面](docs/coroutines.md)。

### 构建并运行测试

```bash
# 在仓库根目录执行；third_party/fmt 已随仓库提供
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

> 使用异步 logger 时，请在 `main()` 返回前调用 `minispdlog::shutdown()`。同步、异步和耐久写入的边界分别见[同步与异步模型](docs/sync-async-model.md)和[耐久性与恢复](docs/durability-and-recovery.md)。

## 文档导航

| 想了解什么 | 从这里开始 |
|------------|------------|
| 安装、链接与第一个 logger | [快速开始](docs/getting-started.md) |
| 全部文档与成熟度 | [文档中心](docs/README.md) |
| 同步、异步、队列和关闭语义 | [同步与异步模型](docs/sync-async-model.md) |
| 协程刷新、关闭与恢复调度 | [协程控制面](docs/coroutines.md) |
| 双缓冲、WAL、断尾修复 | [耐久性与恢复](docs/durability-and-recovery.md) |
| JSON 字段与采集约定 | [结构化日志](docs/structured-logging.md) |
| Sink 能力与平台矩阵 | [Sink 指南](docs/sinks.md) |
| 工厂、配置与 pattern 查询 | [API 速查](docs/api.md) |
| 测试与性能验证 | [测试指南](docs/testing.md) · [性能方法](docs/performance.md) |
| 完整架构长文 | [项目说明](explanation.md) · [设计决策](docs/design-decisions.md) · [PlantUML 图](docs/architecture.puml) |

---

## 目录

- [五分钟上手](#五分钟上手)
- [文档导航](#文档导航)
- [功能指南](#功能指南)
- [构建](#构建)
- [测试体系怎么用](#测试体系怎么用)
- [examples 目录怎么用](#examples-目录怎么用)
- [核心特性](#核心特性)
- [更多用法](#更多用法)
- [项目结构](#项目结构)
- [许可证](#许可证)
- [致谢](#致谢)

---

## 功能指南

默认 pattern 是 `[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] %v`。彩色 sink 只给 `%^`…`%$` 之间（级别名）上色，方括号保持终端默认色：

```
[2026-05-31 10:30:45.123] [app] [info] Hello, World!
[2026-05-31 10:30:45.123] [app] [warn] Warning: 42
[2026-05-31 10:30:45.123] [app] [error] Error: something went wrong
```

面向按行采集的文本日志，可使用项目提供的三个扩展标志：

```cpp
logger->set_pattern("[%u] [epoch_ms=%E] [%n] [%L] %q");
```

- `%u`：固定输出毫秒精度的 UTC RFC 3339 时间；
- `%E`：输出带符号 Unix epoch 毫秒，便于排序和数值过滤；
- `%q`：转义反斜杠、C0 控制字符和 Unicode 行/段落分隔符，保证一条记录保持在一行；需要原始多行正文时继续使用 `%v`。

滚动文件（磁盘不无限涨）：

```cpp
#include <minispdlog/minispdlog.h>
#include <filesystem>

int main() {
    std::filesystem::create_directories("logs");
    /// 单文件上限 1MB，最多再保留 3 个归档：logs/app.log, app.1.log, ...
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
    /// logs/app.2026-09-05.log；最多保留最近 7 个日期文件。
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

每行核心字段：`time`（UTC ISO-8601，`YYYY-MM-DDTHH:MM:SS.mmmZ`）、`ts`（Unix epoch 毫秒，方便数值过滤）、`level`、`level_num`（与 `minispdlog::level` 相同：trace=0 … off=6）、`logger`、`msg`、`tid`、`pid`；有源码位置时带 `source`。字符串按 RFC 8259 规则转义，并额外转义 U+2028 / U+2029，避免换行破坏单记录边界；非法 UTF-8 字节序列会替换为 U+FFFD，保证输出仍可被严格 JSON 解析器读取。`add` / `add_int` / `add_bool` / `add_null` / `with_host()` 写入静态资源字段（service、env、host 等），采集侧可映射到自己的 schema。不要占用 `time`/`ts`/`level`/`level_num`/`logger`/`msg`/`tid`/`pid`/`source`。也可 `sink->json().add(...)`，在开始打日志前配置。

任意 sink 也可只换 formatter：`sink->set_formatter(std::make_unique<minispdlog::json_formatter>());`。生产路径用专用 sink，`set_pattern` / `set_formatter` 都锁在 JSON Lines，且会保留已配置的资源字段：`json_logger_mt`（双缓冲批量写、LF 换行）、`rotating_json_logger_mt`、`daily_json_logger_mt`，容器 stdout/stderr 用 `stdout_json_mt` / `stderr_json_mt`。异步：`async_json_file_mt` / `async_json_rotating_mt`。JSON 也可走内核网络：`json_udp_logger_mt` / `json_tcp_logger_mt`（Linux）。

批量写入 + 双缓冲 + 崩溃找回（`buffered_file_sink`；`json_file_sink` 走同一套）：

```cpp
#include <minispdlog/minispdlog.h>

int main() {
    minispdlog::install_crash_flush(); ///< 收到 SIGSEGV/SIGABRT 时尽量写出前台缓冲。
    minispdlog::batch_config cfg;      ///< 默认 64KiB、256 条、50ms，并执行 fflush。
    cfg.commit = minispdlog::durability::fflush; ///< 也可选择 fsync。
    auto lg = minispdlog::buffered_logger_mt("app", "logs/app.log", true, cfg);
    lg->info("batched {}", 1);
    lg->flush(); ///< 空闲时也要通过 flush、shutdown 或析构触发落盘。
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
                /// line 是已拥有的字符串，可保存；payload / logger_name 只在回调期间有效。
                (void)line;
            }
        },
        [] {
            /// 在 flush 时汇总批次状态。
        });
    lg->error("disk full");
    lg->flush();
    minispdlog::drop("app");
}
```

`callback_logger_st` 单线程版；异步用 `async_callback_mt`（`#include <minispdlog/async.h>`）。回调跑在 sink 锁内，不要对同一 sink 再 `log`/`flush`。只要格式化行时用单参数 `void(const std::string&)`。无 Qt 时用回调；有窗口时用 `qt_sink`。

Linux 内核网络（真实 `socket`/`bind`/`connect`/`send`/`recv`，不是模拟服务）：

```cpp
#include <minispdlog/minispdlog.h>
#include <minispdlog/network.h>

int main() {
    minispdlog::network_listener collector(minispdlog::network_protocol::udp);
    auto lg = minispdlog::udp_logger_mt("app", collector.local().host, collector.local().port);
    lg->info("shipped over UDP");
    lg->flush();
    const auto line = collector.recv(); ///< 内核回环收到的数据报。
    (void)line;
    return 0;
}
```

TCP 用 `tcp_logger_mt` / `json_tcp_logger_mt`（失败会按 `network_config::reconnect` 重连）。采集进程可用 `network_listener`，或把 host/port 指到 syslog / Vector / Fluent Bit。异步：`async_udp_mt` / `async_tcp_mt` / `async_json_udp_mt` / `async_json_tcp_mt`。非 Linux 构造会抛错。

异步文件（调用线程先展开消息参数再入队，Sink formatter 与 I/O 在工作线程执行；进程退出前要 `shutdown()`）：

```cpp
#include <minispdlog/async.h>

int main() {
    minispdlog::init_thread_pool(8192, 2); ///< 也可调用 init_lockfree_thread_pool(8192)。
    auto lg = minispdlog::async_file_mt("async", "async.log", true);
    /// 结构化异步：async_json_file_mt / async_json_rotating_mt。
    lg->info("Fast async logging!");
    minispdlog::shutdown(); ///< 排空队列、停止线程池并移除已注册 logger。
    return 0;
}
```

把上面片段链到本仓库的 `minispdlog` 静态库即可（见下方构建）。不想手写工程时，直接跑 [`examples/`](#examples-目录怎么用)。

---

## 构建

**依赖：** CMake ≥ 3.20 · C++20 编译器 · 已包含的 `third_party/fmt`

```bash
# 在已取得的仓库根目录执行
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

本仓库用 **doctest** 做单元测试：**一个可执行文件** `minispdlog_tests` 包含全部用例（`tests/unit/*.cpp`），CTest 再按 16 个组件源文件注册条目，既避免重复编译，也能并行运行并定位失败组件。

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

### 7. `examples/qt_log_viewer` — Qt 窗口看日志（可选）
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
- **占位符**：`%Y %m %d %H %M %S %e %f %u %E %l %L %v %q %t %P %n %s %# %! %@`；`%^` / `%$` 只标记着色区间，不输出字符
- **彩色输出**：`color_*_sink` 按 `log_msg::color_range_*` 给 `[start, end)` 套 ANSI；区间为空则整行着色
- **源码位置**：`lg.info("n={}", 7)` 经 `sourced_fmt` 在调用点捕获 file/line/func；宏 `MINISPDLOG_INFO` 用 `MINISPDLOG_LOC`
- **线程安全**：`base_sink<Mutex>` → `_mt` / `_st`；logger 的 sink 列表 copy-on-write，热路径不加 logger 锁做 I/O

### 异步日志

- **MPMC 阻塞队列**（默认）：多 worker，`block` / `overrun_oldest` / `discard_new`
- **无锁 MPSC**（可选）：`init_lockfree_thread_pool`，单 worker；只支持 `block` / `discard_new`，`overrun_oldest` 会抛异常
- **生产者热路径**：短消息使用 256 字节内联存储；线程池共享快照按线程缓存，仅在配置版本变化时刷新
- **可观测故障**：`error_count()` 记录异步 Sink 异常，工作线程继续运行，等待式 `flush()` 在调用方报告异常
- **有界刷新**：双代际屏障只等待提交点前的记录，不被后续持续流量拖住
- **接口透明**：`async_logger` 继承 `logger`，worker 走 `backend_sink_it_`（不再入队）；`async_file_mt` / `async_buffered_file_mt` / `async_json_file_mt` / `async_json_rotating_mt` / `async_callback_mt` / `async_udp_mt` / `async_tcp_mt` / `async_json_udp_mt` / `async_json_tcp_mt`
- **关闭**：`minispdlog::shutdown()` 先 flush，再停全局线程池，再 `drop_all`

### 协程控制面

- **低频控制操作**：`coro::flush(logger)`、`coro::flush_all()`、`coro::shutdown()` 可直接 `co_await`
- **不侵入数据热路径**：逐条 `log()` 不创建协程帧，仍沿用同步或异步队列路径
- **确定执行顺序**：共享单线程 FIFO 控制执行器串行处理刷新与关闭，异常在 `await_resume()` 重新抛出
- **运行时中立**：可选 `resume_scheduler` 将 continuation 投递回 Asio、Qt 或业务自己的事件循环

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
        if (msg.lvl >= minispdlog::level::error) {
            /// 可在此处告警或计数。
            (void)line;
        }
    });
auto logger = std::make_shared<minispdlog::logger>(
    "app", minispdlog::logger::sink_list{console, hook});
```

工厂：`callback_logger_mt/st`；异步：`async_callback_mt`。完整演示见 `examples/callback_log`。

### 自定义 pattern

```cpp
auto sink = std::make_shared<minispdlog::sinks::console_sink_mt>();
/// set_pattern：使用 pattern 字符串，并编译为 pattern_formatter。
sink->set_pattern("[%H:%M:%S.%e] [%L] [%s:%#] %v");

auto logger = std::make_shared<minispdlog::logger>("app", sink);
logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] [%s:%#] %v");
logger->log(minispdlog::level::info, "hello {}", name); ///< 一次 log 调用即可输出。

/// set_formatter：安装自定义 formatter，每个 sink 持有一份 clone。
logger->set_formatter(std::make_unique<MyFormatter>());
```

同一场景用同一套分隔：日期 `%Y-%m-%d`，时间 `%H:%M:%S.%e`（微秒用 `%f` 替代 `%e`），UTC `%u`，Unix 毫秒 `%E`，源码 `%s:%#` / `%@`，字段 `[..] [..]`。`%^`…`%$` 包住要上色的片段（默认只包 `%L`）。按行采集优先用 `%q`，保留原始多行正文再用 `%v`。`log()` / `info()` 会填充时间、线程、进程和源码位置。

结构化输出用 `json_formatter`（字段：UTC `time` / `ts` / `level` / `level_num` / `logger` / `msg` / `tid` / `pid`，有源码位置时带 `source`；资源字段用 `add` / `with_host`），不要用 pattern 去「手拼 JSON」。

分层与异步热路径细节见[同步与异步模型](docs/sync-async-model.md)与保留的长篇导读 [`explanation.md`](explanation.md)。

---

## 项目结构

```
minispdlog/
├── CMakeLists.txt
├── explanation.md              # 架构与用法长文
├── Doxyfile / LICENSE          # API 文档配置 / MIT 许可证
├── docs/                       # 专题指南、API 速查与架构图
├── include/minispdlog/         # 对外头文件
│   ├── minispdlog.h            # 用户 API / 工厂
│   ├── async.h / async_config.h
│   ├── coro.h                   # 协程刷新与关闭 Awaitable
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

## 许可证

minispdlog 自有代码采用 [MIT License](LICENSE)。`third_party/` 中的依赖保留各自许可证与版权声明；分发时请一并遵守。

如需浏览中文 API 页面，在仓库根目录安装 Doxygen 后运行 `doxygen Doxyfile`，输出位于 `build/docs/doxygen/html/`。

---

## 致谢

- [fmt](https://github.com/fmtlib/fmt) — 格式化  
- [doctest](https://github.com/doctest/doctest) — 单元测试  
