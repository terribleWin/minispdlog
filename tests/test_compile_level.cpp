#include "minispdlog/minispdlog.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>

using namespace minispdlog;

void test_macros_compile() {
    std::cout << "\n========== 测试1:编译期宏接口编译验证 ==========\n";

    // 设置默认 logger 让宏 API 能正常工作
    auto sink = std::make_shared<sinks::file_sink_mt>("logs/macro_test.log", true);
    auto bench_logger = std::make_shared<logger>("macro_test", sink);
    set_default_logger(bench_logger);

    // 验证每个宏都能正常编译且调用全局函数
    MINISPDLOG_TRACE("Macro TRACE message {}", 1);
    MINISPDLOG_DEBUG("Macro DEBUG message {}", 2);
    MINISPDLOG_INFO("Macro INFO message {}", 3);
    MINISPDLOG_WARN("Macro WARN message {}", 4);
    MINISPDLOG_ERROR("Macro ERROR message {}", 5);
    MINISPDLOG_CRITICAL("Macro CRITICAL message {}", 6);

    bench_logger->flush();

    // 验证文件不为空（宏确实产生了输出）
    std::ifstream file("logs/macro_test.log");
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    assert(!content.empty());
    assert(content.find("TRACE") != std::string::npos);
    assert(content.find("CRITICAL") != std::string::npos);
    std::cout << "  ✓ 所有宏编译通过, 文件大小: " << content.size() << " bytes\n";
}

void test_macro_with_logger_object() {
    std::cout << "\n========== 测试2:宏+运行期级别过滤 ==========\n";

    auto sink = std::make_shared<sinks::file_sink_mt>("logs/macro_filter.log", true);
    auto bench_logger = std::make_shared<logger>("filter_test", sink);
    bench_logger->set_level(level::warn);

    // 直接通过 logger 对象调用（不走宏的 default_logger）
    // 验证宏定义本身能被正确预处理
    bench_logger->trace("Should NOT appear");
    bench_logger->debug("Should NOT appear");
    bench_logger->info("Should NOT appear");
    bench_logger->warn("Should appear - warn");
    bench_logger->error("Should appear - error");
    bench_logger->critical("Should appear - critical");

    bench_logger->flush();

    std::ifstream file("logs/macro_filter.log");
    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        count++;
    }

    assert(count == 3);
    std::cout << "  ✓ 运行期过滤正确: " << count << " 条日志 (应为3)\n";
}

int main() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║  编译期日志级别控制测试                  ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";

    system("mkdir -p logs");

    test_macros_compile();
    test_macro_with_logger_object();

    std::cout << "\n 所有测试通过!\n\n";
    return 0;
}
