#include "minispdlog/sinks/qt_sink.h"
#include "minispdlog/logger.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <thread>

using namespace minispdlog;

// 测试 1：基础回调功能
void test_basic_callback() {
    std::cout << "\n========== 测试1:基础回调功能 ==========\n";
    
    std::vector<std::string> captured;
    
    auto sink = std::make_shared<sinks::qt_sink>(
        [&](const std::string& msg, level lvl) {
            captured.push_back(msg);
        }
    );
    
    auto bench_logger = std::make_shared<logger>("qt_test", sink);
    
    bench_logger->info("Hello {}", "Qt");
    bench_logger->warn("Warning {}", 42);
    bench_logger->error("Error {}", 1.5);
    
    bench_logger->flush();
    
    assert(captured.size() == 3);
    std::cout << "  ✓ 捕获 " << captured.size() << " 条日志\n";
    for (const auto& msg : captured) {
        std::cout << "    " << msg;
    }
}

// 测试 2：级别过滤
void test_level_filter() {
    std::cout << "\n========== 测试2:级别过滤 ==========\n";
    
    std::vector<std::string> captured;
    
    auto sink = std::make_shared<sinks::qt_sink>(
        [&](const std::string& msg, level lvl) {
            captured.push_back(msg);
        }
    );
    sink->set_level(level::warn);
    
    auto bench_logger = std::make_shared<logger>("qt_filter", sink);
    
    bench_logger->trace("trace");
    bench_logger->debug("debug");
    bench_logger->info("info");
    bench_logger->warn("warn");
    bench_logger->error("error");
    
    bench_logger->flush();
    
    assert(captured.size() == 2);
    std::cout << "  ✓ 级别过滤正确: " << captured.size() << " 条 (应为2)\n";
}

// 测试 3：多线程安全
void test_multithreaded() {
    std::cout << "\n========== 测试3:多线程安全 ==========\n";
    
    std::atomic<int> count{0};
    
    auto sink = std::make_shared<sinks::qt_sink>(
        [&](const std::string& msg, level lvl) {
            count++;
        }
    );
    
    auto bench_logger = std::make_shared<logger>("qt_mt", sink);
    
    const int THREADS = 4;
    const int MSGS_PER_THREAD = 1000;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([bench_logger]() {
            for (int i = 0; i < MSGS_PER_THREAD; ++i) {
                bench_logger->info("Thread message {}", i);
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    assert(count == THREADS * MSGS_PER_THREAD);
    std::cout << "  ✓ 多线程 " << THREADS << " 线程 x " << MSGS_PER_THREAD
              << " 条 = " << count << " (无丢失)\n";
}

// 测试 4：空回调不崩溃
void test_empty_callback() {
    std::cout << "\n========== 测试4:空回调不崩溃 ==========\n";
    
    // 不传回调（默认构造的 std::function 为 empty）
    auto sink = std::make_shared<sinks::qt_sink>(nullptr);
    auto bench_logger = std::make_shared<logger>("qt_empty", sink);
    
    // 应该能正常调用，只是不输出
    bench_logger->info("This should not crash");
    bench_logger->flush();
    
    std::cout << "  ✓ 空回调不崩溃\n";
}

// 测试 5：验证 lvl 传递正确
void test_level_passthrough() {
    std::cout << "\n========== 测试5:级别透传 ==========\n";
    
    std::vector<level> captured_levels;
    
    auto sink = std::make_shared<sinks::qt_sink>(
        [&](const std::string& msg, level lvl) {
            captured_levels.push_back(lvl);
        }
    );
    
    auto bench_logger = std::make_shared<logger>("qt_lvl", sink);
    
    bench_logger->trace("test");
    bench_logger->debug("test");
    bench_logger->info("test");
    bench_logger->warn("test");
    bench_logger->error("test");
    bench_logger->critical("test");
    
    bench_logger->flush();
    
    assert(captured_levels.size() == 6);
    assert(captured_levels[0] == level::trace);
    assert(captured_levels[1] == level::debug);
    assert(captured_levels[2] == level::info);
    assert(captured_levels[3] == level::warn);
    assert(captured_levels[4] == level::error);
    assert(captured_levels[5] == level::critical);
    
    std::cout << "  ✓ 6 个级别均正确透传\n";
}

int main() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║  Qt Sink 回调模式测试                 ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
    test_basic_callback();
    test_level_filter();
    test_multithreaded();
    test_empty_callback();
    test_level_passthrough();
    
    std::cout << "\n 所有测试通过!\n\n";
    return 0;
}
