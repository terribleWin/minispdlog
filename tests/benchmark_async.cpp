#include <benchmark/benchmark.h>
#include "minispdlog/logger.h"
#include "minispdlog/sinks/console_sink.h"
#include "minispdlog/sinks/file_sink.h"
#include "minispdlog/details/thread_pool.h"

using namespace minispdlog;

// 控制台同步日志
static void BM_ConsoleSync(benchmark::State& state) {
    auto sink = std::make_shared<sinks::console_sink_mt>();
    sink->set_level(level::off);
    auto bench_logger = std::make_shared<logger>("bench", sink);
    int i = 0;
    for (auto _ : state) {
        bench_logger->info("Sync console message {}", i++);
    }
}
BENCHMARK(BM_ConsoleSync)->Threads(1)->Threads(4);

// 控制台异步日志
static void BM_ConsoleAsync(benchmark::State& state) {
    auto sink = std::make_shared<sinks::console_sink_mt>();
    sink->set_level(level::off);
    details::thread_pool pool(8192, 2);
    auto bench_logger = std::make_shared<logger>("bench", sink);
    for (auto _ : state) {
        details::log_msg msg("bench", level::info, "Async console message");
        pool.post_log(std::make_shared<logger>("bench", sink), msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
BENCHMARK(BM_ConsoleAsync)->Threads(1)->Threads(4);

// 文件同步日志
static void BM_FileSync(benchmark::State& state) {
    auto sink = std::make_shared<sinks::file_sink_mt>("/dev/null", false);
    auto bench_logger = std::make_shared<logger>("bench", sink);
    int i = 0;
    for (auto _ : state) {
        bench_logger->info("Sync file message {}", i++);
    }
}
BENCHMARK(BM_FileSync)->Threads(1)->Threads(4);

// 文件异步日志
static void BM_FileAsync(benchmark::State& state) {
    auto sink = std::make_shared<sinks::file_sink_mt>("/dev/null", false);
    details::thread_pool pool(8192, 2);
    auto bench_logger = std::make_shared<logger>("bench", sink);
    for (auto _ : state) {
        details::log_msg msg("bench", level::info, "Async file message");
        pool.post_log(std::make_shared<logger>("bench", sink), msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
BENCHMARK(BM_FileAsync)->Threads(1)->Threads(4);

BENCHMARK_MAIN();
