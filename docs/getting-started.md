# 快速开始

本页从仓库源码构建一个 C++20 静态库，并分别运行同步和异步日志。完整接口见 [API 速查](api.md)。

## 环境要求

- CMake 3.20 或更高版本；
- 支持 C++20 的编译器；
- 一个可用的构建后端，例如 Ninja、Make 或 Visual Studio；
- 仓库中已包含 `third_party/fmt`，基础构建不需要在线下载依赖。

Qt GUI 与 Google Benchmark 都是可选依赖。Linux 网络示例只在 Linux 加入构建。

## 构建与测试

在仓库根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`--config Release` 对 Visual Studio 等多配置生成器有效，单配置生成器会忽略它。成功后核心目标名为 `minispdlog`，统一单元测试目标名为 `minispdlog_tests`。

## 在 CMake 工程中使用

当前最直接、与仓库持续验证一致的接入方式是把源码作为子目录：

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_app LANGUAGES CXX)

add_subdirectory(path/to/minispdlog)

add_executable(my_app main.cpp)
target_compile_features(my_app PRIVATE cxx_std_20)
target_link_libraries(my_app PRIVATE minispdlog)
```

库会公开 `include/`，因此业务代码使用尖括号包含头文件即可。若采用安装规则，仓库会导出 `minispdlog::minispdlog` 目标；发布包集成前仍应在目标环境验证 `fmt` 依赖的发现方式。

## 第一个同步 logger

```cpp
#include <minispdlog/minispdlog.h>

int main() {
    auto log = minispdlog::stdout_color_mt("app");
    log->set_level(minispdlog::level::info);
    log->info("service started, port={}", 8080);
    log->warn("retry count={}", 1);
    log->flush();
}
```

同步调用会在当前线程完成消息参数格式化、最终行格式化和 Sink 输出。`_mt` Sink 可供多个线程共享，`_st` Sink 使用空锁，只适合由调用方保证单线程访问的场景。

工厂会自动把 logger 注册到进程级 registry。名称重复会抛出异常；测试或需要重建同名 logger 时先调用 `minispdlog::drop("app")`。

## 全局 API

```cpp
#include <minispdlog/minispdlog.h>

int main() {
    auto log = minispdlog::stdout_color_mt("global");
    minispdlog::set_default_logger(log);
    minispdlog::set_level(minispdlog::level::debug);

    minispdlog::info("ready: {}", true);
    MINISPDLOG_WARN("source location is captured, code={}", 17);
}
```

成员函数、全局函数以及 `MINISPDLOG_*` 宏都会从字符串字面量进行编译期格式检查。宏还受 `MINISPDLOG_ACTIVE_LEVEL` 控制，可在编译期移除低等级调用；根 CMake 的单配置 Release 构建默认把活动级别设为 `INFO`。

## 第一个异步 logger

```cpp
#include <minispdlog/async.h>

int main() {
    minispdlog::thread_pool_options options;
    options.queue_size = 8192;
    options.thread_count = 1;
    options.queue_type = minispdlog::async_queue_type::blocking;

    minispdlog::init_thread_pool(options);
    auto log = minispdlog::async_file_mt(
        "async", "app.log", true,
        minispdlog::async_overflow_policy::block);

    log->info("request_id={}", 42);
    log->flush(); ///< 等待已投递日志写入，并刷新该 logger 的 Sink。
    minispdlog::shutdown();
}
```

异步 logger 复用全局线程池。没有初始化线程池时，它会退化为同步写入，而不是静默丢弃。正常进程退出前应调用 `shutdown()`：它会刷新 registry 中的 logger、停止并连接工作线程，最后清空 registry。详细时序见[同步与异步模型](sync-async-model.md)。

## 文件目录与错误

文件 Sink 会打开给定路径，但不会为你创建父目录：

```cpp
#include <filesystem>
#include <minispdlog/minispdlog.h>

int main() {
    std::filesystem::create_directories("logs");
    auto log = minispdlog::basic_logger_mt("file", "logs/app.log", false);
    log->info("append one line");
}
```

无法打开文件、重复名称、非法线程池参数等配置错误以异常报告。生产程序应在初始化边界捕获这些异常，而不是在每条日志周围捕获。

## 下一步

- 决定同步还是异步：[同步与异步模型](sync-async-model.md)
- 决定文本、JSON、滚动或网络输出：[Sink 指南](sinks.md)
- 需要批量落盘和恢复：[耐久性与恢复](durability-and-recovery.md)
- 查工厂和 pattern：[API 速查](api.md)
