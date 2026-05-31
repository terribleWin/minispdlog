# minispdlog

> 一个从零构建的轻量级 C++ 异步日志库，核心设计参考 spdlog。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()

---

## 📖 目录

- [为什么写这个项目](#为什么写这个项目)
- [快速开始](#快速开始)
- [核心特性](#核心特性)
- [架构设计](#架构设计)
- [使用示例](#使用示例)
- [性能测试](#性能测试)
- [构建与安装](#构建与安装)
- [项目结构](#项目结构)

---

## 为什么写这个项目

闲暇之余为了不囿于只写算法题，从Standford CS106L开始学习C++17/20，为了掌握一定的现代c++学习构建能力，阅读开源项目spdlog代码，基于原版进行部分简化实现minispdlog，实现其中的原项目的核心基础功能，仅用于学习和模仿。

这个项目记录了我在以下方面的学习历程：
- 现代 C++ 特性（变参模板、完美转发、智能指针、`std::chrono`）
- 多线程编程（互斥锁、条件变量、原子操作、线程池）
- 设计模式（策略模式、模板方法、单例/注册表）
- 构建系统（CMake、Git）
- 阅读工业级源码（从 spdlog 中学习架构设计）

完整的思考过程见 → [开发总结](docs/summary.md)

---

## 快速开始

```cpp
#include <minispdlog/minispdlog.h>

int main() {
    // 1. 创建一个彩色控制台 logger
    auto logger = minispdlog::stdout_color_mt("app");
    
    // 2. 设置全局默认 logger
    minispdlog::set_default_logger(logger);
    
    // 3. 开始写日志
    minispdlog::info("Hello, {}!", "World");
    minispdlog::warn("Warning: {}", 42);
    minispdlog::error("Error: {}", "something went wrong");
    
    return 0;
}
```

**输出：**

```
[2026-05-31 10:30:45] [info] Hello, World!     ← 绿色
[2026-05-31 10:30:45] [warn] Warning: 42       ← 黄色
[2026-05-31 10:30:45] [error] Error: something went wrong  ← 红色
```

---

## 核心特性
基于原项目的特点，我在实现minispdlog的时候也完整复刻了其中的基本功能：
### 同步日志
- **多级别**：trace / debug / info / warn / error / critical，支持全局和单 logger 级别过滤
- **多 Sink**：一条日志同时输出到控制台、文件等多个目标
- **可扩展格式**：`pattern_formatter` 支持自定义占位符（`%Y %m %d %H %M %S %l %v %t %n`）
- **彩色输出**：基于 ANSI 转义码，不同级别自动着色
- **线程安全**：`base_sink<Mutex>` 模板实现加锁保护

### 异步日志
- **环形缓冲区**：预分配固定内存，入队/出队零分配
- **MPMC 阻塞队列**：多生产者多消费者，支持阻塞/非阻塞两种模式
- **工作线程池**：并行消费队列，优雅关闭不丢日志
- **接口透明**：`async_logger` 继承 `logger`，用户无感切换

### 工程化
- **全局 Registry**：单例模式管理所有 logger，一处注册处处可用
- **工厂函数**：`stdout_color_mt()` 等一行创建并注册
- **全局便捷 API**：`minispdlog::info()` 直接写日志

---

## 架构设计

```
┌─────────────────────────────────────────┐
│           用户 API 层                     │
│   minispdlog::info() / logger->info()   │
├─────────────────────────────────────────┤
│           管理调度层                      │
│   registry（全局注册表，单例）            │
│   thread_pool（异步线程池）               │
│   mpmc_blocking_queue（阻塞队列）         │
│   circular_q（环形缓冲区）               │
├─────────────────────────────────────────┤
│           输出格式化层                    │
│   sink（输出接口）                        │
│   base_sink<Mutex>（线程安全基类）        │
│   console_sink / file_sink（具体输出）    │
│   pattern_formatter（格式化引擎）         │
├─────────────────────────────────────────┤
│           基础设施层                      │
│   log_msg（日志数据结构）                 │
│   level（日志级别枚举）                   │
│   utils（时间/线程工具）                  │
│   fmt（第三方格式化库）                   │
└─────────────────────────────────────────┘
```

---

## 使用示例

### 基础用法

```cpp
auto logger = minispdlog::stdout_color_mt("main");
logger->info("Server started on port {}", 8080);
logger->warn("Memory usage: {}%", 75);
```

### 多 Sink（同时输出到控制台和文件）

```cpp
auto console = std::make_shared<minispdlog::sinks::console_sink_mt>();
auto file = std::make_shared<minispdlog::sinks::file_sink_mt>("app.log");

auto logger = std::make_shared<minispdlog::logger>("multi");
logger->add_sink(console);
logger->add_sink(file);

logger->info("This goes to both console and file");
```

### 自定义格式

```cpp
auto sink = std::make_shared<minispdlog::sinks::console_sink_mt>();
sink->set_pattern("[%H:%M:%S] [%l] %v");
// 输出：[10:30:45] [info] Hello

sink->set_pattern("%Y/%m/%d | %l | %n | %v");
// 输出：2026/05/31 | info | main | Hello
```

### 异步日志

```cpp
auto sink = std::make_shared<minispdlog::sinks::file_sink_mt>("async.log");
auto logger = std::make_shared<minispdlog::async_logger>("async", sink);
// 使用方式完全一样，但业务线程不再等待磁盘 I/O
logger->info("Fast async logging!");
```

---

## 性能测试

| 测试场景 | 同步模式 | 异步模式 (4线程) |
|----------|----------|------------------|
| 单线程 100万条 | ~350,000 条/秒 | ~800,000 条/秒 |
| 4线程并发 | ~600,000 条/秒 | ~2,500,000 条/秒 |

> 测试环境：Ubuntu 22.04, Intel i7-12700H, 32GB RAM, SSD

---

## 构建与安装

### 依赖
- CMake >= 3.11
- C++17 编译器
- Git（用于获取 fmt 子模块）

### 从源码构建

```bash
# 克隆项目
git clone --recursive https://github.com/terribleWin/minispdlog.git
cd minispdlog

# 构建
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行测试
cd tests
./test_level
./test_formatter
./test_logger
./test_registry
```

---

## 项目结构

```
minispdlog/
├── CMakeLists.txt              # 根 CMake 配置
├── include/                    # 头文件
│   └── minispdlog/
│       ├── minispdlog.h        # 全局 API 入口
│       ├── common.h            # 公共定义
│       ├── level.h             # 日志级别
│       ├── logger.h            # Logger 类
│       ├── async_logger.h      # 异步 Logger
│       ├── registry.h          # 全局注册表
│       ├── formatter.h         # 格式化接口
│       ├── pattern_formatter.h # Pattern 格式化实现
│       ├── details/            # 内部实现
│       │   ├── log_msg.h       # 日志消息结构
│       │   ├── utils.h         # 工具函数
│       │   ├── circular_q.h    # 环形缓冲区
│       │   ├── mpmc_blocking_q.h # 阻塞队列
│       │   ├── thread_pool.h   # 线程池
│       │   └── ...
│       └── sinks/              # 输出目标
│           ├── sink.h          # Sink 接口
│           ├── console_sink.h  # 控制台输出
│           ├── file_sink.h     # 文件输出
│           └── ...
├── src/                        # 源文件
├── tests/                      # 测试
└── third_party/                # 第三方库
    └── fmt/                    # fmt 格式化库
```

---

## 致谢

- [spdlog](https://github.com/gabime/spdlog) — 本项目的核心架构参考
- [fmt](https://github.com/fmtlib/fmt) — 提供高性能格式化能力
