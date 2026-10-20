# Sink 指南

Sink 负责把 formatter 生成的字节送到最终目标。一个 logger 可以挂多个 Sink；每个 Sink 有独立级别、formatter 和同步策略。

## 能力矩阵

| Sink | 典型类型 / 工厂 | 输出目标 | 并发版本 | 关键语义 |
|------|-----------------|----------|----------|----------|
| 普通控制台 | `console_sink_*` / `stdout_mt` | stdout | `_mt`、`_st` | 文本输出，`flush()` 调用 `fflush(stdout)` |
| stderr | `stderr_sink_*` | stderr | `_mt`、`_st` | 需手工组合；工厂提供彩色或 JSON 版本 |
| 彩色控制台 | `color_console_sink_*` / `stdout_color_mt` | stdout | `_mt`、`_st` 类型 | 按 `%^...%$` 区间插入 ANSI 颜色 |
| 彩色 stderr | `color_stderr_sink_*` / `stderr_color_mt` | stderr | `_mt`、`_st` 类型 | 同上 |
| 基础文件 | `file_sink_*` / `basic_logger_mt/st` | 单文件 | `_mt`、`_st` | 每条 `fwrite`，显式刷新 `fflush` |
| 批量文件 | `buffered_file_sink_*` / `buffered_logger_mt/st` | 单文件 + WAL | `_mt`、`_st` | 双缓冲、阈值批次、恢复 |
| 大小滚动 | `rotating_file_sink_*` / `rotating_logger_mt` | 当前文件 + 编号归档 | `_mt`、`_st` 类型 | 写入将超过 `max_size` 前滚动 |
| 按日切分 | `daily_file_sink_*` / `daily_logger_mt/st` | 日期文件 | `_mt`、`_st` | 本地日历、可配置每日切点与保留天数 |
| JSON wrapper | `json_*_sink_*` / 多个 `*_json_*` 工厂 | 文件、控制台、网络 | 取决于底层 | 固定 JSON Lines formatter |
| 回调 | `callback_sink_*` / `callback_logger_mt/st` | 用户函数 | `_mt`、`_st` | 在 Sink 锁内调用回调 |
| 空输出 | `null_sink_*` | 只格式化并计数 | `_mt`、`_st` | 用于测 formatter，不代表完全无工作 |
| 网络 | `udp_sink_*`、`tcp_sink_*` | Linux UDP/TCP | `_mt`、`_st` | 非 Linux 构造抛异常 |
| Qt | `qt_sink_*` / `qt_logger_mt/st` | `QObject` 方法 | `_mt`、`_st` | 可选 Qt Widgets；不拥有目标对象 |

`_mt` 表示 Sink 内使用 `std::mutex`，适合共享；`_st` 使用 `null_mutex`，调用方必须保证不会并发进入。工厂未覆盖每个类型组合时，可以直接构造 Sink 和 `logger`。

## 多 Sink 组合

```cpp
auto console = std::make_shared<minispdlog::sinks::color_console_sink_mt>();
auto file = std::make_shared<minispdlog::sinks::file_sink_mt>("logs/app.log", false);
file->set_level(minispdlog::level::warn);

auto log = std::make_shared<minispdlog::logger>(
    "app", minispdlog::logger::sink_list{console, file});
minispdlog::register_logger(log);
```

logger 先按自身级别过滤，再为每个 Sink 调用 `should_log()`。直接调用 `sink->log(msg)` 不会替你执行级别过滤；公共使用方式应通过 logger。

运行中可以 `add_sink()` 或 `remove_sink()`。现有记录使用它取得的 Sink 快照，新记录看到之后发布的列表。

## Formatter 与 pattern

```cpp
log->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] [%s:%#] %v");
```

logger 会为当前每个 Sink 编译或克隆独立 formatter。主要 pattern 标志见 [API 速查](api.md)。JSON wrapper 默认拒绝被 pattern 改回文本，详见[结构化日志](structured-logging.md)。

## 文件类 Sink

所有文件类 Sink 都要求父目录已经存在。`truncate=true` 表示构造时重建文件，`false` 表示追加。

### 基础文件

`file_sink` 每条记录执行 `fwrite`，但 C 运行库仍可能缓冲；`flush()` 才调用 `fflush`。它没有 WAL 或断尾修复。

### 批量文件

`buffered_file_sink` 把多条记录合成批次，支持可配置 `fflush` / `fsync` 与打开时恢复。它和普通“异步 logger”解决不同问题：一个合并文件写入，一个把生产线程与 Sink 解耦。两者可以组合。详见[耐久性与恢复](durability-and-recovery.md)。

### 大小滚动

```cpp
auto log = minispdlog::rotating_logger_mt(
    "rotate", "logs/app.log", 10 * 1024 * 1024, 3);
```

`max_size` 与 `max_files` 都必须大于零。当前文件为 `app.log`，归档依次为 `app.1.log`、`app.2.log` 等；`max_files` 不含当前文件。若单条格式化记录本身大于 `max_size`，它会在新文件中完整写入，因此文件仍可能超过阈值。

### 按日切分

```cpp
auto log = minispdlog::daily_logger_mt(
    "daily", "logs/app.log", 0, 0, false, 7);
```

文件名形如 `app.2026-10-10.log`。切换依据 `log_msg::time` 转换后的本地日历时间；`rotation_hour` / `rotation_minute` 指每日切点。`max_files=0` 保留全部日期文件，否则清理早于保留窗口的日期文件。

## 回调 Sink

```cpp
auto log = minispdlog::callback_logger_mt(
    "hook",
    [](const minispdlog::details::log_msg& record, const std::string& line) {
        /// line 拥有自己的字节，可以保存；record 中的 view 只在回调期间有效。
        (void)record;
        (void)line;
    },
    [] {
        /// 在 flush 时执行收尾操作。
    });
```

回调在 Sink 锁内执行。不要从回调重新对同一个 Sink 调用 `log()` 或 `flush()`，否则 `_mt` 版本会死锁。长时间或可能阻塞的业务操作也会阻塞这个 Sink；需要隔离时使用 `async_callback_mt`，但回调依然在工作线程的 Sink 锁内。

## 网络 Sink

```cpp
minispdlog::network_config cfg = minispdlog::tcp_config("127.0.0.1", 9000);
cfg.connect_timeout_ms = 1000;
cfg.send_timeout_ms = 1000;
cfg.reconnect = true;
cfg.drop_on_error = true;
auto log = minispdlog::network_logger_mt("ship", cfg);
```

当前实现仅支持 Linux 内核 Socket。UDP 保留数据报边界但不保证到达；超过 `max_datagram` 的记录按错误策略丢弃或抛出。TCP 是字节流，发送失败时可重连。`drop_on_error=true` 会累计 `packets_dropped()`；设为 `false` 则把发送错误交给调用方。

`network_listener` 是轻量收集端和回环测试辅助器，不是生产级日志服务。TCP listener 以换行识别记录；默认文本和 JSON formatter 均满足这一边界。

## Qt Sink

配置时启用：

```bash
cmake -S . -B build -DMINISPDLOG_WITH_QT=ON
```

仅当 Qt 被找到时才定义功能宏并构建相应测试与示例。`qt_sink` 通过 `QMetaObject::invokeMethod(..., Qt::AutoConnection)` 调用目标方法，例如 `QTextEdit::append` 或 `QPlainTextEdit::appendPlainText`。

Sink 不拥有 `QObject`。窗口关闭前应停止日志、移除 Sink 或销毁 logger，保证目标对象生命周期覆盖所有调用。其 `flush()` 是空操作；跨线程投递后仍由 Qt 事件循环决定界面何时更新。

## 自定义 Sink

继承 `base_sink<Mutex>` 并实现 `sink_it_()`、`flush_()` 即可。实现遵循三个约束：

1. 在 `sink_it_()` 中使用 `format_message(msg)`，不要长期保存 `log_msg` 的字符串视图；
2. 假定基类已取得 Sink 锁，不要再次锁同一个非递归 mutex；
3. 明确刷新、异常和外部资源生命周期。

自定义输出若只需一个函数，优先考虑 callback Sink，减少新类型和生命周期代码。
