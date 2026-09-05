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
| 同步日志 | 调用线程完成「用户内容格式化 → pattern 拼行 → 写 Sink」 |
| 异步日志 | 业务线程只入队；工作线程池消费并写 I/O，业务与磁盘解耦 |
| 多 Sink | 一个 logger 可同时写控制台、文件、滚动文件、回调、可选 Qt 控件等 |
| 可扩展 | 虚接口 `sink` / `formatter`，按需增加输出目标与行格式 |
| 工程化 | Registry 管理生命周期、doctest 单测、可选 Benchmark、ASan/TSan CI |

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
| 语言标准 | **C++20** | `CMakelists.txt` 强制；使用 `if constexpr`、`string_view`、`format_string` 等 |
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
| CI | GitHub Actions | Release / RelASan / RelTSan + ctest |
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
│    全局 info/warn、stdout_color_mt、async_file_mt、MINISPDLOG_* 宏         │
├─────────────────────────────────────────────────────────────────────────┤
│ ② 管理调度层                                                              │
│    registry · async_logger · thread_pool · 队列（blocking / lockfree）     │
│    「注册表 + 异步投递」；不负责拼最终输出行                               │
├─────────────────────────────────────────────────────────────────────────┤
│ ③ 格式化 / 输出层                                                         │
│    logger · sink/base_sink · 各具体 Sink · pattern_formatter               │
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
| ② 管理调度 | 注册/查找 logger；异步入队与 worker 调度；溢出策略 | pattern 拼行、具体 I/O | `registry.*`、`async.h`、`thread_pool.*`、`mpmc_blocking_q.h`、`mpsc_queue.h` |
| ③ 格式化/输出 | `should_log`、fmt、遍历 Sink、formatter | 全局命名表、线程池生命周期 | `logger.*`、`sinks/*`、`pattern_formatter.*` |
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
         → logger::log          【③】级别过滤 + fmt 用户串 → 造 log_msg【④】
         → logger::sink_it_     【③】
         → Sink::log → pattern  【③】写出
```

**异步**（`async_logger` + 已 `init_thread_pool` / `init_lockfree_thread_pool`）：

```
info("x") → logger::log                 【③】过滤 + fmt（仍在业务线程）
         → async_logger::sink_it_       【②】打包 async_msg，入队
         → thread_pool worker           【②】出队
         → logger::backend_sink_it_     【③】写 Sink（不再入队）
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
     │  name_ / level_ / sinks_[]  │
     └────────────┬────────────────┘
                  │ sink_ptr
     ┌────────────┼────────────┐
     ▼            ▼            ▼
  console_*    file_*     callback / qt_* …   【③】
     └──── base_sink<Mutex> ──► formatter ────┘

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
| `include/minispdlog/lockfree_queue.h` | ②/对外：无锁队列便捷入口 |
| `src/` | 上述实现，链成静态库 `minispdlog` |
| `tests/unit/`、`tests/framework/` | 按组件测各层行为 |
| `examples/`、`scripts/`、`cmake/` | 示例与可选 Qt 工程化 |
| `docs/roadmap_and_testing_framework.md` | 迭代路线图 |

---

## 4. 核心设计：消息、同步与异步

### 4.1 两种消息结构（关系，不是平行两套）

| 类型 | 文件 | 角色 |
|------|------|------|
| `log_msg` | `details/log_msg.h` | **同步与异步共用**的日志内容载体：级别、时间、线程 id、logger 名、payload 等 |
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
| ② pattern 整行 | Sink 写出时 | 加上时间、级别等 → `[2026-…] [info] User Alice…` |

异步入队时：① 已完成，且已采样 `time` / `thread_id` / `lvl`；② 在工作线程的 Sink 上做。  
这样时间戳反映「调用点」，且避免把临时参数跨线程传递。

### 4.3 同步调用链

```
logger->info("User {} logged in", "Alice")
  → logger::log（logger.h）
       should_log → fmt::format_to → 构造 log_msg → sink_it_
  → logger::sink_it_（logger.cpp）：遍历 sinks_
  → base_sink::log：lock_guard → 子类 sink_it_
  → 例如 console_sink：format_message → cout.write
  → pattern_formatter::format
```

### 4.4 异步调用链

```
业务线程：同上 logger::log（得到 log_msg）
  → async_logger::sink_it_（async.h）
  → post_log / post_log_nowait(shared_from_this(), msg)
  → async_msg 入队（payload 深拷贝 + worker_ptr）
工作线程：dequeue_for（仅短暂持队列锁）
  → 解锁后 worker_ptr->sink_it_ → 与同步相同的 Sink/pattern/I/O
thread_pool 析构：按线程数入队 terminate → join（尽量 drain 队头日志）
```

使用前需：

```cpp
minispdlog::init_thread_pool(/*queue_size*/ 8192, /*threads*/ 1);
auto lg = minispdlog::async_file_mt("async", "async.log", false);
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
- 对应策略枚举 `async_overflow_policy`：`block` / `overrun_oldest`。

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
| `color_console_sink` / `color_stderr_sink` | `color_console_sink.h` | ANSI 按级别着色 |
| `file_sink` | `file_sink.h` | 普通文件 |
| `rotating_file_sink` | `rotating_file_sink.h` | 按大小滚动 |
| `callback_sink` | `callback_sink.h` | 回调投递（测 GUI 路径、自定义处理） |
| `qt_sink`（可选） | `qt_sink.h` | 写入 `QTextEdit` / `QPlainTextEdit` |
| `mock_sink`（测试） | `tests/framework/mock_sink.h` | 内存捕获断言 |

每类通常有 `_mt`（`std::mutex`）与 `_st`（`null_mutex`）。

### 5.2 插件扩展方式

1. 继承 `base_sink<Mutex>`，实现 `sink_it_` / `flush_`；  
2. 或实现虚接口 `formatter`，或配置 `pattern_formatter` 的 pattern（如 `%Y %m %d %H %M %S %l %n %v %t`）。

路线图中还可扩展：按天/小时滚动、JSON、syslog、网络 Sink 等（见 `docs/roadmap_and_testing_framework.md`）。

### 5.3 颜色如何实现

- **不是**写在 `log_msg` 里带颜色字段；  
- 仅在 `color_*_sink` 写出时临时加 ANSI 前缀/后缀（`color_console_sink.h` 中 `color::green` 等）；  
- 文件 Sink 一般为纯文本。

### 5.4 Registry

`registry` 单例用 `unordered_map<string, shared_ptr<logger>>` 管理命名 logger；工厂函数创建后 `register_logger`。  
全局 `minispdlog::info` 走 `default_logger()`。

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

- `if constexpr` + `MINISPDLOG_ACTIVE_LEVEL`：Release 可剥掉低级别日志；  
- `enum class`：级别、消息类型、溢出策略。

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
| 无锁 spsc/mpsc | 按协议 | lockfree 异步后端使用 MPSC（单 worker） |

### 7.3 锁粒度（易错点）

队列锁**只**保护入队/出队临界区；`wait_for` 等待时会释放锁。  
写文件 / pattern / Qt 投递在**锁外**进行，故多个工作线程可并行处理已取出的消息，但不能并行修改环上的 `head_`/`tail_`。

Sink 另有一把锁，避免多 worker 同时写同一文件；设计上先放队列锁再进 Sink，避免「持队列锁做 I/O」堵死全局。

### 7.4 调用方约定

1. 多线程共享 Sink → 用 `*_mt`；  
2. 异步前先 `init_thread_pool`；  
3. `async_logger` 必须由 `shared_ptr` 管理；  
4. 勿把裸 `circular_q` 给多线程；  
5. 热路径是**有锁** MPMC，不是无锁。

### 7.5 关闭语义

池析构：`terminate` × N 入队 + `join`，队头积压日志会尽量先消费（`block` 策略下）。  
无全局 atexit；`overrun_oldest` 覆盖的消息不会落盘。

---

## 8. 测试、基准与 CI

| 项 | 说明 |
|----|------|
| 框架 | doctest，单入口 `tests/test_main.cpp` → `minispdlog_tests` |
| 辅助 | `mock_sink` 内存断言；`test_fixture` 临时目录 RAII |
| 运行 | `./build/tests/minispdlog_tests`；`ctest --test-dir build/tests` |
| 过滤 | `./minispdlog_tests -tc='*async*'` 或标签查询 |
| Benchmark | 需安装 Google Benchmark；目标 `minispdlog_bench_async` / `_queue` |
| CI | g++/clang × Release / RelASan / RelTSan |

队列微基准已控制 Iterations/消息量，避免默认统计跑数十分钟；多生产者无锁用例因实验队列风险已收紧。

---

## 9. 不同平台下的 Qt 使用方法

| 路径 | 说明 |
|------|------|
| `sinks/qt_sink.h` | GUI Sink（需 `MINISPDLOG_WITH_QT`） |
| `sinks/callback_sink.h` | 无 Qt 回调投递 |
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
| 测试与 example 源码 | 本机 `build/` 目录 |

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

---

## 12. 建议阅读顺序

1. `minispdlog.h` — 用户 API 与编译期宏  
2. `logger.h` / `logger.cpp` — 同步路径  
3. `sinks/base_sink.h`、`console_sink.h`、`color_console_sink.h` — Sink 与 ANSI 颜色  
4. `pattern_formatter.*` — 行格式化  
5. `details/log_msg.h`、`async_msg.h` — 消息模型  
6. `async.h` → `thread_pool.*` → `mpmc_blocking_q.h` / `mpsc_queue.h` — 异步与并发（含可选无锁）  
7. `registry.*` — 全局生命周期  
8. `callback_sink.h` / `qt_sink.h`、`examples/qt_log_viewer` — GUI（可选，见 §9）  
9. `tests/unit/test_logger.cpp`、`mock_sink.h` — 用测试反推行为  
10. `docs/roadmap_and_testing_framework.md` — 后续迭代  

按此顺序阅读，即可系统掌握本项目的设计意图与技术栈落地方式。
