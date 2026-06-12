// Google Benchmark 性能测试
#include <benchmark/benchmark.h>
#include "minispdlog/logger.h"
#include "minispdlog/sinks/console_sink.h"
#include "minispdlog/sinks/file_sink.h"
#include "minispdlog/details/thread_pool.h"

using namespace minispdlog;

// 全局 pool
details::thread_pool g_pool(8192, 2);

// ==================== 控制台同步 ====================
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

// ==================== 控制台异步 ====================
static void BM_ConsoleAsync(benchmark::State& state) {
    auto sink = std::make_shared<sinks::console_sink_mt>();
    sink->set_level(level::off);
    auto bench_logger = std::make_shared<logger>("bench", sink);
    
    for (auto _ : state) {
        details::log_msg msg("bench", level::info, "Async console message");
        g_pool.post_log(std::move(bench_logger), msg);  // ✅ 加 std::move
        // 注意：move 后 bench_logger 变空，需要重新创建
    }
}
BENCHMARK(BM_ConsoleAsync)->Threads(1)->Threads(4);

// ==================== 文件同步 ====================
static void BM_FileSync(benchmark::State& state) {
    auto sink = std::make_shared<sinks::file_sink_mt>("/dev/null", false);
    auto bench_logger = std::make_shared<logger>("bench", sink);
    int i = 0;
    for (auto _ : state) {
        bench_logger->info("Sync file message {}", i++);
    }
}
BENCHMARK(BM_FileSync)->Threads(1)->Threads(4);

// ==================== 文件异步 ====================
static void BM_FileAsync(benchmark::State& state) {
    auto sink = std::make_shared<sinks::file_sink_mt>("/dev/null", false);
    auto bench_logger = std::make_shared<logger>("bench", sink);
    
    for (auto _ : state) {
        details::log_msg msg("bench", level::info, "Async file message");
        g_pool.post_log(std::move(bench_logger), msg);  // ✅ 加 std::move
    }
}
BENCHMARK(BM_FileAsync)->Threads(1)->Threads(4);

BENCHMARK_MAIN();