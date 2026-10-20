# API 速查

本页按公开使用场景汇总 `0.1.0` 接口。精确签名以头文件和 Doxygen 输出为准；行为解释链接到各专题文档。

## 头文件

| 头文件 | 主要内容 |
|--------|----------|
| `<minispdlog/minispdlog.h>` | 同步 logger、registry、常用 Sink 与工厂、全局日志 API |
| `<minispdlog/async.h>` | 线程池、`async_logger` 与异步工厂 |
| `<minispdlog/json_formatter.h>` | JSON Lines formatter |
| `<minispdlog/network.h>` | 网络配置、client 与 listener |
| `<minispdlog/lockfree_queue.h>` | SPSC/MPSC 队列原语；一般业务优先使用 `async.h` |
| `<minispdlog/sinks/qt_sink.h>` | 可选 Qt Sink；仅定义 `MINISPDLOG_WITH_QT` 时可包含 |

`minispdlog.h` 不会自动包含异步 API；使用 `init_thread_pool()` 或异步工厂时显式包含 `async.h`。

## 版本与日志级别

```cpp
minispdlog::VERSION; ///< 当前版本为 "0.1.0"。
```

级别从低到高：

```text
trace(0) < debug(1) < info(2) < warn(3) < error(4) < critical(5) < off(6)
```

相关函数：

- `level_to_string(level)`：完整小写名称；
- `level_to_short_string(level)`：`T/D/I/W/E/C/O`；
- `level_to_string_view(level)`：无分配视图；
- `string_to_level(string)`：大小写不敏感，未知值回退到 `info`；
- `should_log(logger_level, msg_level)`：比较级别阈值。

## `logger`

构造方式：空 Sink 列表、单个 `sink_ptr`，或 `logger::sink_list`。常用成员：

```cpp
log->trace("x={}", x);
log->debug("x={}", x);
log->info("x={}", x);
log->warn("x={}", x);
log->error("x={}", x);
log->critical("x={}", x);
log->log(minispdlog::level::info, "x={}", x);

log->set_level(minispdlog::level::info);
auto lvl = log->get_level();
bool enabled = log->should_log(minispdlog::level::debug);

log->set_pattern("[%L] %v");
log->set_formatter(std::make_unique<MyFormatter>());
log->flush_on(minispdlog::level::error);
log->flush();

log->add_sink(sink);
log->remove_sink(sink);
auto snapshot = log->sinks();
auto name = log->name();
```

格式字符串使用 fmt 的编译期检查。logger 新实例的级别默认是 `trace`；进程启动时 registry 创建的默认 logger 级别是 `info`。Sink 还有自己的级别阈值，两层都允许时才输出。

`flush_on(level)` 在达到该级别的每条记录之后刷新当前 Sink。默认值 `off` 表示普通日志不会自动刷新。

## 全局 API 与 registry

```cpp
auto log = minispdlog::get("name");
minispdlog::register_logger(log);
minispdlog::drop("name");
minispdlog::drop_all();

auto current = minispdlog::default_logger();
minispdlog::set_default_logger(log);
minispdlog::set_level(minispdlog::level::warn);
minispdlog::flush_all();
minispdlog::shutdown();
```

工厂会自动注册 logger；同名注册抛出 `std::runtime_error`。`set_level()` 会更新默认 logger 和 registry 中所有具名 logger。`flush_all()` 取得快照后在 registry 锁外刷新。

全局记录函数使用默认 logger：

```cpp
minispdlog::trace("...");
minispdlog::debug("...");
minispdlog::info("...");
minispdlog::warn("...");
minispdlog::error("...");
minispdlog::critical("...");
```

编译期级别宏：

```cpp
MINISPDLOG_TRACE("...");
MINISPDLOG_DEBUG("...");
MINISPDLOG_INFO("...");
MINISPDLOG_WARN("...");
MINISPDLOG_ERROR("...");
MINISPDLOG_CRITICAL("...");
```

构建前定义 `MINISPDLOG_ACTIVE_LEVEL` 为 `MINISPDLOG_LEVEL_*` 之一，可在编译期移除更低等级宏调用。根 CMake 的 Release 单配置默认定义为 `MINISPDLOG_LEVEL_INFO`。

## 同步工厂

所有工厂第一个参数都是唯一 logger 名称并返回 `std::shared_ptr<logger>`。

### 控制台与文件

| 工厂 | 主要参数 | 输出 |
|------|----------|------|
| `stdout_color_mt(name)` | 名称 | 彩色 stdout |
| `stderr_color_mt(name)` | 名称 | 彩色 stderr |
| `stdout_mt(name)` | 名称 | 普通 stdout |
| `basic_logger_mt/st(name, file, truncate=false)` | 路径、是否截断 | 基础文件 |
| `buffered_logger_mt/st(name, file, truncate=false, batch={})` | 路径、批次配置 | durable 批量文件 |
| `rotating_logger_mt(name, file, max_size, max_files)` | 字节上限、归档数 | 按大小滚动 |
| `daily_logger_mt/st(name, file, hour=0, minute=0, truncate=false, max_files=0)` | 每日切点、保留数 | 按日切分 |

`rotating_file_sink_st` 类型存在，但没有 `rotating_logger_st` 快捷工厂；需要时直接构造 Sink 与 logger。

### JSON

| 工厂 | 底层输出 |
|------|----------|
| `json_logger_mt/st(...)` | durable 批量文件 |
| `stdout_json_mt/st(name, formatter={})` | stdout |
| `stderr_json_mt/st(name, formatter={})` | stderr |
| `rotating_json_logger_mt/st(...)` | 按大小滚动文件 |
| `daily_json_logger_mt/st(...)` | 按日切分文件 |
| `json_udp_logger_mt/st(...)` | Linux UDP |
| `json_tcp_logger_mt/st(...)` | Linux TCP |

各工厂可接收 `json_formatter`，用于预配置资源字段。字段契约见[结构化日志](structured-logging.md)。

### 网络与回调

```cpp
udp_logger_mt/st(name, host, port);
tcp_logger_mt/st(name, host, port);
network_logger_mt(name, network_config);

callback_logger_mt/st(name, formatted_callback, flush_callback = {});
callback_logger_mt/st(name, record_callback, flush_callback = {});
```

回调签名二选一：

```cpp
void(const std::string& formatted);
void(const minispdlog::details::log_msg&, const std::string& formatted);
```

`formatted` 是拥有数据的字符串；`log_msg` 中的字符串视图只在回调期间有效。

## 异步 API

初始化快捷方式：

```cpp
minispdlog::init_thread_pool(queue_size, thread_count); ///< 阻塞式 MPMC 后端。
minispdlog::init_thread_pool(options);
minispdlog::init_lockfree_thread_pool(queue_size); ///< 无锁 MPSC 后端，固定一个工作线程。
auto* pool = minispdlog::get_thread_pool();        ///< 由线程局部快照保活的非拥有型指针。
```

`get_thread_pool()` 会在线程局部保存一次共享快照，使返回指针在本线程下一次调用前不会悬空；但重配置后该旧池可能已停止。需要跨调用或跨线程使用时，通过 `thread_pool_manager::instance().get()` 显式取得 `shared_ptr`。

配置类型：

```cpp
enum class async_queue_type { blocking, lockfree };
enum class async_overflow_policy { block, overrun_oldest, discard_new };

struct thread_pool_options {
    std::size_t queue_size = 8192;
    std::size_t thread_count = 1;
    async_queue_type queue_type = async_queue_type::blocking;
    std::size_t wake_batch = 64;
    std::uint32_t wake_interval_us = 100;
};
```

异步工厂：

- `async_file_mt(name, file, truncate, policy=block)`；
- `async_buffered_file_mt(name, file, truncate, policy=block, batch={})`；
- `async_json_file_mt(name, file, truncate, policy=block, batch={}, formatter={})`；
- `async_json_rotating_mt(name, file, max_size, max_files, policy=block, formatter={})`；
- `async_udp_mt(...)`、`async_tcp_mt(...)`；
- `async_json_udp_mt(...)`、`async_json_tcp_mt(...)`；
- `async_callback_mt(...)` 两种回调重载。

线程池暴露 `overrun_count()`、`discard_count()`、`queue_type()` 和 `worker_count()`。完整限制见[同步与异步模型](sync-async-model.md)。

## 批量文件配置

```cpp
enum class durability { none, fflush, fsync };

struct batch_config {
    std::size_t max_bytes = 64 * 1024;
    std::size_t max_records = 256;
    std::chrono::milliseconds max_age{50};
    durability commit = durability::fflush;
    bool recover = true;

    static batch_config until_flush();
};
```

全局辅助接口：

- `dump_buffered_logs() noexcept`：尽力转储所有已注册 durable writer 的前台缓冲；
- `install_crash_flush()`：安装一次进程 fatal handler，属于 best-effort。

详细保证与限制见[耐久性与恢复](durability-and-recovery.md)。

## Pattern 标志

默认 pattern：

```text
[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] %v
```

| 标志 | 内容 | 标志 | 内容 |
|------|------|------|------|
| `%Y` | 四位年 | `%m` | 两位月 |
| `%d` | 两位日 | `%H` | 两位小时 |
| `%M` | 两位分钟 | `%S` | 两位秒 |
| `%e` | 三位毫秒 | `%f` | 六位微秒 |
| `%L` | 完整级别名 | `%l` | 单字符级别 |
| `%n` | logger 名 | `%v` | payload |
| `%t` | 线程标识 | `%P` | 进程标识 |
| `%s` | 源文件 basename | `%g` | 完整源文件路径 |
| `%#` | 源码行号 | `%!` | 函数名 |
| `%@` | `basename:line` | `%%` | 字面量 `%` |
| `%^` | 彩色区间开始 | `%$` | 彩色区间结束 |

未知 `%x` 会原样保留为 `%x`。formatter 总是在记录末尾追加 `\n`。颜色标志只设置字节区间，普通文件 Sink 不写 ANSI 转义。

## JSON formatter

```cpp
minispdlog::json_formatter fmt;
fmt.add("key", "value")
   .add_int("count", 1)
   .add_bool("ready", true)
   .add_null("optional")
   .with_host();
fmt.clear_fields();
```

专用 `json_sink_wrapper` 还提供：

- `json()`：取得当前 live `json_formatter`；
- `allow_custom_formatting(bool=true)`：允许改用非 JSON formatter。

## 网络配置

```cpp
enum class network_protocol { udp, tcp };

struct network_config {
    std::string host = "127.0.0.1";
    std::uint16_t port = 514;
    network_protocol protocol = network_protocol::udp;
    bool tcp_nodelay = true;
    bool reconnect = true;
    bool drop_on_error = true;
    int send_timeout_ms = 2000;
    int connect_timeout_ms = 2000;
    std::size_t max_datagram = 65507;
};
```

辅助函数：`udp_config(host, port)`、`tcp_config(host, port)`、`network_sink_supported()`。

`network_client` 提供 `send()`、`try_recv()`、`close()`、连接和端点查询，以及 sent/dropped 计数。`network_listener` 提供 `local()`、`try_recv()` / `recv()`、回复、断开 TCP peer 与关闭，主要用于轻量收集和回环测试。当前真实实现只在 Linux 可用。

## Qt 工厂

启用 Qt 后：

```cpp
auto log = minispdlog::qt_logger_mt("ui", text_edit, "append");
auto plain = minispdlog::qt_logger_st("plain", plain_text_edit, "appendPlainText");
```

目标必须是非空 `QObject*`，方法名不能为空，且对象生命周期必须由应用保证。详见 [Sink 指南](sinks.md)。
