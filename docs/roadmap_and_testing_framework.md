# minispdlog 迭代更新路线图 & 工业化测试框架设计

> [!IMPORTANT]
> **历史归档，不是当前执行计划。** 本文保留早期问题诊断、8 周路线与测试框架推演，便于追溯设计背景；其中路径、待办、样例配置和能力判断可能已经过时。当前使用方式以[文档中心](README.md)、[测试指南](testing.md)和仓库代码为准，请勿从本文直接复制构建配置到生产工程。

> 文档版本：v1.1（对照当前仓库修订）  
> 原文：2026-07-11 规划稿；下文 **§0** 是落地状态，后面 8 周计划保留为历史设计，不要再当成「尚未开工」。

---

## 0. 当前落地状态（以代码为准）

规划里已经做完、文档应视为**现有架构**的部分：

| 规划项 | 现状 |
|--------|------|
| doctest 单入口 + mock_sink + CTest | `tests/unit/` + `minispdlog_tests` |
| RelASan / RelTSan / coverage / ASan·TSan CI | 根 `CMakeLists.txt`、`.github/workflows/ci.yml` |
| `daily_file_sink` + `max_files` | `daily_logger_mt/st` |
| `rotating_file_sink`（按大小 + max_files） | `rotating_logger_mt` |
| JSON Lines | `json_formatter` + `json_file/console/stderr/rotating/daily` + `json_logger_*` / `rotating_json_*` / `daily_json_*` / `async_json_file_mt` / `async_json_rotating_mt` |
| 批量写 / 双缓冲 / WAL | `buffered_file_sink` + `json_file_sink` + `buffered_logger_*` / `async_buffered_file_mt`；`install_crash_flush` |
| 回调旁路 | `callback_sink` + `callback_logger_*` / `async_callback_mt` |
| `qt_sink` + 示例 + offscreen 单测 | `-DMINISPDLOG_WITH_QT=ON` |
| 无锁 MPSC 接入异步 | `init_lockfree_thread_pool`（单 worker；禁止 `overrun_oldest`） |
| 优雅关闭 | `minispdlog::shutdown()` |
| clang-format / clang-tidy CI | `scripts/lint.sh`；format 覆盖项目自有 C++ 源码并排除第三方目录 |

**仍未做**（规划后半仍有效）：hourly sink、backtrace、syslog、android/windebug、object pool、完全 signal-safe 的崩溃日志（当前 `install_crash_flush` 是尽力而为，不是 async-signal-safe）等。

权威架构说明见仓库根目录 [`explanation.md`](../explanation.md) 与 [`README.md`](../README.md)。

---

## 目录

1. [项目现状诊断](#一项目现状诊断)
2. [迭代更新路线图（8周计划）](#二迭代更新路线图8周计划)
3. [测试框架设计总览](#三测试框架设计总览)
4. [测试框架详细设计](#四测试框架详细设计)
5. [落地实施步骤](#五落地实施步骤)
6. [附录：测试选型依据](#六附录测试选型依据)

---

## 一、项目现状诊断

### 1.1 现有架构

minispdlog 目前已实现的核心模块：

| 层级 | 组件 | 状态 | 成熟度 |
|------|------|------|--------|
| 用户 API | `minispdlog::info()`、`sourced_fmt`、工厂函数 | ✅ 可用 | 中 |
| 管理调度 | `registry` + `shutdown()` | ✅ 可用 | 中 |
| 管理调度 | `async_logger` + `thread_pool`（blocking / lockfree） | ✅ 可用 | 中 |
| 管理调度 | `mpmc_blocking_queue`；溢出 `block` / `overrun_oldest` / `discard_new` | ✅ 可用 | 中 |
| 管理调度 | `mpsc_queue` / `spsc_queue`（无锁） | ✅ 可用 | 中（已接入 lockfree 池） |
| 输出格式化 | `sink` / `base_sink<Mutex>` | ✅ 可用 | 中 |
| 输出格式化 | `console` / `color_*`（`%^`/`%$` 区间着色） | ✅ 可用 | 中 |
| 输出格式化 | `file` / `rotating` / `daily` / `json_*` / `callback` / 可选 `qt` | ✅ 可用 | 中 |
| 输出格式化 | `pattern_formatter` / `json_formatter` | ✅ 可用 | 中 |
| 基础设施 | `level` / `log_msg`（含 `source_loc`、`color_range_*`）/ `utils` | ✅ 可用 | 高 |
| 基础设施 | 编译时级别控制（宏路径） | ✅ 可用 | 中 |
| 第三方 | `fmt` 格式化库 | ✅ 集成 | — |

### 1.2 现有测试问题清单

**当前测试代码（`tests/` 目录）存在以下系统性缺陷：**

> **v1.1 注**：P1–P3、P7、P9 以及 P5/P6 的 CI 形态已落地（见 §0）。下表保留为当时诊断，便于对照「为什么要上 doctest」。

| 问题编号 | 问题描述 | 严重程度 | 影响 |
|----------|----------|----------|------|
| P1 | **无断言框架**：所有测试靠 `std::cout` 输出 + 人眼判断，没有 `REQUIRE`/`CHECK` 等自动化断言 | 🔴 严重 | 无法 CI 自动化 |
| P2 | **无测试自动发现**：每个测试文件是独立 `main()` 可执行文件，无法统一运行 | 🔴 严重 | 维护成本高 |
| P3 | **无 CTest 集成**：CMake 没有 `enable_testing()`，无法 `ctest` 一键运行 | 🔴 严重 | 无法接入 CI/CD |
| P4 | **测试代码冗余**：每个测试文件重复包含 `system("mkdir -p logs")`、重复构造 sink/logger | 🟡 中等 | 违反 DRY |
| P5 | **无内存/线程检测**：没有 ASan/TSan/Valgrind 集成，内存泄漏和 data race 无法自动发现 | 🟡 中等 | 可靠性隐患 |
| P6 | **无代码覆盖率**：没有 gcov/lcov 集成，无法量化测试完整性 | 🟡 中等 | 质量不可测 |
| P7 | **无 Mock 设施**：测试 sink 只能写真实文件/控制台，无法捕获输出做断言 | 🟡 中等 | 测试边界受限 |
| P8 | **无性能回归测试**：benchmark 数据是手写的，没有历史对比和回归报警 | 🟢 轻微 | 性能退化难发现 |
| P9 | **无多构建变体**：仅 Release/Debug，没有 RelASan/RelTSan 等变体 | 🟢 轻微 | 调试手段单一 |

---

## 二、迭代更新路线图（8周计划）

> **核心理念**：每两周为一个阶段，每个阶段聚焦一个主题，遵循 "先打地基（测试）→ 再盖房子（功能）→ 最后装修（工程化）" 的顺序。

---

### 🔧 第一阶段：测试地基（Week 1-2）

**主题**：引入工业化测试框架，覆盖现有全部功能

**为什么先改进测试**：
- 测试是代码的"安全防护网"。当前项目功能迭代越快，没有测试保护，引入 regression 的风险越高
- 测试先行（TDD）能倒逼 API 设计更合理。很多接口在写测试时会发现边界情况考虑不足
- 测试覆盖率是工程化项目的名片，直接影响后续开源社区贡献者的信心

**具体任务**：

| 任务 | 内容 | 交付物 |
|------|------|--------|
| W1-1 | 引入 doctest（header-only 测试框架），建立 `tests/framework/` 目录 | `tests/framework/doctest.h` |
| W1-2 | 编写 `mock_sink`（内存捕获型测试替身），解决测试断言难题 | `tests/framework/mock_sink.h` |
| W1-3 | 重写 `test_level.cpp`、`test_formatter.cpp` 为 doctest 风格 | `tests/unit/test_level.cpp` |
| W1-4 | 重写 `test_sink.cpp`、`test_logger.cpp` 为 doctest 风格 | `tests/unit/test_sink.cpp` |
| W1-5 | 重写 `test_registry.cpp`、`test_thread_pool.cpp` 为 doctest 风格 | `tests/unit/test_registry.cpp` |
| W2-1 | 统一 CMake 测试配置：单测试入口 + CTest 集成 | `tests/CMakeLists.txt` 重构 |
| W2-2 | 添加 `RelASan`/`RelTSan`/`DebugASan` 等多构建变体 | 根 `CMakeLists.txt` 修改 |
| W2-3 | 添加代码覆盖率目标（gcov/lcov） | `cmake --build build --target coverage` |
| W2-4 | 删除旧测试文件，所有测试通过新框架运行 | 清理旧 `tests/*.cpp` |

**本阶段优势**：
- doctest 是 **header-only** 的，不需要额外编译测试框架库，适合本项目的单入口测试模型
- 单测试入口可执行文件编译一次、运行所有测试，比当前 10+ 独立可执行文件编译效率更高
- CTest 集成后，`ctest` 一键运行，天然支持 CI/CD（GitHub Actions 可直接使用）
- ASan/TSan 变体能在不修改代码的情况下发现 90% 的内存错误和 data race

---

### 🚀 第二阶段：核心功能完善（Week 3-4）

**主题**：以生产日志场景为基准，补齐项目自身的关键能力

**为什么在这个阶段加功能**：
- 测试框架已经就位，新功能可以"带着测试一起写"，避免技术债务累积
- 以下功能缺失会直接影响日志库的实用性：没有 daily 轮转，日志文件会无限膨胀；没有 backtrace，线上故障定位困难；没有 JSON 格式，无法对接现代可观测系统

**具体任务**：

| 任务 | 内容 | 为什么需要 | 验收重点 |
|------|------|-----------|----------|
| W3-1 | `daily_rotating_file_sink`：按天自动切分日志 | 日志文件无限增长会撑满磁盘，是生产环境必选项 | 按日轮转实践 |
| W3-2 | `hourly_rotating_file_sink`：按小时切分（可选） | 高并发场景下单日文件仍可能过大 | 扩展能力 |
| W3-3 | `backtrace`：在 error/critical 时输出最近 N 条调用栈 | 线上故障定位的第一线索，没有调用栈的日志价值减半 | 环形诊断缓存 |
| W3-4 | `json_sink` / JSON formatter：输出结构化日志 | 对接 ELK/Loki/Grafana 等现代可观测系统，纯文本日志已过时 | 结构化日志生态 |
| W4-1 | `syslog_sink`：输出到系统 syslog | Unix/Linux 服务器环境的标准做法 | POSIX syslog |
| W4-2 | `qt_sink`（已在项目中声明）完成实现与测试 | 已有的声明未完成，Qt 是 C++ GUI 开发的主流框架 | Qt 消息系统集成 |
| W4-3 | `android_sink` / `windebug_sink`：平台专属 sink | 移动端和 Windows 调试环境的刚需 | 平台调试接口 |
| W4-4 | 日志文件大小限制（max_size）与自动清理（max_files） | 避免磁盘空间耗尽，运维友好 | logrotate 行为 |

**本阶段优势**：
- daily_rotating + max_files 组合后，日志系统可以**无人值守运行数月**，这是生产环境的基本要求
- 结构化日志（JSON）是云原生时代的通用语言，直接决定了该日志库能否进入企业技术栈
- backtrace 虽然实现复杂（需要平台适配），但它是区分"玩具日志库"和"工业日志库"的关键分水岭

---

### ⚡ 第三阶段：性能与可靠性（Week 5-6）

**主题**：从"功能可用"到"性能卓越、运行稳定"

**为什么需要性能优化**：
- 早期单机测量约为 2.5M 条/秒，但消息大小、Sink、并发数和计时边界尚未形成可复现基线，不能据此下通用性能结论
- 无锁队列（mpsc_queue）目前在测试中并未真正替代 mpmc_blocking_queue，说明稳定性和性能收益还需要验证
- 内存分配是日志系统的隐形杀手，高频日志场景下每次 new/delete 都会引入不可预测的延迟尖峰

**具体任务**：

| 任务 | 内容 | 为什么需要 | 预期收益 |
|------|------|-----------|----------|
| W5-1 | 用 `mpsc_queue`（无锁）替代 `mpmc_blocking_queue` 在异步路径中的使用 | 消除锁竞争和条件变量开销，提升并发吞吐 | 吞吐提升 30-50% |
| W5-2 | 引入 `memory_pool` / `object_pool`：预分配 `log_msg` 对象 | 避免每次日志都动态分配内存，消除 latency spike | P99 延迟降低 |
| W5-3 | 零拷贝优化：`string_view` 贯穿日志路径，减少 `std::string` 构造 | 高频日志下字符串拷贝是主要 CPU 消耗 | 吞吐量提升 |
| W5-4 | 批量 flush（buffered write）：累积 N 条或 M 毫秒后统一写入磁盘 | 减少 syscall 次数，磁盘 I/O 更高效 | 磁盘吞吐翻倍 |
| W6-1 | 完善优雅关闭（drain on shutdown）：确保进程退出前所有日志落盘 | 避免日志丢失，是运维基本要求 | 可靠性 |
| W6-2 | 信号安全处理：SIGSEGV/SIGABRT 时也能输出日志到文件 | 崩溃时保留最后的现场信息 | 故障定位 |
| W6-3 | 内存泄漏检测：在 CI 中跑 ASan 变成强制项 | 内存安全是 C++ 项目的底线 | 质量保障 |
| W6-4 | Data race 检测：在 CI 中跑 TSan 变成强制项 | 并发日志库最隐蔽的 bug 类型 | 质量保障 |

**本阶段优势**：
- 无锁队列、内存复用与批量写入分别针对竞争、分配和系统调用成本，必须用拆分基准验证各自收益
- ASan/TSan 在 CI 中强制运行，相当于给每次提交上了"双保险"，这是工业项目的硬性门槛

---

### 🏗️ 第四阶段：工程化与生态（Week 7-8）

**主题**：从"代码库"到"可交付产品"

**为什么需要工程化**：
- 一个库的价值不仅在于代码，还在于**易用性**：用户能不能一行代码配置？能不能热加载配置？
- 文档和示例是开源项目的门面，直接影响 Stars 和社区参与度

**具体任务**：

| 任务 | 内容 | 为什么需要 |
|------|------|-----------|
| W7-1 | 配置文件支持：YAML/JSON 格式声明式配置 logger | 不用写代码就能配置日志，运维友好 |
| W7-2 | 配置热加载：运行时修改配置自动生效 | 线上调整日志级别无需重启进程 |
| W7-3 | `minispdlog::cfg::load_from_file()` 等配置 API | 形成项目自有的配置命名空间与 schema |
| W7-4 | 完善的 Doxygen 文档：每个 public API 都有文档注释 | 开发者体验 |
| W8-1 | 丰富示例代码：10+ 个场景示例（多线程、异步、多 sink、配置等） | 降低上手门槛 |
| W8-2 | 性能基准的持续追踪：每次 PR 自动跑 benchmark，对比基线 | 防止性能回归 |
| W8-3 | 打包发布：支持 vcpkg / conan 包管理器 | 现代 C++ 生态的标准做法 |
| W8-4 | 完善 README：架构图、quickstart、benchmark、贡献指南 | 开源项目的门面 |

**本阶段优势**：
- 配置热加载是区分"demo 级项目"和"产品级项目"的关键标志
- vcpkg/conan 支持意味着用户可以用 `vcpkg install minispdlog` 一键使用，极大降低采用门槛

---

## 三、测试框架设计总览

### 3.1 设计目标

> **一句话**：围绕 minispdlog 的并发、格式化、耐久性和平台边界，建立一套**单入口、CMake 深度集成、可过滤、可消毒、可覆盖率统计**的测试体系。

### 3.2 核心设计决策

| 决策点 | 选择 | 理由 |
|--------|------|------|
| 底层测试框架 | **doctest**（header-only） | 单头文件、启动快，支持标签过滤和清晰断言，能够随仓库直接构建 |
| 构建系统 | **CMake + CTest** | 与主构建系统一致，跨平台，并能按组件注册测试 |
| 测试组织 | **单入口可执行文件** + 组件标签 | 编译一次运行全部，比当前 10+ 独立可执行文件效率更高；用 `[sink]`/`[logger]` 标签可按组件过滤 |
| 辅助设施 | **自研 mock_sink + test_fixture** | 日志库的特殊需求：需要捕获内存中的日志输出做断言，通用的测试框架不提供这个 |
| 构建变体 | **Debug/Release/RelASan/RelTSan/DebugASan/DebugTSan** | 覆盖调试、优化、内存错误与数据竞争场景 |
| 代码质量 | **clang-tidy + clang-format** | 统一格式并在提交前发现常见缺陷 |
| 覆盖率 | **gcov + lcov + genhtml** | 行业标准的 C++ 覆盖率方案 |

### 3.3 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                    CI/CD 层 (GitHub Actions)                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │  Build   │  │   Test   │  │  ASan    │  │ Coverage │   │
│  │(4 combos)│  │ (ctest)  │  │  (必过)  │  │(阈值80%) │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                    CMake 工具链层                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │  test    │  │  coverage│  │  tidy    │  │  format  │   │
│  │  target  │  │  target  │  │  target  │  │  target  │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  构建变体：Debug / Release / RelASan / RelTSan   │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                    测试框架内核层 (doctest)                   │
│  TEST_CASE("sink level filtering", "[sink][level]") {       │
│      auto mock = std::make_shared<mock_sink>();             │
│      mock->set_level(level::warn);                          │
│      REQUIRE(mock->should_log(level::info) == false);       │
│      REQUIRE(mock->should_log(level::error) == true);       │
│  }                                                          │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                    测试辅助设施层                             │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐          │
│  │  mock_sink │  │ test_fixture│  │  bench_    │          │
│  │ (内存捕获) │  │ (setup/    │  │  helper    │          │
│  │            │  │  teardown) │  │            │          │
│  └────────────┘  └────────────┘  └────────────┘          │
└─────────────────────────────────────────────────────────────┘
```

---

## 四、测试框架详细设计

### 4.1 目录结构重构

```
tests/
├── CMakeLists.txt              # 测试入口 CMake
├── framework/                  # 测试框架内核 + 辅助设施
│   ├── doctest.h               # doctest 单头文件（v2.4.11）
│   ├── mock_sink.h             # 内存捕获型测试替身
│   ├── test_fixture.h          # 通用测试夹具（临时目录、日志清理等）
│   └── test_utils.h            # 测试工具函数（如 wait_for_file）
├── unit/                       # 单元测试（按组件组织）
│   ├── test_level.cpp          # 日志级别系统
│   ├── test_sink.cpp           # Sink 基础行为
│   ├── test_formatter.cpp      # 格式化引擎
│   ├── test_pattern.cpp        # Pattern 占位符
│   ├── test_logger.cpp         # Logger 接口
│   ├── test_registry.cpp       # Registry 全局管理
│   ├── test_async.cpp          # 异步日志核心
│   ├── test_queue.cpp          # 队列（circular/mpmc/mpsc）
│   ├── test_thread_pool.cpp    # 线程池
│   └── test_rotating_file.cpp  # 文件轮转
├── integration/                # 集成测试
│   └── test_end_to_end.cpp     # 全链路：创建→写入→读取→断言
├── benchmark/                  # 性能基准
│   ├── bench_throughput.cpp    # 吞吐基准
│   └── bench_latency.cpp       # 延迟基准
└── fixtures/                   # 测试数据（如参考日志文件）
    └── expected_logs/
```

### 4.2 mock_sink（测试替身）

日志库测试的核心难题：**如何断言日志确实被写了？**

解决方案：实现一个 `mock_sink`，将日志捕获到内存 `std::vector<std::string>` 中，测试直接用 `REQUIRE` 断言。

```cpp
#pragma once
#include "minispdlog/sinks/base_sink.h"
#include <vector>
#include <mutex>

namespace minispdlog::tests {

/// 内存捕获型 Sink——测试专用的测试替身。
/// 解决日志输出到控制台或文件后如何自动验证内容的问题。
template<typename Mutex = std::mutex>
class mock_sink : public sinks::base_sink<Mutex> {
public:
    /// 获取已捕获的全部日志内容，即格式化后的字符串。
    std::vector<std::string> messages() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return messages_;
    }

    /// 清空捕获内容。
    void clear() {
        std::lock_guard<Mutex> lock(this->mutex_);
        messages_.clear();
    }

    /// 断言最后一条日志包含指定子串。
    bool last_contains(const std::string& substr) const {
        std::lock_guard<Mutex> lock(this->mutex_);
        if (messages_.empty()) return false;
        return messages_.back().find(substr) != std::string::npos;
    }

protected:
    void sink_it_(const details::log_msg& msg) override {
        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);
        messages_.emplace_back(formatted.data(), formatted.size());
    }

    void flush_() override {}

private:
    std::vector<std::string> messages_;
};

using mock_sink_mt = mock_sink<std::mutex>;
using mock_sink_st = mock_sink<sinks::null_mutex>;

} ///< 结束 minispdlog::tests 命名空间。
```

**为什么这个设计优于当前测试**：
- 当前测试写文件后需要人眼查看文件内容，mock_sink 让断言变成纯内存操作，毫秒级、可自动化
- `mock_sink` 继承 `base_sink`，复用了现有格式化逻辑，测试代码本身就是对生产代码的调用

### 4.3 CMake 测试配置

```cmake
# tests/CMakeLists.txt

# ─── 构建变体预设 ───
# Debug: 调试符号 + 低优化
# Release: 全优化（默认）
# RelASan:  release + AddressSanitizer + UBSan
# RelTSan:  release + ThreadSanitizer
# DebugASan: debug + AddressSanitizer + UBSan
# DebugTSan: debug + ThreadSanitizer

# ─── 测试框架内核 ───
set(TEST_FRAMEWORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/framework)

# 收集所有单元测试 + 集成测试源文件
file(GLOB_RECURSE UNIT_TEST_SOURCES unit/*.cpp)
file(GLOB_RECURSE INTEGRATION_TEST_SOURCES integration/*.cpp)

# 统一测试入口可执行文件（单入口设计，编译效率远高于 10+ 独立可执行文件）
add_executable(minispdlog_tests
    test_main.cpp               # 仅包含 #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    ${UNIT_TEST_SOURCES}
    ${INTEGRATION_TEST_SOURCES}
)

target_include_directories(minispdlog_tests PRIVATE
    ${TEST_FRAMEWORK_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(minispdlog_tests PRIVATE
    minispdlog
    Threads::Threads
)

# CTest 集成：每个 TEST_CASE 成为 CTest 的一个测试项
doctest_discover_tests(minispdlog_tests)

# ─── benchmark 目标（可选，依赖 Google Benchmark） ───
if(benchmark_FOUND)
    add_executable(minispdlog_bench benchmark/bench_throughput.cpp)
    target_link_libraries(minispdlog_bench PRIVATE minispdlog benchmark::benchmark)
endif()

# ─── 代码覆盖率目标 ───
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    option(MINISPDLOG_ENABLE_COVERAGE "Enable coverage reporting" OFF)
    if(MINISPDLOG_ENABLE_COVERAGE)
        target_compile_options(minispdlog_tests PRIVATE --coverage -O0)
        target_link_options(minispdlog_tests PRIVATE --coverage)
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND lcov --capture --directory . --output-file coverage.info
            COMMAND lcov --remove coverage.info '/usr/*' '*/third_party/*' '*/tests/*' --output-file coverage.info.cleaned
            COMMAND genhtml coverage.info.cleaned --output-directory coverage_report
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating coverage report..."
        )
    endif()
endif()
```

### 4.4 测试示例（重写后的风格）

**当前风格（手写，无断言）**：
```cpp
void test_level_filtering() {
    std::cout << "\n========== 测试3:级别过滤 ==========\n";
    auto console_sink = std::make_shared<sinks::console_sink_mt>();
    logger my_logger("FilterLogger", console_sink);
    my_logger.set_level(level::warn);
    my_logger.trace("Trace (不会输出)"); ///< 依靠人工观察判断。
    my_logger.warn("Warn (会输出)");     ///< 依靠人工观察判断。
}
```

**新框架风格（doctest，自动断言）**：
```cpp
#include <doctest.h>
#include "minispdlog/minispdlog.h"
#include "framework/mock_sink.h"

using namespace minispdlog;
using minispdlog::tests::mock_sink_mt;

TEST_CASE("logger level filtering", "[logger][level]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("test", mock);
    lg.set_level(level::warn);

    lg.trace("should not appear");
    lg.debug("should not appear");
    lg.info("should not appear");
    REQUIRE(mock->messages().size() == 0); ///< 自动化断言。

    lg.warn("should appear");
    lg.error("should appear");
    REQUIRE(mock->messages().size() == 2); ///< 自动化断言。
    REQUIRE(mock->last_contains("should appear"));
}

TEST_CASE("multi-sink independent levels", "[sink][multi]") {
    auto console = std::make_shared<mock_sink_mt>();
    auto file = std::make_shared<mock_sink_mt>();
    console->set_level(level::info);
    file->set_level(level::error);

    logger lg("multi");
    lg.add_sink(console);
    lg.add_sink(file);

    lg.warn("warning");
    REQUIRE(console->messages().size() == 1); ///< console 接收 warn。
    REQUIRE(file->messages().size() == 0);    ///< file 不接收 warn。

    lg.error("error");
    REQUIRE(console->messages().size() == 2);
    REQUIRE(file->messages().size() == 1); ///< file 接收 error。
}
```

### 4.5 构建变体设计

```cmake
# 根 CMakeLists.txt 中添加的构建变体
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelASan;RelTSan;DebugASan;DebugTSan")

# 通用 ASan/UBSan 设置
set(ASAN_FLAGS "-fsanitize=address,undefined -fno-sanitize-recover=all")
set(TSAN_FLAGS "-fsanitize=thread")

# RelASan: Release + ASan + UBSan
set(CMAKE_CXX_FLAGS_RELASAN "${CMAKE_CXX_FLAGS_RELEASE} -O2 -g ${ASAN_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_RELASAN "${ASAN_FLAGS}")

# RelTSan: Release + ThreadSanitizer
set(CMAKE_CXX_FLAGS_RELTSAN "${CMAKE_CXX_FLAGS_RELEASE} -O2 -g ${TSAN_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_RELTSAN "${TSAN_FLAGS}")

# DebugASan: Debug + ASan + UBSan
set(CMAKE_CXX_FLAGS_DEBUGASAN "${CMAKE_CXX_FLAGS_DEBUG} ${ASAN_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_DEBUGASAN "${ASAN_FLAGS}")

# DebugTSan: Debug + ThreadSanitizer
set(CMAKE_CXX_FLAGS_DEBUGTSAN "${CMAKE_CXX_FLAGS_DEBUG} ${TSAN_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_DEBUGTSAN "${TSAN_FLAGS}")
```

**使用方式**：
```bash
# 默认 Release
cmake -S . -B build

# 检测内存泄漏和 UB
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelASan
cmake --build build --target minispdlog_tests
ctest --output-on-failure

# 检测 data race
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelTSan
cmake --build build --target minispdlog_tests
ctest --output-on-failure
```

**为什么这样设计**：
- ASan 在 **Rel**ease 模式下运行，能检测真实代码中的内存问题，同时性能接近生产环境
- TSan 在 **Rel**ease 模式下运行，因为 Debug 模式引入的额外同步会影响 race 检测
- 这些变体把优化级别与错误检测维度分开，便于复现仅在特定构建模式出现的问题

### 4.6 GitHub Actions CI 流水线

```yaml
# .github/workflows/ci.yml
name: CI

on: [push, pull_request]

jobs:
  test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
        build_type: [Release, RelASan, RelTSan]
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=${{ matrix.build_type }}

      - name: Build
        run: cmake --build build --parallel

      - name: Test
        run: ctest --test-dir build --output-on-failure

  coverage:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure with Coverage
        run: cmake -S . -B build -DMINISPDLOG_ENABLE_COVERAGE=ON
      - name: Build
        run: cmake --build build
      - name: Test + Coverage
        run: cmake --build build --target coverage
      - name: Upload Coverage
        uses: codecov/codecov-action@v4
        with:
          files: build/coverage.info.cleaned
```

---

## 五、落地实施步骤

### Step 1：引入 doctest（1 天）

1. 下载 `doctest.h`（v2.4.11，约 6.7k 行）到 `tests/framework/doctest.h`
2. 创建 `tests/test_main.cpp`：
   ```cpp
   #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
   #include "doctest.h"
   ```
3. 修改 `tests/CMakeLists.txt` 为单入口设计

### Step 2：编写 mock_sink（1 天）

1. 创建 `tests/framework/mock_sink.h`（如 4.2 节设计）
2. 创建 `tests/framework/test_fixture.h`（自动创建临时目录、自动清理）

### Step 3：重写单元测试（3-4 天）

按优先级顺序重写：
1. `test_level.cpp`（最简单，验证框架）
2. `test_sink.cpp`（引入 mock_sink）
3. `test_formatter.cpp` / `test_pattern.cpp`
4. `test_logger.cpp`（核心逻辑）
5. `test_registry.cpp`（多线程场景）
6. `test_queue.cpp` / `test_thread_pool.cpp` / `test_async.cpp`

### Step 4：添加构建变体 + CI（1-2 天）

1. 修改根 `CMakeLists.txt` 添加 ASan/TSan 变体
2. 修改 `.github/workflows/ci.yml` 跑多矩阵构建
3. 添加 `coverage` target

### Step 5：添加 clang-tidy + clang-format（1 天）

1. 创建 `.clang-format`（LLVM 风格）
2. 创建 `.clang-tidy`（启用 `cppcoreguidelines-*`, `performance-*`, `bugprone-*`）
3. 添加 CMake `tidy` / `format` target，并接入 CI

---

## 六、附录：测试选型依据

### 6.1 必须满足的约束

| 约束 | 项目选择 | 验收方式 |
|------|----------|----------|
| 测试随仓库可直接构建 | 使用 doctest 单头文件 | 全新构建目录不需要另行下载测试框架 |
| 避免重复编译项目头文件 | 所有用例链接到一个 `minispdlog_tests` | 构建日志只生成一个单测可执行文件 |
| 失败能定位到组件 | CTest 按源文件注册 `minispdlog.<component>` | `ctest -N` 能列出各组件条目 |
| 能覆盖并发缺陷 | 提供 ASan / TSan 构建变体 | CI 分别运行内存和线程检查 |
| 能验证真实输出 | `mock_sink` 加临时文件、回环网络测试 | 断言内容、计数、恢复和平台行为 |
| 性能结论可复现 | 分离过滤、格式化、入队和 I/O 基准 | 报告环境、消息尺寸、并发数和统计口径 |

### 6.2 为什么保留 doctest

doctest 已经满足当前单元测试所需的断言、标签、夹具和命令行过滤能力，并且无需单独构建测试运行时。继续使用它可以把维护成本集中在 minispdlog 的行为覆盖上。若未来测试规模发生显著变化，应先列出缺失能力和迁移成本，再决定是否更换框架，而不是仅根据流行度改造。

### 6.3 测试分层

- **单元测试**：验证级别过滤、formatter、队列和 registry 等局部契约；
- **组件测试**：验证具体 Sink、异步线程池、协程控制面和文件恢复；
- **平台测试**：验证 Linux 网络、可选 Qt 与不支持平台的显式失败；
- **故障测试**：验证队列满载、异常传播、WAL 回放、断尾和轮转失败；
- **性能测试**：只用于发现回归和定位成本，不替代功能正确性测试。

---

> **结语**：minispdlog 的测试体系应直接保护同步、异步、耐久、结构化、网络和协程控制面的公开语义。路线图中的每项能力都应与行为测试、故障测试和必要的性能基线一同演进。
