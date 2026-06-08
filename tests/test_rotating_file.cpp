#include "minispdlog/minispdlog.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>

using namespace minispdlog;

// 辅助函数:检查文件是否存在
bool file_exists(const std::string& filename) {
    std::ifstream f(filename);
    return f.good();
}

// 辅助函数:获取文件大小
size_t get_file_size(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary | std::ios::ate);
    if (!f.good()) {
        return 0;
    }
    return static_cast<size_t>(f.tellg());
}

// 辅助函数:读取文件内容
std::string read_file(const std::string& filename) {
    std::ifstream f(filename);
    if (!f.good()) {
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
}

void test_filename_calculation() {
    std::cout << "\n========== 测试1:文件名计算 ==========\n";
    
    using rotating_file_sink = sinks::rotating_file_sink_mt;
    
    // 测试不同索引的文件名
    std::string base = "logs/mylog.txt";
    
    std::cout << "基础文件名: " << base << "\n\n";
    
    for (size_t i = 0; i <= 3; ++i) {
        std::string filename = rotating_file_sink::calc_filename(base, i);
        std::cout << "索引 " << i << ": " << filename << "\n";
    }
    
    // 测试没有扩展名的情况
    std::cout << "\n没有扩展名的情况:\n";
    std::string no_ext = "logs/mylog";
    for (size_t i = 0; i <= 2; ++i) {
        std::string filename = rotating_file_sink::calc_filename(no_ext, i);
        std::cout << "索引 " << i << ": " << filename << "\n";
    }
}

void test_basic_rotation() {
    std::cout << "\n========== 测试2:基础轮转功能 ==========\n";
    
    std::string base_filename = "logs/rotating_basic.log";
    size_t max_size = 1024;  // 1KB
    size_t max_files = 3;
    
    // 删除旧文件
    std::remove(base_filename.c_str());
    system("rm -f logs/rotating_basic*");
    
    auto logger = rotating_logger_mt("rotating_test", base_filename, max_size, max_files);
    
    std::cout << "配置: max_size=" << max_size << " bytes, max_files=" << max_files << "\n\n";
    
    // 写入足够多的日志以触发轮转
    std::string long_msg(100, 'A');  // 100字节的消息
    
    for (int i = 0; i < 15; ++i) {
        logger->info("{} - Message {}", long_msg, i);
    }
    
    logger->flush();
    
    std::cout << "\n写入15条日志后的文件状态:\n";
    std::cout << "  " << base_filename << ": " 
              << (file_exists(base_filename) ? "存在" : "不存在") << "\n";
    std::cout << "  " << sinks::rotating_file_sink_mt::calc_filename(base_filename, 1) << ": "
              << (file_exists(sinks::rotating_file_sink_mt::calc_filename(base_filename, 1)) ? "存在" : "不存在") << "\n";
    std::cout << "  " << sinks::rotating_file_sink_mt::calc_filename(base_filename, 2) << ": "
              << (file_exists(sinks::rotating_file_sink_mt::calc_filename(base_filename, 2)) ? "存在" : "不存在") << "\n";
    
    drop("rotating_test");
}

void test_size_trigger() {
    std::cout << "\n========== 测试3:大小触发轮转 ==========\n";
    
    std::string base_filename = "logs/size_trigger.log";
    size_t max_size = 512;  // 512 bytes
    size_t max_files = 2;
    
    std::remove(base_filename.c_str());
    std::remove(sinks::rotating_file_sink_mt::calc_filename(base_filename, 1).c_str());
    
    auto logger = rotating_logger_mt("size_test", base_filename, max_size, max_files);
    
    std::cout << "max_size=" << max_size << " bytes\n";
    
    // 写入一些日志
    for (int i = 0; i < 10; ++i) {
        logger->info("Message {} with some padding text to increase size", i);
    }
    
    logger->flush();
    
    // 检查文件大小
    size_t current_size = get_file_size(base_filename);
    size_t rotated_size = get_file_size(sinks::rotating_file_sink_mt::calc_filename(base_filename, 1));
    
    std::cout << "\n文件大小:\n";
    std::cout << "  当前文件: " << current_size << " bytes\n";
    std::cout << "  轮转文件: " << rotated_size << " bytes\n";
    
    if (current_size < max_size) {
        std::cout << " 当前文件小于 max_size\n";
    }
    
    if (rotated_size > 0) {
        std::cout << " 轮转已发生\n";
    }
    
    drop("size_test");
}

void test_multiple_rotations() {
    std::cout << "\n========== 测试4:多次轮转 ==========\n";
    
    std::string base_filename = "logs/multi_rotate.log";
    size_t max_size = 300;  // 300 bytes
    size_t max_files = 6;
    
    // 清理旧文件
    std::remove(base_filename.c_str());
    for (size_t i = 1; i <= max_files; ++i) {
        std::remove(sinks::rotating_file_sink_mt::calc_filename(base_filename, i).c_str());
    }
    
    auto logger = rotating_logger_mt("multi_rotate", base_filename, max_size, max_files);
    
    // 写入大量日志以触发多次轮转
    for (int i = 0; i < 50; ++i) {
        logger->info("Rotation test message number {} with padding", i);
    }
    
    logger->flush();
    
    std::cout << "写入50条日志后的文件状态:\n";
    
    for (size_t i = 0; i < max_files + 1; ++i) {
        std::string filename = sinks::rotating_file_sink_mt::calc_filename(base_filename, i);
        bool exists = file_exists(filename);
        
        if (i == 0) {
            std::cout << "  当前文件: " << filename << " - "
                      << (exists ? " 存在" : " 不存在") << "\n";
        } else {
            std::cout << "  轮转文件 " << i << ": " << filename << " - "
                      << (exists ? " 存在" : " 不存在") << "\n";
        }
    }
    
    drop("multi_rotate");
}

void test_max_files_limit() {
    std::cout << "\n========== 测试5:max_files 限制 ==========\n";
    
    std::string base_filename = "logs/max_files.log";
    size_t max_size = 200;
    size_t max_files = 3;

    size_t expected_total = max_files + 1;  // 4 个文件
    
    std::cout << "max_files=" << max_files 
              << " (最多保留 " << expected_total 
              << " 个文件: 1个当前 + " << max_files << "个历史)\n\n";
    
    // 清理
    std::remove(base_filename.c_str());
    for (size_t i = 1; i <= max_files + 2; ++i) {
        std::remove(sinks::rotating_file_sink_mt::calc_filename(base_filename, i).c_str());
    }
    
    auto logger = rotating_logger_mt("max_files_test", base_filename, max_size, max_files);
    
    // 写入足够多的日志以触发多次轮转
    for (int i = 0; i < 100; ++i) {
        logger->info("Testing max files limit message {}", i);
    }
    
    logger->flush();
    
    std::cout << "写入100条日志后,检查文件:\n";
    
    int existing_files = 0;
    for (size_t i = 0; i <= max_files + 2; ++i) {
        std::string filename = sinks::rotating_file_sink_mt::calc_filename(base_filename, i);
        bool exists = file_exists(filename);
        
        if (exists) {
            existing_files++;
            std::cout << "  " << filename << " -  存在\n";
        } else if (i <= max_files) {
            std::cout << "  " << filename << " -  不存在\n";
        }
    }
    
    std::cout << "\n实际文件数: " << existing_files << "\n";
    std::cout << "预期文件数: " << expected_total << "\n";
    
    if (existing_files <= expected_total) {
        std::cout << " max_files 限制生效\n";
    }
    
    drop("max_files_test");
}

void test_content_integrity() {
    std::cout << "\n========== 测试6:内容完整性 ==========\n";
    
    std::string base_filename = "logs/content_check.log";
    size_t max_size = 400;
    size_t max_files = 2;
    
    std::remove(base_filename.c_str());
    std::remove(sinks::rotating_file_sink_mt::calc_filename(base_filename, 1).c_str());
    
    auto logger = rotating_logger_mt("content_test", base_filename, max_size, max_files);
    
    // 写入一些可识别的消息
    logger->info("FIRST_MESSAGE");
    logger->info("SECOND_MESSAGE");
    
    // 写入足够的数据触发轮转
    for (int i = 0; i < 10; ++i) {
        logger->info("Padding message to trigger rotation {}", i);
    }
    
    logger->info("AFTER_ROTATION");
    logger->flush();
    
    // 检查内容
    std::string current_content = read_file(base_filename);
    std::string rotated_content = read_file(sinks::rotating_file_sink_mt::calc_filename(base_filename, 1));
    
    std::cout << "当前文件包含 'AFTER_ROTATION': " 
              << (current_content.find("AFTER_ROTATION") != std::string::npos ? "✓" : "✗") << "\n";
    
    drop("content_test");
}

void test_factory_function() {
    std::cout << "\n========== 测试7:工厂函数 ==========\n";
    
    // 使用工厂函数创建 rotating logger
    auto logger = rotating_logger_mt(
        "factory_logger",
        "logs/factory.log",
        1024 * 1024,  // 1MB
        5             // 5 个文件
    );
    
    logger->info("Created via factory function");
    logger->warn("This is a warning");
    logger->error("This is an error");
    
    // 从 registry 获取
    auto retrieved = get("factory_logger");
    if (retrieved) {
        std::cout << " 可以从 registry 获取\n";
        retrieved->info("Retrieved from registry");
    }
    
    drop("factory_logger");
}

void test_concurrent_writes() {
    std::cout << "\n========== 测试8:并发写入 ==========\n";
    
    std::string base_filename = "logs/concurrent.log";
    size_t max_size = 1024;
    size_t max_files = 3;
    
    std::remove(base_filename.c_str());
    for (size_t i = 1; i <= max_files; ++i) {
        std::remove(sinks::rotating_file_sink_mt::calc_filename(base_filename, i).c_str());
    }
    
    auto logger = rotating_logger_mt("concurrent", base_filename, max_size, max_files);
    
    // 多线程写入
    auto write_logs = [&logger](int thread_id) {
        for (int i = 0; i < 20; ++i) {
            logger->info("Thread {} - Message {}", thread_id, i);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    
    std::thread t1(write_logs, 1);
    std::thread t2(write_logs, 2);
    std::thread t3(write_logs, 3);
    
    t1.join();
    t2.join();
    t3.join();
    
    logger->flush();
    
    std::cout << "3个线程并发写入完成\n";
    std::cout << "检查文件完整性:\n";
    
    size_t current_size = get_file_size(base_filename);
    std::cout << "  当前文件大小: " << current_size << " bytes\n";
    
    if (current_size > 0) {
        std::cout << " 文件写入成功\n";
    }
    
    drop("concurrent");
}

void test_performance() {
    std::cout << "\n========== 测试9:性能测试 ==========\n";
    
    std::string base_filename = "logs/performance.log";
    size_t max_size = 1024 * 1024;  // 1MB
    size_t max_files = 5;
    
    std::remove(base_filename.c_str());
    
    auto logger = rotating_logger_mt("perf", base_filename, max_size, max_files);
    
    const int iterations = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        logger->info("Performance test message number {}", i);
    }
    
    logger->flush();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "写入 " << iterations << " 条日志耗时: " << duration.count() << " ms\n";
    std::cout << "吞吐量: " << (iterations * 1000.0 / duration.count()) << " 条/秒\n";
    
    size_t total_size = get_file_size(base_filename);
    for (size_t i = 1; i <= max_files; ++i) {
        total_size += get_file_size(sinks::rotating_file_sink_mt::calc_filename(base_filename, i));
    }
    
    std::cout << "总写入大小: " << (total_size / 1024.0) << " KB\n";
    
    drop("perf");
}

void test_real_world_scenario() {
    std::cout << "\n========== 测试10:真实场景模拟 ==========\n";
    
    // 模拟应用程序日志配置
    // 1. 应用主日志:5MB 轮转,保留 3 个文件
    auto app_logger = rotating_logger_mt(
        "app",
        "logs/app.log",
        5 * 1024 * 1024,  // 5MB
        3
    );
    
    // 2. 错误日志:10MB 轮转,保留 5 个文件
    auto error_logger = rotating_logger_mt(
        "errors",
        "logs/errors.log",
        10 * 1024 * 1024,  // 10MB
        5
    );
    
    std::cout << "应用日志配置:\n";
    std::cout << "  app.log: 5MB × 3 个文件\n";
    std::cout << "  errors.log: 10MB × 5 个文件\n\n";
    
    // 模拟应用运行
    std::cout << "模拟应用运行...\n";
    
    for (int i = 0; i < 100; ++i) {
        get("app")->info("Processing request {}", i);
        
        if (i % 10 == 0) {
            get("app")->debug("Checkpoint reached: {}", i);
        }
        
        if (i % 20 == 0) {
            get("errors")->error("Simulated error at iteration {}", i);
        }
    }
    
    std::cout << " 日志写入完成\n";
    std::cout << "\n 查看日志文件:\n";
    std::cout << "  - logs/app.log (主日志)\n";
    std::cout << "  - logs/errors.log (错误日志)\n";
    
    drop_all();
}

void test_edge_cases() {
    std::cout << "\n========== 测试11:边界情况 ==========\n";
    
    // 测试1: 非常小的 max_size
    try {
        system("rm -f logs/tiny*");
        std::cout << "测试非常小的 max_size (100 bytes)...\n";
        auto logger1 = rotating_logger_mt("tiny", "logs/tiny.log", 100, 2);
        logger1->info("Testing very small max_size");
        logger1->info("Second messageaaaaaaaaaaaaaaaaaa");
        logger1->flush();
        std::cout << " 小文件轮转正常\n";
        drop("tiny");
    } catch (const std::exception& e) {
        std::cout << " 异常: " << e.what() << "\n";
    }
    
    // 测试2: max_files = 1
    try {
        std::cout << "\n测试 max_files = 1...\n";
        auto logger2 = rotating_logger_mt("single", "logs/single.log", 500, 1);
        for (int i = 0; i < 20; ++i) {
            logger2->info("Message {} for single file", i);
        }
        logger2->flush();
        std::cout << " 单文件模式正常\n";
        drop("single");
    } catch (const std::exception& e) {
        std::cout << " 异常: " << e.what() << "\n";
    }
    
    // 测试3: 无效参数
    std::cout << "\n测试无效参数...\n";
    try {
        auto logger3 = rotating_logger_mt("invalid", "logs/invalid.log", 0, 3);
        std::cout << " 应该抛出异常但没有\n";
    } catch (const std::exception& e) {
        std::cout << " 捕获异常: " << e.what() << "\n";
    }
}

int main() {
    std::cout << "╔════════════════════════════════════════════╗\n";
    std::cout << "║ MiniSpdlog 第6天测试 - Rotating File Sink   ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n";
    
    // 创建日志目录
    system("mkdir -p logs");
    
    try {
        test_filename_calculation();
        test_basic_rotation();
        test_size_trigger();
         test_multiple_rotations();
         test_max_files_limit();
         test_content_integrity();
         test_factory_function();
         test_concurrent_writes();
         test_performance();
         test_real_world_scenario();
         test_edge_cases();
        
        std::cout << "\n 所有测试通过!\n\n";
    } catch (const std::exception& e) {
        std::cerr << "\n 测试失败: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}