#include "minispdlog/async.h"
#include "minispdlog/details/thread_pool.h"
#include "minispdlog/json_formatter.h"
#include "minispdlog/logger.h"
#include "minispdlog/sinks/console_sink.h"
#include "minispdlog/sinks/file_sink.h"
#include "minispdlog/sinks/null_sink.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

using namespace minispdlog;

class timer {
    using clock = std::chrono::high_resolution_clock;
    clock::time_point start_;

public:
    timer() : start_(clock::now()) {}
    double elapsed_ms() const {
        auto dur = clock::now() - start_;
        return std::chrono::duration<double, std::milli>(dur).count();
    }
    void reset() { start_ = clock::now(); }
};

void print_rate(const char* name, int n, double ms) {
    std::cout << "  " << name << "\n";
    std::cout << "    消息数量: " << n << "\n";
    std::cout << "    耗时: " << ms << " ms\n";
    std::cout << "    吞吐: " << (n / (ms / 1000.0)) << " msg/s\n";
}

void bench_format_only() {
    std::cout << "\n========== [format] null_sink（不含磁盘/控制台） ==========\n";
    const int N = 1000000;

    {
        auto sink = std::make_shared<sinks::null_sink_st>();
        auto lg = std::make_shared<logger>("fmt", sink);
        timer t;
        for (int i = 0; i < N; ++i) {
            lg->info("Format default message {}", i);
        }
        print_rate("default pattern", N, t.elapsed_ms());
        std::cout << "    bytes: " << sink->bytes_written() << "\n";
    }
    {
        auto sink = std::make_shared<sinks::null_sink_st>();
        sink->set_pattern("%v");
        auto lg = std::make_shared<logger>("fmtv", sink);
        timer t;
        for (int i = 0; i < N; ++i) {
            lg->info("Format message-only {}", i);
        }
        print_rate("%v", N, t.elapsed_ms());
    }
    {
        auto sink = std::make_shared<sinks::null_sink_st>();
        sink->set_formatter(std::make_unique<json_formatter>());
        auto lg = std::make_shared<logger>("fmtj", sink);
        timer t;
        for (int i = 0; i < N; ++i) {
            lg->info("Format json message {}", i);
        }
        print_rate("json", N, t.elapsed_ms());
    }
}

void bench_filtered_console() {
    std::cout << "\n========== [filter] sink level=off（只测 payload fmt + 级别判断） ==========\n";
    const int N = 1000000;
    auto console_sink = std::make_shared<sinks::console_sink_mt>();
    console_sink->set_level(level::off);

    auto sync_logger = std::make_shared<logger>("sync", console_sink);
    timer t;
    for (int i = 0; i < N; ++i) {
        sync_logger->info("Sync message {}", i);
    }
    print_rate("sync filtered", N, t.elapsed_ms());

    details::thread_pool pool(8192, 1);
    auto async_logger = std::make_shared<logger>("async", console_sink);
    t.reset();
    for (int i = 0; i < N; ++i) {
        details::log_msg msg("async", level::info, "Async message");
        pool.post_log(async_logger, msg);
    }
    print_rate("async enqueue filtered", N, t.elapsed_ms());
    pool.post_flush(async_logger, true);
}

void bench_file_disk() {
    std::cout << "\n========== [disk] file_sink ==========\n";
    const int N = 10000;

    auto file_sink_sync = std::make_shared<sinks::file_sink_mt>("logs/bench_sync.log", true);
    auto sync_logger = std::make_shared<logger>("sync", file_sink_sync);
    timer t;
    for (int i = 0; i < N; ++i) {
        sync_logger->info("Sync file message {}", i);
    }
    sync_logger->flush();
    print_rate("sync file", N, t.elapsed_ms());

    auto file_sink_async = std::make_shared<sinks::file_sink_mt>("logs/bench_async.log", true);
    details::thread_pool pool(8192, 1);
    auto async_logger = std::make_shared<logger>("async", file_sink_async);
    t.reset();
    for (int i = 0; i < N; ++i) {
        details::log_msg msg("async", level::info, "Async file message");
        pool.post_log(async_logger, msg);
    }
    print_rate("async file enqueue", N, t.elapsed_ms());
    pool.post_flush(async_logger, true);
}

void bench_multithreaded() {
    std::cout << "\n========== [filter] 多线程 sink level=off ==========\n";
    const int N = 20000;
    const int THREADS = 4;

    auto sink = std::make_shared<sinks::null_sink_mt>();
    sink->set_level(level::off);
    auto sync_logger = std::make_shared<logger>("sync", sink);
    timer t;
    {
        std::vector<std::thread> threads;
        std::atomic<int> ready{0};
        for (int ti = 0; ti < THREADS; ++ti) {
            threads.emplace_back([&, ti]() {
                ready++;
                while (ready < THREADS) {
                }
                for (int i = 0; i < N; ++i) {
                    sync_logger->info("Sync thread {} msg {}", ti, i);
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
    }
    print_rate("sync filtered 4T", THREADS * N, t.elapsed_ms());

    details::thread_pool pool(16384, 2);
    auto async_logger = std::make_shared<logger>("async", sink);
    t.reset();
    {
        std::vector<std::thread> threads;
        std::atomic<int> ready{0};
        for (int ti = 0; ti < THREADS; ++ti) {
            threads.emplace_back([&]() {
                ready++;
                while (ready < THREADS) {
                }
                for (int i = 0; i < N; ++i) {
                    details::log_msg msg("async", level::info, "Async thread msg");
                    pool.post_log(async_logger, msg);
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
    }
    print_rate("async enqueue filtered 4T", THREADS * N, t.elapsed_ms());
    pool.post_flush(async_logger, true);
}

void bench_queue_pressure() {
    std::cout << "\n========== [filter] 小队列压力（reuse logger） ==========\n";
    const int N = 100000;
    auto sink = std::make_shared<sinks::null_sink_mt>();
    sink->set_level(level::off);

    details::thread_pool pool(64, 1);
    auto stress_logger = std::make_shared<logger>("stress", sink);

    timer t;
    for (int i = 0; i < N; ++i) {
        details::log_msg msg("stress", level::info, "Stress test message");
        pool.post_log_nowait(std::shared_ptr<logger>(stress_logger), msg);
    }
    print_rate("post_log_nowait", N, t.elapsed_ms());
    std::cout << "    overrun: " << pool.overrun_count() << "\n";
    std::cout << "    discard: " << pool.discard_count() << "\n";
}

void bench_async_null_wake() {
    std::cout << "\n========== [async] null_sink enqueue vs drain（批量 wake） ==========\n";
    const int N = 1000000;

    auto run = [&](const char* name, thread_pool_options opts) {
        opts.queue_size = 262144;
        opts.thread_count = 1;
        details::thread_pool pool(opts);
        auto sink = std::make_shared<sinks::null_sink_mt>();
        auto lg = std::make_shared<logger>("async", sink);

        for (int i = 0; i < 10000; ++i) {
            details::log_msg msg("async", level::info, "warmup");
            pool.post_log(lg, msg);
        }
        pool.post_flush(lg, true);

        timer t;
        for (int i = 0; i < N; ++i) {
            details::log_msg msg("async", level::info, "async null message");
            pool.post_log(lg, msg);
        }
        const double enqueue_ms = t.elapsed_ms();
        t.reset();
        pool.post_flush(lg, true);
        const double flush_ms = t.elapsed_ms();

        std::cout << "  " << name << "\n";
        print_rate("enqueue", N, enqueue_ms);
        std::cout << "    flush wait: " << flush_ms << " ms  (leftover in queue, not full drain)\n";
        print_rate("enqueue+flush", N, enqueue_ms + flush_ms);
        std::cout << "    bytes: " << sink->bytes_written() << "\n";
    };

    thread_pool_options batched;
    batched.wake_batch = 64;
    batched.wake_interval_us = 100;

    thread_pool_options every;
    every.wake_batch = 1;
    every.wake_interval_us = 0;

    batched.queue_type = async_queue_type::blocking;
    every.queue_type = async_queue_type::blocking;
    run("blocking  N=64 T=100us", batched);
    run("blocking  notify every", every);

    batched.queue_type = async_queue_type::lockfree;
    every.queue_type = async_queue_type::lockfree;
    run("lockfree  N=64 T=100us", batched);
    run("lockfree  notify every", every);
}

int main() {
    std::cout << "MiniSpdlog 性能对比测试（format / filter / disk 分开）\n";
    std::filesystem::create_directories("logs");

    bench_format_only();
    bench_async_null_wake();
    bench_filtered_console();
    bench_file_disk();
    bench_multithreaded();
    bench_queue_pressure();
    return 0;
}
