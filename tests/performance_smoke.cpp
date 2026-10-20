/**
 * @file performance_smoke.cpp
 * @brief 不依赖第三方基准框架的可重复性能烟雾测量。
 *
 * 本程序固定消息数、日志内容与 Sink，仅报告耗时和吞吐量。
 * 它不对性能设置硬阈值，返回失败只表示消息未被完整处理。
 */

#include "minispdlog/async.h"
#include "minispdlog/details/thread_pool.h"
#include "minispdlog/logger.h"
#include "minispdlog/sinks/null_sink.h"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;
constexpr std::size_t kMessages = 50000;
constexpr std::size_t kProducerThreads = 4;
constexpr std::string_view kPayload = "fixed performance payload";

/** @brief 打印单项测量结果。 */
void print_result(std::string_view name, clock_type::duration elapsed, std::size_t messages = kMessages) {
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    const double ns_per_message = static_cast<double>(nanoseconds) / static_cast<double>(messages);
    const double messages_per_second =
        static_cast<double>(messages) * 1'000'000'000.0 / static_cast<double>(nanoseconds == 0 ? 1 : nanoseconds);

    std::cout << std::left << std::setw(24) << name << "  " << std::right << std::fixed << std::setprecision(1)
              << std::setw(10) << ns_per_message << " ns/msg  " << std::setprecision(0) << std::setw(12)
              << messages_per_second << " msg/s\n";
}

/** @brief 测量同步 logger 格式化与 Sink 调用。 */
std::size_t measure_sync() {
    auto sink = std::make_shared<minispdlog::sinks::null_sink_st>();
    sink->set_pattern("%v");
    minispdlog::logger logger("perf-sync", sink);

    const auto started = clock_type::now();
    for (std::size_t i = 0; i < kMessages; ++i) {
        logger.info("fixed performance payload");
    }
    const auto elapsed = clock_type::now() - started;
    print_result("sync/null_sink", elapsed);
    return sink->bytes_written();
}

/** @brief 测量指定队列后端的入队与排空。 */
std::size_t measure_async(minispdlog::async_queue_type queue_type, std::string_view name) {
    auto sink = std::make_shared<minispdlog::sinks::null_sink_mt>();
    sink->set_pattern("%v");
    auto logger = std::make_shared<minispdlog::logger>("perf-async", sink);
    minispdlog::details::thread_pool pool(4096, 1, queue_type);

    const auto started = clock_type::now();
    for (std::size_t i = 0; i < kMessages; ++i) {
        minispdlog::details::log_msg msg("perf-async", minispdlog::level::info, kPayload);
        pool.post_log(logger.get(), msg, minispdlog::async_overflow_policy::block);
    }
    pool.post_flush(logger.get(), true);
    const auto elapsed = clock_type::now() - started;
    print_result(name, elapsed);
    return sink->bytes_written();
}

/** @brief 通过公开异步日志器测量多生产者投递与排空。 */
std::size_t measure_async_multi(minispdlog::async_queue_type queue_type, std::string_view name) {
    auto sink = std::make_shared<minispdlog::sinks::null_sink_mt>();
    sink->set_pattern("%v");

    minispdlog::thread_pool_options options;
    options.queue_size = 4096;
    options.thread_count = 1;
    options.queue_type = queue_type;
    minispdlog::init_thread_pool(options);

    auto logger = std::make_shared<minispdlog::async_logger>(
        "perf-async-multi", std::vector<minispdlog::sinks::sink_ptr>{sink}, minispdlog::async_overflow_policy::block);
    const auto started = clock_type::now();
    std::vector<std::thread> producers;
    producers.reserve(kProducerThreads);
    for (std::size_t thread = 0; thread < kProducerThreads; ++thread) {
        producers.emplace_back([logger] {
            for (std::size_t i = 0; i < kMessages / kProducerThreads; ++i) {
                logger->info("fixed performance payload");
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    logger->flush();
    const auto elapsed = clock_type::now() - started;
    print_result(name, elapsed);
    minispdlog::thread_pool_manager::instance().shutdown();
    return sink->bytes_written();
}

} // namespace

int main() {
    std::cout << "minispdlog performance smoke (" << kMessages << " messages)\n";

    const auto sync_bytes = measure_sync();
    const auto blocking_bytes = measure_async(minispdlog::async_queue_type::blocking, "async/blocking");
    const auto lockfree_bytes = measure_async(minispdlog::async_queue_type::lockfree, "async/lockfree");
    const auto blocking_multi_bytes = measure_async_multi(minispdlog::async_queue_type::blocking, "async/blocking 4P");
    const auto lockfree_multi_bytes = measure_async_multi(minispdlog::async_queue_type::lockfree, "async/lockfree 4P");

    const auto expected = kMessages * (kPayload.size() + 1);
    if (sync_bytes != expected || blocking_bytes != expected || lockfree_bytes != expected ||
        blocking_multi_bytes != expected || lockfree_multi_bytes != expected) {
        std::cerr << "message accounting failed: expected " << expected << " bytes per run\n";
        return 1;
    }
    return 0;
}
