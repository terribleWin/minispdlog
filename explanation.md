# minispdlog 项目说明

> 本文结合仓库**真实代码与路径**，说明项目定位、分层架构、核心数据流、技术栈落地方式、线程安全边界，以及测试 / Qt 等工程用法。  
> 标准：**C++20** · 架构参考 [spdlog](https://github.com/gabime/spdlog) · 格式化依赖 `third_party/fmt`

---

## 目录

1. [项目概览](#1-项目概览)
2. [技术栈一览](#2-技术栈一览)
3. [分层架构与模块职责](#3-分层架构与模块职责)
4. [核心设计：消息、同步与异步](#4-核心设计消息同步与异步)
5. [输出层：Sink / Formatter / 颜色 / GUI](#5-输出层sink--formatter--颜色--gui)
6. [现代 C++ 在本项目中的用法](#6-现代-c-在本项目中的用法)
7. [线程安全保证](#7-线程安全保证)
8. [测试、基准与 CI](#8-测试基准与-ci)
9. [不同平台下的 Qt 使用方法](#9-不同平台下的-qt-使用方法)
10. [构建、依赖与推送约定](#10-构建依赖与推送约定)
11. [设计决策（ADR）](#11-设计决策adr)
12. [建议阅读顺序](#12-建议阅读顺序)

---

## 1. 项目概览

`minispdlog` 是一个从零实现的轻量级 C++ 日志库，目标是在保持 API 简单的同时，具备：

| 能力 | 含义 |
|------|------|
| 同步日志 | 调用线程完成「用户内容格式化 → formatter 拼行 → 写 Sink」 |
| 异步日志 | 业务线程只入队；工作线程走 `backend_sink_it_` 写 I/O，业务与磁盘解耦 |
| 多 Sink | 一个 logger 可同时写控制台、文件、滚动/按天文件、JSON Lines、回调、可选 Qt |
| 可扩展 | 虚接口 `sink` / `formatter`；文本用 `pattern_formatter`，结构化用 `json_formatter` |
| 工程化 | Registry、`shutdown()`、doctest、可选 Benchmark、CI lint + ASan/TSan |

用户侧通常只需要：

```cpp
#include <minispdlog/minispdlog.h>

auto lg = minispdlog::stdout_color_mt("app");
minispdlog::set_default_logger(lg);
minispdlog::info("Hello, {}!", "World");
```

---

## 2. 技术栈一览

| 类别 | 选型 | 在本项目中的角色 |
|------|------|------------------|
| 语言标准 | **C++20** | `CMakeLists.txt` 强制；使用 `if constexpr`、`string_view`、`format_string` 等 |
| 构建 | **CMake ≥ 3.11** | 静态库 `minispdlog`、测试、可选 Qt/Benchmark、ASan/TSan 变体 |
| 字符串格式化 | **{fmt}**（`third_party/fmt`） | `logger::log` 中 `fmt::format_to`；编译期检查格式串 |
| 异步队列 | 自研 **circular_q + mpmc_blocking_queue** | 热路径；mutex + 双条件变量 |
| 无锁队列 | 自研 **spsc_queue / mpsc_queue**（Vyukov） | 可通过 `init_lockfree_thread_pool` / `async_queue_type::lockfree` 接入异步热路径（单 worker） |
| 线程 | `std::thread` + 自研 **thread_pool** | 预创建消费者；析构 `terminate` + `join` |
| 同步原语 | `mutex` / `condition_variable` / `lock_guard` / `unique_lock` | 队列、Sink、Registry |
| 生命周期 | `shared_ptr` / `unique_ptr` / `enable_shared_from_this` | logger、Sink、异步消息、formatter |
| 控制台着色 | **ANSI SGR 转义码**（非第三方着色库） | `color_console_sink.h` |
| GUI（可选） | **Qt5/Qt6 Widgets** | `qt_sink` + `QMetaObject::invokeMethod` |
| 单元测试 | **doctest**（header-only，`tests/framework/`） | 单入口 `minispdlog_tests` + 标签过滤 |
| 性能 | **Google Benchmark**（可选系统包） | `benchmark_async` / `benchmark_queue` |
| CI | GitHub Actions | lint + g++/clang × Release/ASan/TSan + Windows MSVC + coverage |
| Qt 安装 | **aqtinstall** 脚本 | `scripts/setup_qt.sh` / `.ps1` → `third_party/qt`（不提交 SDK） |

---

## 3. 分层架构与模块职责

### 3.0 30 秒搞懂分层

把日志库想成四条流水线工序，**自上而下调用，自下而上提供能力**：

| 层 | 一句话 | 回答的问题 |
|----|--------|------------|
| **① 用户 API** | 给业务写的「快捷入口」 | 怎么一行代码打出日志？ |
| **② 管理调度** | 管 logger 名单 +（可选）异步投递 | 谁在跑、消息去哪个线程？ |
| **③ 格式化 / 输出** | 真正「过滤 → 拼内容 → 写出」 | 写什么、写到哪？ |
| **④ 基础设施** | 级别、消息结构、fmt 等积木 | 用什么数据结构描述一条日志？ |

**依赖铁律**：上层可以依赖下层；下层**不知道**上层（例如 `level` / `log_msg` 从不 `#include` `async.h`）。

```
业务代码
   │ 调用
   ▼
① 用户 API  ──薄包装──►  ② 管理调度（registry / 异步入队）
                              │ 最终落到
                              ▼
                         ③ logger + Sink + formatter（干活）
                              │ 使用
                              ▼
                         ④ level / log_msg / fmt（积木）
```

记忆口诀：**API 好用 → 调度管人/管线程 → 输出层干活 → 基建层供零件。**

---

### 3.1 四层对照（职责 / 不做的事 / 关键文件）

```
┌─────────────────────────────────────────────────────────────────────────┐
│ ① 用户 API 层                                                            │
│    minispdlog.h / async.h（工厂与 init_*）                                │
│    全局 info/warn、工厂（含 daily/json/async_*）、MINISPDLOG_* 宏           │
├─────────────────────────────────────────────────────────────────────────┤
│ ② 管理调度层                                                              │
│    registry · async_logger · thread_pool · 队列（blocking / lockfree）     │
│    「注册表 + 异步投递」；不负责拼最终输出行                               │
├─────────────────────────────────────────────────────────────────────────┤
│ ③ 格式化 / 输出层                                                         │
│    logger · sink/base_sink · 各具体 Sink · pattern_formatter / json_formatter │
│    级别过滤、fmt payload、按 sinks_ 写出；同步路径的主战场                  │
├─────────────────────────────────────────────────────────────────────────┤
│ ④ 基础设施层                                                              │
│    level · log_msg / async_msg · common · utils · third_party/fmt          │
│    类型与规则；不决定「写到哪个文件」                                       │
└─────────────────────────────────────────────────────────────────────────┘
```

| 层 | 负责 | **不**负责 | 关键路径 |
|----|------|-----------|----------|
| ① 用户 API | 默认 logger、工厂、全局函数、编译期宏 | 队列实现、Sink 细节 | `minispdlog.h`、`async.h` |
| ② 管理调度 | 注册/查找 logger；异步入队与 worker 调度；溢出策略 | 拼最终输出行、具体 I/O | `registry.*`、`async.h`、`thread_pool.*`、`mpmc_blocking_q.h`、`mpsc_queue.h` |
| ③ 格式化/输出 | `should_log`、fmt、遍历 Sink、formatter | 全局命名表、线程池生命周期 | `logger.*`、`sinks/*`、`pattern_formatter.*`、`json_formatter.*` |
| ④ 基础设施 | `level` 语义、`log_msg` 字段、时间/线程 id 工具、fmt | 业务 API、异步策略 | `level.h`、`details/log_msg.h`、`async_msg.h`、`common.h` |

易混点（面试常问）：

- **级别过滤有两处用法，不是两套引擎**：④ 定义「`msg >= 门槛`」规则；③ 的 `logger` / Sink 在运行时执行；② 的 `registry::set_level` 只是**批量改门槛**。  
- **`logger` 算第 ③ 层**：名字像「管理」，但核心是过滤与写出；`async_logger` 才把「投递」抬到第 ② 层。  
- **队列在第 ② 层**：管调度与跨线程搬运；环形缓冲本身不写磁盘。

---

### 3.2 一条日志怎么穿过各层？

**同步**（默认 `logger`）：几乎只走 ① → ③ → ④，不经线程池。

```
info("x") → API/默认 logger
         → logger::log          【③】级别过滤 + fmt 用户串 → 造 log_msg【④】（含 source_loc）
         → logger::sink_it_     【③】= backend_sink_it_：快照 sinks_，各 Sink::log
         → Sink::log → formatter【③】写出（pattern 或 JSON）
```

**异步**（`async_logger` + 已 `init_thread_pool` / `init_lockfree_thread_pool`）：

```
info("x") → logger::log                 【③】过滤 + fmt（仍在业务线程；sourced_fmt 已带 loc）
         → async_logger::sink_it_       【②】打包 async_msg，入队
         → thread_pool worker           【②】出队
         → logger::backend_sink_it_     【③】写 Sink（禁止再走虚 sink_it_，否则会再次入队）
```

| | 同步 | 异步 |
|--|------|------|
| 业务线程做什么 | 过滤 + fmt + 写 Sink | 过滤 + fmt + **入队** |
| 谁写磁盘/控制台 | 业务线程 | worker |
| 队列 | 无 | blocking MPMC（可多 worker）或 lockfree MPSC（单 worker） |

要点：同步/异步共用 `logger::log`；差别只在虚函数 `sink_it_`（异步重写为投递）。Worker 必须调 `backend_sink_it_`，否则会再次入队。

---

### 3.3 核心对象关系（跨层怎么挂在一起）

```
        ② registry（单例）
              │ shared_ptr<logger>
              ▼
     ┌── ③ logger / async_logger ──┐
     │  name_ / atomic level_ / COW sinks_ │
     └────────────┬────────────────┘
                  │ sink_ptr
     ┌────────────┼────────────┐
     ▼            ▼            ▼
  console / file / rotating / daily / json / callback / qt_*   【③】
     └──── base_sink<Mutex> ──► formatter（pattern 或 json） ──┘

异步仅改投递（②），目的地仍在 logger.sinks_（③）：
  async_logger::sink_it_
    → thread_pool → 队列<async_msg>（含 worker_ptr）
    → worker: worker_ptr->backend_sink_it_ → 同上 Sink 路径
```

- **目的地不写在队列字符串里**，而在 logger 的 `sinks_`。  
- `async_msg::worker_ptr` 保活 logger，并告诉 worker「用哪套 Sink」。  
- 换 Sink / 换同步异步，尽量不动 ④ 的消息定义。

---

### 3.4 目录与职责（工程视角）

| 路径 | 对应分层 / 职责 |
|------|-----------------|
| `include/minispdlog/*.h` | ① 及部分 ②/③ 对外接口 |
| `include/minispdlog/details/` | ② 队列/线程池、④ 消息与工具（内部） |
| `include/minispdlog/sinks/` | ③ 输出插件 |
| `include/minispdlog/json_formatter.h` / `pattern_formatter.h` | ③ 行格式（JSON Lines / 文本 pattern） |
| `include/minispdlog/lockfree_queue.h` | ②/对外：无锁队列便捷入口 |
| `src/` | 上述实现，链成静态库 `minispdlog` |
| `tests/unit/`、`tests/framework/` | 按组件测各层行为 |
| `examples/`、`scripts/`、`cmake/` | 示例与可选 Qt 工程化 |

---

## 4. 核心设计：消息、同步与异步

### 4.1 两种消息结构（关系，不是平行两套）

| 类型 | 文件 | 角色 |
|------|------|------|
| `log_msg` | `details/log_msg.h` | **同步与异步共用**载体：级别、时间、tid/pid、logger 名、payload、`source_loc`、`color_range_*` |
| `async_msg` | `details/async_msg.h` | 继承 `log_msg_buffer`（再继承 `log_msg`），增加 `msg_type` 与 `worker_ptr`，专供**队列传递** |

```
log_msg  ←  log_msg_buffer（深拷贝 payload → std::string）  ←  async_msg
                                                              + msg_type
                                                              + shared_ptr<logger> worker_ptr
```

`worker_ptr` 指向的是 **logger**，不是某个文件路径；多目的地由 logger 内多个 Sink 完成。  
使用 `shared_ptr` 的主因是 **异步期间延长 logger 生命周期**，不是「一个指针对应多个目的地」。

### 4.2 两层「格式化」

| 阶段 | 何时 | 做什么 |
|------|------|--------|
| ① fmt 用户内容 | 业务线程 `logger::log` | `"User {}"` + `"Alice"` → `"User Alice logged in"` |
| ② 整行 formatter | Sink 写出时 | **pattern**：时间/级别等拼成文本行；**json**：一行一个对象 |

`info()` / `log()` 通过 `sourced_fmt` 在调用点填入 `source_loc`（file/line/func）。宏路径用 `MINISPDLOG_LOC`。

异步入队时：① 已完成，且已采样 `time` / `thread_id` / `process_id` / `lvl` / `source`；② 在工作线程的 Sink 上做。  
这样时间戳反映「调用点」，且避免把临时参数跨线程传递。

默认文本 pattern：`[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] %v`。`%^` / `%$` 不输出字符，只写入 `log_msg::color_range_start/end`。

### 4.3 同步调用链

```
logger->info("User {} logged in", "Alice")   // sourced_fmt 捕获调用点
  → logger::log（logger.h）
       should_log → fmt::format_to → 构造 log_msg → sink_it_
  → logger::sink_it_ = backend_sink_it_（logger.cpp）：atomic 快照 sinks_
  → base_sink::log：lock_guard → 子类 sink_it_
  → 例如 color_console_sink：format_message → write_colored（按 color_range 上色）
  → pattern_formatter::format 或 json_formatter::format
```

### 4.4 异步调用链

```
业务线程：同上 logger::log（得到 log_msg）
  → async_logger::sink_it_（async.h）
  → post_log(..., overflow_policy)（block / overrun_oldest / discard_new）
  → async_msg 入队（payload 深拷贝 + worker_ptr；flush 可带 promise ack）
工作线程：出队后解锁
  → worker_ptr->backend_sink_it_ → 与同步相同的 Sink / formatter / I/O
进程退出：minispdlog::shutdown() → flush_all + 停线程池 + drop_all
```

使用前需：

```cpp
minispdlog::init_thread_pool(/*queue_size*/ 8192, /*threads*/ 1);
auto lg = minispdlog::async_file_mt("async", "async.log", false);
// 或 async_json_file_mt("async", "async.json.log", false);
lg->info("hello");
minispdlog::shutdown();
```

### 4.5 环形缓冲与阻塞队列

**`circular_q`**（`details/circular_q.h`）：

- 构造时 `std::vector<T> v_(容量)` **一次预分配**；
- `push`/`pop` 主要移动 `head_`/`tail_`，**不**对环做 `resize`；
- 单独使用时**不是**线程安全的。

**`mpmc_blocking_queue`**（`details/mpmc_blocking_q.h`）：

- 内部持有 `circular_q`，所有访问包在 `mutex_` 下 → **线程安全 MPMC**；
- 生产：`enqueue`（满则阻塞）/ `enqueue_nowait`（满则覆盖最旧）；
- 消费：`dequeue_for`（`wait_for`：被唤醒或超时）；
- 对应策略枚举 `async_overflow_policy`：`block` / `overrun_oldest` / `discard_new`。
- 无锁后端只允许 `block` 与 `discard_new`；`overrun_oldest` 会抛异常。

**注意**：「队列槽位预分配」≠ 整条链路零分配；`async_msg` 仍会为 payload 分配 `std::string`。

### 4.6 线程池

`details::thread_pool`：构造时 `vector<thread>` 拉起 N 个消费者，共享一条队列。  
公开接口：`post_log` / `post_log_nowait` / `post_flush` / `overrun_count`。  
全局默认由 `thread_pool_manager` 单例持有**一个**池；生产者是业务线程，不必再开「生产线程池」。

---

## 5. 输出层：Sink / Formatter / 颜色 / GUI

### 5.1 已实现的输出方向

| Sink | 文件 | 说明 |
|------|------|------|
| `console_sink` / `stderr_sink` | `console_sink.h` | 标准输出/错误，无颜色 |
| `color_console_sink` / `color_stderr_sink` | `color_console_sink.h` | 按 `color_range_*` 套 ANSI；默认只涂级别名 |
| `file_sink` | `file_sink.h` | 普通文件（逐条 `ofstream`） |
| `buffered_file_sink` | `buffered_file_sink.h` | 双缓冲批量写 + WAL + 半截行 salvage |
| `rotating_file_sink` | `rotating_file_sink.h` | 按大小滚动 |
| `daily_file_sink` | `daily_file_sink.h` | 按天切分 |
| `json_file_sink` / `json_console_sink` / `json_stderr_sink` | `json_sink.h` | JSON Lines；文件 sink 继承 `buffered_file_sink`（LF） |
| `json_rotating_file_sink` / `json_daily_file_sink` | `json_sink.h` | 滚动/按天 JSON，锁住 formatter |
| `callback_sink` | `callback_sink.h` | 业务回调：格式化行和/或完整 `log_msg`；可选 flush 钩子。工厂 `callback_logger_mt/st`、`async_callback_mt` |
| `qt_sink`（可选） | `qt_sink.h` | 写入 `QTextEdit` / `QPlainTextEdit` |
| `mock_sink`（测试） | `tests/framework/mock_sink.h` | 内存捕获断言 |

每类通常有 `_mt`（`std::mutex`）与 `_st`（`null_mutex`）。

### 5.2 插件扩展方式

1. 继承 `base_sink<Mutex>`，实现 `sink_it_` / `flush_`；  
2. 或实现虚接口 `formatter`：文本用 `pattern_formatter`（`set_pattern`），结构化用 `json_formatter`（`set_formatter`）；  
3. 或不写新类：把输出交给 `callback_sink`（单参格式化行，或双参 `log_msg` + 行）。回调在 sink 锁内执行；`log_msg` 的 view 只在回调期间有效。`qt_sink` 是把行投到 Qt 控件的特化。

`json_*` sink 在构造时安装 `json_formatter`，并覆盖 `set_pattern` / `set_formatter`，避免 `logger->set_pattern` 把结构化输出改回纯文本。任意其它 sink（含 rolling/daily 文本 sink）仍可 `set_formatter(std::make_unique<json_formatter>())`；要滚动仍保持 JSON，用 `json_rotating_file_sink` / `rotating_json_logger_mt`。

后续还可扩展按小时滚动、syslog、网络 Sink 等。

### 5.3 颜色如何实现

着色是 **formatter 标区间 + color sink 涂色**，不是往 payload 里塞 ANSI：

1. `pattern_formatter` 遇到 `%^` / `%$` 时把当前已写出长度写入 `log_msg::color_range_start/end`（半开区间）；每次 `format()` 开头先清零。  
2. `color_console_sink` / `color_stderr_sink` 的 `write_colored` 只给 `[start, end)` 套级别对应的 ANSI；`end <= start` 则整行着色。  
3. 文件 / JSON sink 不读这两个字段，输出不含颜色码。

默认只给级别名上色：`[%^%L%$]`。

### 5.4 JSON Lines

`json_formatter` 每条日志一行对象，字段：`time`（UTC ISO-8601 `YYYY-MM-DDTHH:MM:SS.mmmZ`，按秒缓存日历部分）、`ts`（Unix epoch 毫秒）、`level`、`level_num`（与 `level` 枚举相同）、`logger`、`msg`、`tid`、`pid`；`source` 仅在 `source_loc` 非空时出现。字符串按 RFC 8259 转义，连续非转义字节整段拷贝，并转义 U+2028 / U+2029（NDJSON / `JSON.parse`）。`add` / `add_int` / `add_bool` / `add_null` / `with_host()` 写入静态资源字段，clone 与 `json_*` sink 的 `set_pattern` 会保留它们。文件类 `json_file_sink` 走 `buffered_file_sink`（批量双缓冲、WAL、LF）。

工厂：`json_logger_mt/st`、`rotating_json_logger_mt/st`、`daily_json_logger_mt/st`、`stdout_json_mt/st`、`stderr_json_mt/st`、`async_json_file_mt`、`async_json_rotating_mt`。末参可传 `json_formatter`。`json_logger_*` / `async_json_file_mt` 可传 `batch_config`。`sink->json()` 在开始打日志前改资源字段。

### 5.5 批量写入、双缓冲与崩溃找回

热路径把已格式化的字节追加到 **front** buffer；达到 `batch_config` 的字节/条数/时长阈值（或 `flush()` / 析构）时 **swap**，把 **frozen** 侧一次 `fwrite` 出去。`_mt` 在写 frozen 时放开 sink 锁，生产者可以继续填新的 front。

提交一批时若 `recover==true`，先写 sidecar `日志路径.minispdlog-wal`（magic `MSLG` + 目标偏移 + payload），再写入主文件，成功后删除 WAL。下次打开：

1. 按 WAL 里的 `target_offset` 与主文件当前大小决定：补写、截断半截 fwrite 再补写、或认定已提交只删 WAL（避免重复行）；
2. 再从文件尾往前找最后一个 `\n`，丢掉半截最后一行。

`durability::fflush` 让已提交批次在进程崩溃后通常仍在内核页缓存里；`fsync` 才抗住整机掉电。未 swap 的 front 只在内存中：`install_crash_flush()` / `dump_buffered_logs()` 会尽力写出（不是 POSIX async-signal-safe）；`kill -9` 和异步队列里尚未出队的消息不在这套机制里。

工厂：`buffered_logger_mt/st`、`async_buffered_file_mt`。

### 5.6 Registry

`registry` 单例用 `unordered_map<string, shared_ptr<logger>>` 管理命名 logger；工厂函数创建后 `register_logger`。  
全局 `minispdlog::info` 走 `default_logger()`。  
`shutdown()`：`flush_all` → 停全局 `thread_pool_manager` → `drop_all`。

logger 的 `sinks_` 是 copy-on-write：`add_sink` / `remove_sink` 持 mutex 发布新 vector；`log` / `flush` 只 `atomic_load` 快照，I/O 期间不持 logger 锁。级别用 `atomic<level>`。

---

## 6. 现代 C++ 在本项目中的用法

工程强制 **C++20**。按解决问题归类：

### 6.1 RAII 与智能指针

| 特性 | 位置 | 作用 |
|------|------|------|
| `unique_ptr` | Sink 的 `formatter_`、线程池管理器 | 独占 |
| `shared_ptr` | registry / sinks / `async_msg::worker_ptr` | 共享；异步保活 |
| `enable_shared_from_this` | `logger` | `shared_from_this()` 安全入队 |
| `lock_guard` / `unique_lock` | 队列、Sink、registry | 作用域解锁 |
| 禁止拷贝 `= delete` | registry、thread_pool、circular_q、base_sink | 防双份状态 |
| 线程 `join` 于析构 | `thread_pool` | 线程生命周期绑定池对象 |

### 6.2 零开销抽象与泛型

- `base_sink<Mutex>` + `null_mutex`：一套代码覆盖多线程锁 / 单线程空锁；  
- 变参模板 + `std::forward` + `fmt::format_string`：类型安全、少拷贝；  
- 模板队列与模板 Sink：与具体类型解耦。

### 6.3 编译期

- `if constexpr` + `MINISPDLOG_ACTIVE_LEVEL`：Release 可剥掉低级别日志（目前主要作用于 `MINISPDLOG_*` 宏）；  
- `sourced_fmt`：`consteval` 在调用点捕获 `__builtin_FILE/LINE/FUNCTION`；  
- `enum class`：级别、消息类型、溢出策略、队列类型。

### 6.4 移动与视图

- 队列传递优先 `move`；`async_msg` 禁止拷贝；  
- 同步路径可用 `string_view`；**入队前**必须深拷贝为拥有型 `string`（`log_msg_buffer`）。

### 6.5 并发与 OOP

- 默认热路径：`mutex` + `condition_variable`（MPMC）；可选 `async_queue_type::lockfree`（MPSC + `atomic.wait`）；  
- 虚接口 `sink`/`formatter`、`async_logger` 继承 `logger` 只改投递；  
- Meyers' Singleton：`registry::instance()` 等（C++11 起初始化线程安全）。

### 6.6 导出宏

`MINISPDLOG_API`（`common.h`）：为 Windows DLL 预留 `dllexport`/`dllimport`；当前默认静态库时为空。标在 `logger` / `registry` / `thread_pool` 及部分自由函数上。

---

## 7. 线程安全保证

### 7.1 并发模型

```
业务线程 × N  ──enqueue──►  共享 mpmc_blocking_queue
工作线程 × M  ──dequeue──┘
                         │ 出队后释放队列锁
                         ▼
                    logger::sink_it_ → 各 Sink（各自 mutex）
```

### 7.2 组件级保证

| 组件 | 线程安全？ | 机制 |
|------|------------|------|
| `circular_q` 单独用 | 否 | 无锁 |
| `mpmc_blocking_queue` | 是（MPMC） | 一把 mutex + 两条件变量 |
| `base_sink` `_mt` | 是 | `lock_guard` |
| `base_sink` `_st` | 仅单线程 | `null_mutex` |
| `registry` | 是 | 独立 mutex |
| `logger` 级别 | 是 | `atomic<level>` |
| `logger` sink 列表 | 是 | COW + `atomic_load` 快照；变更时 mutex |
| 无锁 spsc/mpsc | 按协议 | lockfree 异步后端使用 MPSC（单 worker） |

### 7.3 锁粒度（易错点）

队列锁**只**保护入队/出队临界区；`wait_for` 等待时会释放锁。  
写文件 / pattern / Qt 投递在**锁外**进行，故多个工作线程可并行处理已取出的消息，但不能并行修改环上的 `head_`/`tail_`。

Sink 另有一把锁，避免多 worker 同时写同一文件；设计上先放队列锁再进 Sink，避免「持队列锁做 I/O」堵死全局。

### 7.4 调用方约定

1. 多线程共享 Sink → 用 `*_mt`；  
2. 异步前先 `init_thread_pool`（或 `init_lockfree_thread_pool`）；退出前 `shutdown()`；  
3. `async_logger` 必须由 `shared_ptr` 管理；  
4. 勿把裸 `circular_q` 给多线程；  
5. 默认热路径是**有锁** MPMC；无锁路径单 worker，且不要用 `overrun_oldest`。

### 7.5 关闭语义

`minispdlog::shutdown()`：先对已注册 logger `flush_all`，再停全局线程池（`terminate` × N + `join`），最后 `drop_all`。用过异步时，在 `main` 返回前调用。  
无全局 atexit；`overrun_oldest` / `discard_new` 丢掉的消息不会落盘。  
`async_logger::flush()` 向池投递 flush 并等待 ack（`async_msg::ack`），保证已入队记录写出。

---

## 8. 测试、基准与 CI

| 项 | 说明 |
|----|------|
| 框架 | doctest，单入口 `tests/test_main.cpp` → `minispdlog_tests` |
| 用例 | `tests/unit/`：level / sink / logger / registry / pattern / queue / async / utils / callback / daily / rotating / json / qt |
| 辅助 | `mock_sink` 内存断言；`test_fixture` 临时目录 RAII（析构用 `error_code`，避免 noexcept 里抛异常） |
| 运行 | `./build/tests/minispdlog_tests`；`ctest --test-dir build --output-on-failure` |
| 过滤 | `-tc='*json*'` 或标签；PowerShell 下 `[json]` 可能被当成通配符 |
| Benchmark | 需安装 Google Benchmark；目标 `minispdlog_bench_async` / `_queue` |
| CI | lint（clang-format 允许名单 + clang-tidy）· g++/clang × Release / RelASan / RelTSan · Windows MSVC · coverage |

队列微基准已控制 Iterations/消息量，避免默认统计跑数十分钟；多生产者无锁用例因实验队列风险已收紧。

---

## 9. 不同平台下的 Qt 使用方法

| 路径 | 说明 |
|------|------|
| `sinks/qt_sink.h` | GUI Sink（需 `MINISPDLOG_WITH_QT`） |
| `sinks/callback_sink.h` | 无 Qt 的业务回调（告警 / 计数 / 测试）；`callback_logger_*` / `async_callback_mt` |
| `examples/qt_log_viewer/` | 窗口示例 |
| `scripts/setup_qt.sh` / `setup_qt.ps1` | 一键装 Qt 到 `third_party/qt` |
| `tests/unit/test_qt_sink.cpp` | offscreen 单测 |

配置加 `-DMINISPDLOG_WITH_QT=ON`。成功日志含：`minispdlog: Qt x.x.x enabled for qt_sink`。

### 9.1 通用约定

1. SDK 建议在 `third_party/qt/`（已 gitignore，**不要提交 SDK**）；  
2. 编译器与 Qt kit 一致（MSVC ↔ msvc Qt，gcc ↔ `gcc_64`）；  
3. 出真窗口时**不要**设 `QT_QPA_PLATFORM=offscreen`；  
4. API 示例：

```cpp
#include "minispdlog/sinks/qt_sink.h"  // 需 MINISPDLOG_WITH_QT

auto logger = minispdlog::qt_logger_mt("gui", textEdit, "append");
// QPlainTextEdit → "appendPlainText"
logger->info("hello from qt_sink");
```

线程模型：任意线程打日志 → `QMetaObject::invokeMethod(..., Qt::AutoConnection)` → GUI 线程更新控件。

### 9.2 Windows（推荐看窗口）

```powershell
.\scripts\setup_qt.ps1
# 可选：.\scripts\setup_qt.ps1 -Arch win64_msvc2022_64

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMINISPDLOG_WITH_QT=ON
cmake --build build --config Release --target qt_log_viewer

.\build\examples\qt_log_viewer\Release\qt_log_viewer.exe
# 缺 DLL 时：把 third_party\qt\...\bin 加入 PATH
```

### 9.3 Linux

```bash
# A：项目内
./scripts/setup_qt.sh
# B：系统包
sudo apt install qt6-base-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMINISPDLOG_WITH_QT=ON
cmake --build build --target qt_log_viewer -j$(nproc)
./build/examples/qt_log_viewer/qt_log_viewer
```

无显示器时：`QT_QPA_PLATFORM=offscreen ./build/tests/minispdlog_tests -tc='*qt*'`。

### 9.4 WSL

可编译与 offscreen 单测；真窗口常因 EGL/MESA/WSLg 失败。可试软件渲染：

```bash
export QT_OPENGL=software LIBGL_ALWAYS_SOFTWARE=1
./build/examples/qt_log_viewer/qt_log_viewer
```

仍失败时，请用 **§9.2 Windows 原生** 跑窗口。

### 9.5 macOS（简要）

```bash
brew install qt   # 或 aqt 装到 third_party/qt
cmake -S . -B build -DMINISPDLOG_WITH_QT=ON -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --target qt_log_viewer
```

### 9.6 单测对照

| 目标 | 命令 |
|------|------|
| 无 Qt 的投递逻辑 | `./minispdlog_tests -tc='*callback*'` |
| 有 Qt、无窗口 | `QT_QPA_PLATFORM=offscreen ./minispdlog_tests -tc='*qt*'` |
| 看窗口 | 运行 `qt_log_viewer` |

---

## 10. 构建、依赖与推送约定

### 10.1 基础构建（无 Qt）

```bash
git submodule update --init --recursive   # fmt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/tests/minispdlog_tests
```

### 10.2 Qt 一键脚本

| 平台 | 命令 |
|------|------|
| Linux/WSL | `./scripts/setup_qt.sh` |
| Windows | `.\scripts\setup_qt.ps1` |

只需在仓库根目录终端执行（需 Python）。装完再 `-DMINISPDLOG_WITH_QT=ON`。

### 10.3 推送到 Git 时推什么？

| 应提交 | 不应提交 |
|--------|----------|
| 源码、CMake、脚本、`third_party/qt/README.md`、doctest 框架、`third_party/fmt`（子模块） | **`third_party/qt/` 下下载的完整 SDK**（体积大，已 ignore） |
| 测试与 example 源码 | 本机 `build/`、`logs/`、JVM `hs_err_pid*` / `replay_pid*`、fmt 的 `.gradle/` |

其他人克隆后自行跑安装脚本或使用系统 Qt 即可。

---

## 11. 设计决策（ADR）

### ADR-001：异步默认 MPMC 阻塞队列，可选无锁 MPSC

默认 `mpmc_blocking_queue` + `circular_q`（多 worker、block/overrun 清晰）。  
低延迟场景可用 `init_lockfree_thread_pool`（Vyukov MPSC，单 worker + `atomic.wait`）。

### ADR-002：`base_sink<Mutex>` + `null_mutex`

一套 Sink 覆盖 MT/ST，避免复制两套实现。

### ADR-003：异步深拷贝 payload + `shared_ptr<logger>`

跨线程安全传递内容与「用哪个 logger 输出」，并延长生命周期。

### ADR-004：Qt 可选、SDK 外置到 `third_party/qt`

核心库默认不链 Qt；需要 GUI 时再开 CMake 选项；SDK 不进版本库。

### ADR-005：结构化日志是 formatter，不是第二套 logger

`json_formatter` 可挂到任意 sink；生产用的 `json_*` sink 默认装上它并锁住 `set_pattern` / `set_formatter`。滚动/按天仍要 JSON 时用 `json_rotating_file_sink` / `json_daily_file_sink`，不要只给文本 rolling sink 换 formatter（随后 `set_pattern` 会改回纯文本）。

### ADR-006：着色用偏移区间，不把 ANSI 写进 payload

`%^`/`%$` 写入 `log_msg::color_range_*`；只有 color sink 消费。文件与 JSON 保持纯文本/纯 JSON。

### ADR-007：logger sink 列表 copy-on-write

热路径只读快照，避免持 logger 锁做 I/O；与「Sink 自己一把锁」分层。

### ADR-008：批量落盘用双缓冲 + WAL 偏移，而不是逐条 fwrite

`buffered_file_sink`（`json_file_sink` 继承它）在内存里攒一批再一次写入。WAL 带 `target_offset`，崩溃后按主文件大小决定补写或去重，再 salvage 半截行。front buffer 仍可能丢，需要 `flush`/`shutdown`/`install_crash_flush`；这比假装「每条都已落盘」更诚实。

---

## 12. 建议阅读顺序

1. `minispdlog.h` — 用户 API、工厂与编译期宏  
2. `logger.h` / `logger.cpp` — 同步路径、`sourced_fmt`、COW sinks  
3. `sinks/base_sink.h`、`console_sink.h`、`color_console_sink.h` — Sink 与 `%^`/`%$` 着色  
4. `pattern_formatter.*`、`json_formatter.*` — 两种行格式  
5. `details/log_msg.h`、`async_msg.h` — 消息模型（含 `source_loc` / color_range）  
6. `async.h` → `thread_pool.*` → `mpmc_blocking_q.h` / `mpsc_queue.h` — 异步与并发  
7. `registry.*` — 全局生命周期与 `shutdown()`  
8. `sinks/buffered_file_sink.h`、`details/durable_file.*`、`json_sink.h`、`daily_file_sink.h`、`rotating_file_sink.h` — 落盘、批量与找回  
9. `callback_sink.h`、`examples/callback_log` — 回调旁路；`qt_sink.h`、`examples/qt_log_viewer` — GUI（可选，见 §9）  
10. `tests/unit/test_logger.cpp`、`test_json.cpp`、`test_buffered_file.cpp`、`mock_sink.h` — 用测试反推行为  

按此顺序阅读，即可系统掌握本项目的设计意图与技术栈落地方式。
