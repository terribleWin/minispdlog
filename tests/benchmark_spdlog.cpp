//spdlog 性能测试
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <fstream>
#include <iomanip>

using namespace std::chrono;

class BenchmarkTimer {
public:
    BenchmarkTimer() : start_(high_resolution_clock::now()) {}
    
    void reset() {
        start_ = high_resolution_clock::now();
    }
    
    double elapsed_ms() const {
        auto end = high_resolution_clock::now();
        return duration_cast<nanoseconds>(end - start_).count() / 1e6;
    }
    
private:
    high_resolution_clock::time_point start_;
};

struct BenchmarkResult {
    std::string test_name;
    int message_count;
    int thread_count;
    double elapsed_ms;
    double throughput;
    
    void print() const {
        std::cout << std::left << std::setw(40) << test_name
                  << " | " << std::right << std::setw(10) << message_count
                  << " | " << std::setw(8) << thread_count
                  << " | " << std::setw(10) << std::fixed << std::setprecision(2) << elapsed_ms
                  << " | " << std::setw(12) << std::fixed << std::setprecision(0) << throughput
                  << std::endl;
    }
};

std::vector<BenchmarkResult> results;

void benchmark_sync_st(int iterations) {
    spdlog::drop("bench_sync_st");
    auto logger = spdlog::basic_logger_st("bench_sync_st", "logs/spd_sync_st.log");
    
    BenchmarkTimer timer;
    for (int i = 0; i < iterations; ++i) {
        logger->info("Benchmark message #{} with some text", i);
    }
    logger->flush();
    double elapsed = timer.elapsed_ms();
    
    results.push_back({
        "Spdlog - Sync ST",
        iterations,
        1,
        elapsed,
        iterations / (elapsed / 1000.0)
    });
    
    spdlog::drop("bench_sync_st");
}

void benchmark_sync_mt(int iterations) {
    spdlog::drop("bench_sync_mt");
    auto logger = spdlog::basic_logger_mt("bench_sync_mt", "logs/spd_sync_mt.log");
    
    BenchmarkTimer timer;
    for (int i = 0; i < iterations; ++i) {
        logger->info("Benchmark message #{} with some text", i);
    }
    logger->flush();
    double elapsed = timer.elapsed_ms();
    
    results.push_back({
        "Spdlog - Sync MT",
        iterations,
        1,
        elapsed,
        iterations / (elapsed / 1000.0)
    });
    
    spdlog::drop("bench_sync_mt");
}

void benchmark_async_mt(int iterations) {
    spdlog::drop("bench_async_block");
    spdlog::init_thread_pool(131072, 1);
    
    auto logger = spdlog::basic_logger_mt<spdlog::async_factory>(
        "bench_async_block",
        "logs/spd_async_block.log"
    );
    
    BenchmarkTimer timer;
    for (int i = 0; i < iterations; ++i) {
        logger->info("Benchmark message #{} with some text", i);
    }
    double call_time = timer.elapsed_ms();
    
    logger->flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    results.push_back({
        "Spdlog - Async Block",
        iterations,
        1,
        call_time,
        iterations / (call_time / 1000.0)
    });
    
    spdlog::drop("bench_async_block");
}

void benchmark_async_overrun(int iterations) {
    spdlog::drop("bench_async_overrun");
    spdlog::init_thread_pool(131072, 1);
    
    auto logger = spdlog::create_async_nb<spdlog::sinks::basic_file_sink_mt>(
        "bench_async_overrun",
        "logs/spd_async_overrun.log"
    );
    
    BenchmarkTimer timer;
    for (int i = 0; i < iterations; ++i) {
        logger->info("Benchmark message #{} with some text", i);
    }
    double call_time = timer.elapsed_ms();
    
    logger->flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    results.push_back({
        "Spdlog - Async Overrun",
        iterations,
        1,
        call_time,
        iterations / (call_time / 1000.0)
    });
    
    spdlog::drop("bench_async_overrun");
}

void benchmark_multi_thread_sync(int thread_count, int messages_per_thread) {
    spdlog::drop("bench_multi_sync");
    auto logger = spdlog::basic_logger_mt("bench_multi_sync", "logs/spd_multi_sync.log");
    
    BenchmarkTimer timer;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([logger, messages_per_thread, t]() {
            for (int i = 0; i < messages_per_thread; ++i) {
                logger->info("Thread {} - Message #{}", t, i);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    logger->flush();
    double elapsed = timer.elapsed_ms();
    int total_messages = thread_count * messages_per_thread;
    
    results.push_back({
        "Spdlog - Multi Sync MT",
        total_messages,
        thread_count,
        elapsed,
        total_messages / (elapsed / 1000.0)
    });
    
    spdlog::drop("bench_multi_sync");
}

void benchmark_multi_thread_async(int thread_count, int messages_per_thread) {
    spdlog::drop("bench_multi_async");
    spdlog::init_thread_pool(131072, 1);
    
    auto logger = spdlog::basic_logger_mt<spdlog::async_factory>(
        "bench_multi_async",
        "logs/spd_multi_async.log"
    );
    
    BenchmarkTimer timer;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([logger, messages_per_thread, t]() {
            for (int i = 0; i < messages_per_thread; ++i) {
                logger->info("Thread {} - Message #{}", t, i);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    double call_time = timer.elapsed_ms();
    logger->flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    int total_messages = thread_count * messages_per_thread;
    
    results.push_back({
        "Spdlog - Multi Async MT",
        total_messages,
        thread_count,
        call_time,
        total_messages / (call_time / 1000.0)
    });
    
    spdlog::drop("bench_multi_async");
}

int main() {
    system("mkdir -p logs");
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "   Spdlog 性能测试" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    const int SINGLE_ITERATIONS = 500000;
    // const int MULTI_THREADS = 8;
    const int MULTI_THREADS = 16;
    const int MULTI_MESSAGES = 62500;  // 16线程 x 62500条 = 100万条
    
    std::cout << "测试配置：" << std::endl;
    std::cout << "  单线程测试：" << SINGLE_ITERATIONS << " 条消息" << std::endl;
    std::cout << "  多线程测试：" << MULTI_THREADS << " 线程 x " 
              << MULTI_MESSAGES << " 消息\n" << std::endl;
    
    // 单线程测试
    std::cout << "执行单线程测试..." << std::endl;
    benchmark_sync_st(SINGLE_ITERATIONS);
    benchmark_sync_mt(SINGLE_ITERATIONS);
    benchmark_async_mt(SINGLE_ITERATIONS);
    benchmark_async_overrun(SINGLE_ITERATIONS);
    
    // 多线程测试
    std::cout << "执行多线程测试..." << std::endl;
    benchmark_multi_thread_sync(MULTI_THREADS, MULTI_MESSAGES);
    benchmark_multi_thread_async(MULTI_THREADS, MULTI_MESSAGES);
    
    // 打印结果
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试结果汇总" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    std::cout << std::left << std::setw(40) << "测试项目"
              << " | " << std::right << std::setw(10) << "消息数"
              << " | " << std::setw(8) << "线程数"
              << " | " << std::setw(10) << "耗时(ms)"
              << " | " << std::setw(12) << "吞吐量(msg/s)"
              << std::endl;
    std::cout << std::string(90, '-') << std::endl;
    
    for (const auto& result : results) {
        result.print();
    }
    
    // 保存结果到文件
    std::ofstream out("results/spdlog_results.txt");
    out << "Spdlog Benchmark Results\n\n";
    for (const auto& result : results) {
        out << result.test_name << ": " 
            << result.throughput << " msg/sec\n";
    }
    out.close();
    
    std::cout << "\n结果已保存到 results/spdlog_results.txt" << std::endl;
    
    return 0;
}