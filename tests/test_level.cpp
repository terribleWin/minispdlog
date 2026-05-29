#include "minispdlog/level.h"
#include "minispdlog/details/utils.h"
#include <iostream>
#include <iomanip>

using namespace minispdlog;

void test_level_conversion() {
    std::cout << "\n========== 测试1:日志级别转换 ==========\n";
    
    // 测试所有级别
    level levels[] = {
        level::trace, level::debug, level::info,
        level::warn, level::error, level::critical, level::off
    };
    
    for (auto lvl : levels) {
        std::cout << "Level: " << std::setw(10) << level_to_string(lvl)
                  << " | Short: " << level_to_short_string(lvl)
                  << " | Value: " << static_cast<int>(lvl) << "\n";
    }
}

void test_string_to_level() {
    std::cout << "\n========== 测试2:字符串转级别 ==========\n";
    
    std::string test_strings[] = {
        "trace", "DEBUG", "Info", "WARN", "unknown"
    };
    
    for (const auto& str : test_strings) {
        auto lvl = string_to_level(str);
        std::cout << "Input: " << std::setw(10) << str
                  << " -> " << level_to_string(lvl) << "\n";
    }
}

void test_should_log() {
    std::cout << "\n========== 测试3:日志级别过滤 ==========\n";
    
    auto logger_level = level::info;
    level test_levels[] = {
        level::trace, level::debug, level::info, level::warn, level::error
    };
    
    std::cout << "Logger level set to: " << level_to_string(logger_level) << "\n\n";
    
    for (auto msg_level : test_levels) {
        bool should = should_log(logger_level, msg_level);
        std::cout << "Message level: " << std::setw(10) << level_to_string(msg_level)
                  << " -> " << (should ? "✓ 输出" : "✗ 过滤") << "\n";
    }
}

void test_time_utils() {
    std::cout << "\n========== 测试4:时间工具函数 ==========\n";
    
    // 直接使用 log_clock
    auto now = minispdlog::log_clock::now();
    
    std::cout << "当前时间(默认格式): " << details::format_time(now) << "\n";
    std::cout << "当前时间(自定义):   " << details::format_time(now, "%Y年%m月%d日 %H:%M:%S") << "\n";
    std::cout << "时间戳(毫秒):       " << details::get_timestamp_ms() << "\n";
}

void test_string_utils() {
    std::cout << "\n========== 测试5:字符串工具 ==========\n";
    
    std::string test1 = "  hello  ";
    std::string test2 = test1;
    std::string test3 = test1;
    
    std::cout << "原始字符串: [" << test1 << "]\n";
    std::cout << "ltrim:      [" << details::ltrim(test1) << "]\n";
    std::cout << "rtrim:      [" << details::rtrim(test2) << "]\n";
    std::cout << "trim:       [" << details::trim(test3) << "]\n";
}

int main() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   MiniSpdlog 第1天测试 - 基础框架       ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
    try {
        test_level_conversion();
        test_string_to_level();
        test_should_log();
        test_time_utils();
        test_string_utils();
        
        std::cout << "\n 所有测试通过!\n\n";
    } catch (const std::exception& e) {
        std::cerr << "\n 测试失败: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}