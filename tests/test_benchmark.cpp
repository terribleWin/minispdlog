#include "minispdlog/logger.h"
#include "minispdlog/sinks/console_sink.h"
#include "minispdlog/sinks/file_sink.h"
#include "minispdlog/details/thread_pool.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

using namespace minispdlog;

// 计时器工具
class timer {
    using clock = std::chrono::high_resolution_clock;
    clock::time_point start_;
public:
    timer() : start_(clock::now()) {}
    double elapsed_ms() const {
        auto dur = clock::now() - start_;
        return std::chrono::duration<double, std::milli>(dur).count();
    }
    double elapsed_us() const {
        auto dur = clock::now() - start_;
        return std::chrono::duration<double, std::micro>(dur).count();
    }
    void reset() { start_ = clock::now(); }
};

// ====================== 基准1:控制台输出 ======================
void bench_console_sync_vs_async() {
    std::cout << "\n========== 基准1:控制台输出(同步 vs 异步) ==========\n";
    
    const int N = 1000000;
    auto console_sink = std::make_shared<sinks::console_sink_mt>();
    console_sink->set_level(level::off);  // 关闭输出,只测纯开销
    
    // --- 同步 ---
    auto sync_logger = std::make_shared<logger>("sync", console_sink);
    timer t;
    for (int i = 0; i < N; ++i) {
        sync_logger->info("Sync message {}", i);
    }
    double sync_time = t.elapsed_ms();
    
    // --- 异步 ---
    details::thread_pool pool(8192, 1);
    auto async_logger = std::make_shared<logger>("async", console_sink);
    t.reset();
    for (int i = 0; i < N; ++i) {
        details::log_msg msg("async", level::info, "Async message " + std::to_string(i));
        pool.post_log(std::make_shared<logger>("async", console_sink), msg);
    }
    double async_time = t.elapsed_ms();
    
    std::cout << "  消息数量: " << N << "\n";
    std::cout << "  同步耗时: " << sync_time << " ms\n";
    std::cout << "  异步耗时: " << async_time << " ms\n";
    std::cout << "  加速比: " << (sync_time / async_time) << "x\n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// ====================== 基准2:文件输出 ======================
void bench_file_sync_vs_async() {
    std::cout << "\n========== 基准2:文件输出(同步 vs 异步) ==========\n";
    
    const int N = 10000;
    
    // --- 同步文件 ---
    auto file_sink_sync = std::make_shared<sinks::file_sink_mt>("logs/bench_sync.log", true);
    auto sync_logger = std::make_shared<logger>("sync", file_sink_sync);
    
    timer t;
    for (int i = 0; i < N; ++i) {
        sync_logger->info("Sync file message {}", i);
    }
    double sync_time = t.elapsed_ms();
    
    // --- 异步文件 ---
    auto file_sink_async = std::make_shared<sinks::file_sink_mt>("logs/bench_async.log", true);
    details::thread_pool pool(8192, 1);
    auto async_logger = std::make_shared<logger>("async", file_sink_async);
    
    t.reset();
    for (int i = 0; i < N; ++i) {
        details::log_msg msg("async", level::info, "Async file message " + std::to_string(i));
        pool.post_log(std::make_shared<logger>("async", file_sink_async), msg);
    }
    double async_time = t.elapsed_ms();
    
    std::cout << "  消息数量: " << N << "\n";
    std::cout << "  同步耗时: " << sync_time << " ms\n";
    std::cout << "  异步耗时: " << async_time << " ms\n";
    std::cout << "  加速比: " << (sync_time / async_time) << "x\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// ====================== 基准3:多线程场景 ======================
void bench_multithreaded() {
    std::cout << "\n========== 基准3:多线程场景(4线程) ==========\n";
    
    const int N = 20000;      // 每个线程的消息数
    const int THREADS = 4;
    
    auto sink = std::make_shared<sinks::console_sink_mt>();
    sink->set_level(level::off);
    
    // --- 同步: 4个线程各自写 ---
    auto sync_logger = std::make_shared<logger>("sync", sink);
    timer t;
    {
        std::vector<std::thread> threads;
        std::atomic<int> ready{0};
        for (int ti = 0; ti < THREADS; ++ti) {
            threads.emplace_back([&]() {
                ready++;
                while (ready < THREADS) {}  // 等待所有线程就绪
                for (int i = 0; i < N; ++i) {
                    sync_logger->info("Sync thread {} msg {}", ti, i);
                }
            });
        }
        for (auto& th : threads) th.join();
    }
    double sync_time = t.elapsed_ms();
    
    // --- 异步: 4个线程投递到同一个线程池 ---
    details::thread_pool pool(16384, 2);
    auto async_logger = std::make_shared<logger>("async", sink);
    t.reset();
    {
        std::vector<std::thread> threads;
        std::atomic<int> ready{0};
        for (int ti = 0; ti < THREADS; ++ti) {
            threads.emplace_back([&]() {
                ready++;
                while (ready < THREADS) {}
                for (int i = 0; i < N; ++i) {
                    details::log_msg msg("async", level::info, "Async thread msg");
                    pool.post_log(std::make_shared<logger>("async", sink), msg);
                }
            });
        }
        for (auto& th : threads) th.join();
    }
    double async_time = t.elapsed_ms();
    
    std::cout << "  线程数: " << THREADS << ", 每条线程 " << N << " 条\n";
    std::cout << "  总消息数: " << (THREADS * N) << "\n";
    std::cout << "  同步耗时: " << sync_time << " ms\n";
    std::cout << "  异步耗时: " << async_time << " ms\n";
    std::cout << "  加速比: " << (sync_time / async_time) << "x\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// ====================== 基准4:队列压力测试 ======================
void bench_queue_pressure() {
    std::cout << "\n========== 基准4:队列压力(小队列+大量消息) ==========\n";
    
    const int N = 100000;
    auto sink = std::make_shared<sinks::console_sink_mt>();
    sink->set_level(level::off);
    
    // 小队列: 容量64, 1个工作线程
    details::thread_pool pool(64, 1);
    auto stress_logger = std::make_shared<logger>("stress", sink);
    
    timer t;
    for (int i = 0; i < N; ++i) {
        details::log_msg msg("stress", level::info, "Stress test message");
        pool.post_log_nowait(std::make_shared<logger>("stress", sink), msg);
    }
    double enqueue_time = t.elapsed_ms();
    
    std::cout << "  消息数: " << N << "\n";
    std::cout << "  入队耗时: " << enqueue_time << " ms\n";
    std::cout << "  溢出次数: " << pool.overrun_count() << "\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

int main() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║  MiniSpdlog 性能对比测试              ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
    system("mkdir -p logs");
    
    bench_console_sync_vs_async();
    bench_file_sync_vs_async();
    bench_multithreaded();
    bench_queue_pressure();
    
    std::cout << "\n╔════════════════════════════════════════╗\n";
    std::cout << "║  性能对比测试完成                     ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
    return 0;
}
