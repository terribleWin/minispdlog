// Format vs I/O vs enqueue. Console/file "off" is not a formatter bench.
#include "minispdlog/details/thread_pool.h"
#include "minispdlog/json_formatter.h"
#include "minispdlog/logger.h"
#include "minispdlog/pattern_formatter.h"
#include "minispdlog/sinks/file_sink.h"
#include "minispdlog/sinks/null_sink.h"

#include <benchmark/benchmark.h>

using namespace minispdlog;

namespace {

#ifdef _WIN32
constexpr const char* kDevNull = "NUL";
#else
constexpr const char* kDevNull = "/dev/null";
#endif

details::thread_pool g_pool(8192, 2);

} // namespace

static void BM_LoggerDisabled(benchmark::State& state) {
    auto sink = std::make_shared<sinks::null_sink_mt>();
    auto bench_logger = std::make_shared<logger>("bench", sink);
    bench_logger->set_level(level::off);
    int i = 0;
    for (auto _ : state) {
        bench_logger->info("disabled message {}", i++);
    }
}
BENCHMARK(BM_LoggerDisabled)->Threads(1)->Threads(4);

static void BM_SinkFiltered(benchmark::State& state) {
    auto sink = std::make_shared<sinks::null_sink_mt>();
    sink->set_level(level::off);
    auto bench_logger = std::make_shared<logger>("bench", sink);
    int i = 0;
    for (auto _ : state) {
        bench_logger->info("filtered sink message {}", i++);
    }
}
BENCHMARK(BM_SinkFiltered)->Threads(1)->Threads(4);

static void BM_FormatDefault(benchmark::State& state) {
    auto sink = std::make_shared<sinks::null_sink_mt>();
    auto bench_logger = std::make_shared<logger>("bench", sink);
    int i = 0;
    for (auto _ : state) {
        bench_logger->info("format default message {}", i++);
    }
    benchmark::DoNotOptimize(sink->bytes_written());
}
BENCHMARK(BM_FormatDefault)->Threads(1)->Threads(4);

static void BM_FormatMessageOnly(benchmark::State& state) {
    auto sink = std::make_shared<sinks::null_sink_mt>();
    sink->set_pattern("%v");
    auto bench_logger = std::make_shared<logger>("bench", sink);
    int i = 0;
    for (auto _ : state) {
        bench_logger->info("format message-only {}", i++);
    }
    benchmark::DoNotOptimize(sink->bytes_written());
}
BENCHMARK(BM_FormatMessageOnly)->Threads(1)->Threads(4);

static void BM_FormatJson(benchmark::State& state) {
    auto sink = std::make_shared<sinks::null_sink_mt>();
    sink->set_formatter(std::make_unique<json_formatter>());
    auto bench_logger = std::make_shared<logger>("bench", sink);
    int i = 0;
    for (auto _ : state) {
        bench_logger->info("format json message {}", i++);
    }
    benchmark::DoNotOptimize(sink->bytes_written());
}
BENCHMARK(BM_FormatJson)->Threads(1)->Threads(4);

static void BM_FileNul(benchmark::State& state) {
    auto sink = std::make_shared<sinks::file_sink_mt>(kDevNull, false);
    auto bench_logger = std::make_shared<logger>("bench", sink);
    int i = 0;
    for (auto _ : state) {
        bench_logger->info("file nul message {}", i++);
    }
}
BENCHMARK(BM_FileNul)->Threads(1)->Threads(4);

static void BM_FormatAsync(benchmark::State& state) {
    auto sink = std::make_shared<sinks::null_sink_mt>();
    auto bench_logger = std::make_shared<logger>("bench", sink);
    for (auto _ : state) {
        details::log_msg msg("bench", level::info, "async format message");
        g_pool.post_log(bench_logger, msg);
    }
}
BENCHMARK(BM_FormatAsync)->Threads(1)->Threads(4);

static void BM_FileAsyncNul(benchmark::State& state) {
    auto sink = std::make_shared<sinks::file_sink_mt>(kDevNull, false);
    auto bench_logger = std::make_shared<logger>("bench", sink);
    for (auto _ : state) {
        details::log_msg msg("bench", level::info, "async file nul message");
        g_pool.post_log(bench_logger, msg);
    }
}
BENCHMARK(BM_FileAsyncNul)->Threads(1)->Threads(4);

BENCHMARK_MAIN();
