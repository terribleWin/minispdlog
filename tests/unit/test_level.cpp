#include "framework/doctest.h"
#include "minispdlog/level.h"
#include <string>

using namespace minispdlog;

// ============================================================
// 测试套件：日志级别系统 (level.h)
// 标签: [level] —— 可用命令行过滤: ./minispdlog_tests "[level]"
// ============================================================

TEST_CASE("level enum values are ordered correctly [level]") {
    // 日志级别必须保持严格顺序：trace < debug < info < warn < error < critical < off
    // 这是整个级别过滤系统的基础假设
    REQUIRE(static_cast<int>(level::trace)    < static_cast<int>(level::debug));
    REQUIRE(static_cast<int>(level::debug)    < static_cast<int>(level::info));
    REQUIRE(static_cast<int>(level::info)     < static_cast<int>(level::warn));
    REQUIRE(static_cast<int>(level::warn)     < static_cast<int>(level::error));
    REQUIRE(static_cast<int>(level::error)    < static_cast<int>(level::critical));
    REQUIRE(static_cast<int>(level::critical) < static_cast<int>(level::off));
}

TEST_CASE("level_to_string converts all levels correctly [level]") {
    // 注意：level_to_string 返回 const char*，需转为 std::string 做内容比较
    REQUIRE(std::string(level_to_string(level::trace))    == "trace");
    REQUIRE(std::string(level_to_string(level::debug))    == "debug");
    REQUIRE(std::string(level_to_string(level::info))     == "info");
    REQUIRE(std::string(level_to_string(level::warn))     == "warn");
    REQUIRE(std::string(level_to_string(level::error))    == "error");
    REQUIRE(std::string(level_to_string(level::critical)) == "critical");
    REQUIRE(std::string(level_to_string(level::off))      == "off");
}

TEST_CASE("level_to_string_view matches level_to_string [level]") {
    REQUIRE(level_to_string_view(level::info) == "info");
    REQUIRE(level_to_string_view(level::critical).size() == 8);
    REQUIRE(level_to_string_view(static_cast<level>(99)) == "unknown");
}

TEST_CASE("level_to_short_string converts all levels correctly [level]") {
    REQUIRE(std::string(level_to_short_string(level::trace))    == "T");
    REQUIRE(std::string(level_to_short_string(level::debug))    == "D");
    REQUIRE(std::string(level_to_short_string(level::info))     == "I");
    REQUIRE(std::string(level_to_short_string(level::warn))     == "W");
    REQUIRE(std::string(level_to_short_string(level::error))    == "E");
    REQUIRE(std::string(level_to_short_string(level::critical)) == "C");
    REQUIRE(std::string(level_to_short_string(level::off))      == "O");
}

TEST_CASE("string_to_level parses lowercase correctly [level]") {
    REQUIRE(string_to_level("trace")    == level::trace);
    REQUIRE(string_to_level("debug")    == level::debug);
    REQUIRE(string_to_level("info")     == level::info);
    REQUIRE(string_to_level("warn")     == level::warn);
    REQUIRE(string_to_level("error")    == level::error);
    REQUIRE(string_to_level("critical") == level::critical);
    REQUIRE(string_to_level("off")      == level::off);
}

TEST_CASE("string_to_level parses uppercase correctly [level]") {
    // 级别解析应该不区分大小写（常见配置场景）
    REQUIRE(string_to_level("TRACE")    == level::trace);
    REQUIRE(string_to_level("DEBUG")    == level::debug);
    REQUIRE(string_to_level("INFO")     == level::info);
    REQUIRE(string_to_level("WARN")     == level::warn);
    REQUIRE(string_to_level("ERROR")    == level::error);
    REQUIRE(string_to_level("CRITICAL") == level::critical);
    REQUIRE(string_to_level("OFF")      == level::off);
}

TEST_CASE("string_to_level parses mixed case correctly [level]") {
    REQUIRE(string_to_level("Trace")    == level::trace);
    REQUIRE(string_to_level("Debug")    == level::debug);
    REQUIRE(string_to_level("Info")     == level::info);
    REQUIRE(string_to_level("Warn")     == level::warn);
    REQUIRE(string_to_level("Error")    == level::error);
    REQUIRE(string_to_level("Critical") == level::critical);
    REQUIRE(string_to_level("Off")      == level::off);
}

TEST_CASE("string_to_level returns info for unknown strings [level]") {
    // 未知字符串应该返回默认值 info（安全降级策略）
    REQUIRE(string_to_level("unknown")       == level::info);
    REQUIRE(string_to_level("")              == level::info);
    REQUIRE(string_to_level("not_a_level")   == level::info);
    REQUIRE(string_to_level("123")           == level::info);
    REQUIRE(string_to_level("traces")        == level::info);  // 注意：不是 "trace"
}

TEST_CASE("should_log respects level ordering [level][filtering]") {
    // 当 logger 级别设置为 info 时，只有 >= info 的消息才应该输出
    const auto logger_level = level::info;

    REQUIRE(should_log(logger_level, level::trace)    == false);
    REQUIRE(should_log(logger_level, level::debug)    == false);
    REQUIRE(should_log(logger_level, level::info)     == true);   // 边界：等于应该通过
    REQUIRE(should_log(logger_level, level::warn)     == true);
    REQUIRE(should_log(logger_level, level::error)    == true);
    REQUIRE(should_log(logger_level, level::critical) == true);
    REQUIRE(should_log(logger_level, level::off)      == true);   // off 值最大，>= info
}

TEST_CASE("should_log with trace level allows all [level][filtering]") {
    const auto logger_level = level::trace;
    REQUIRE(should_log(logger_level, level::trace)    == true);
    REQUIRE(should_log(logger_level, level::debug)    == true);
    REQUIRE(should_log(logger_level, level::info)     == true);
    REQUIRE(should_log(logger_level, level::warn)     == true);
    REQUIRE(should_log(logger_level, level::error)    == true);
    REQUIRE(should_log(logger_level, level::critical) == true);
}

TEST_CASE("should_log with off level blocks all [level][filtering]") {
    const auto logger_level = level::off;
    REQUIRE(should_log(logger_level, level::trace)    == false);
    REQUIRE(should_log(logger_level, level::debug)    == false);
    REQUIRE(should_log(logger_level, level::info)     == false);
    REQUIRE(should_log(logger_level, level::warn)     == false);
    REQUIRE(should_log(logger_level, level::error)    == false);
    REQUIRE(should_log(logger_level, level::critical) == false);
    REQUIRE(should_log(logger_level, level::off)      == true);  // off >= off
}

TEST_CASE("should_log with critical level only allows critical and off [level][filtering]") {
    const auto logger_level = level::critical;
    REQUIRE(should_log(logger_level, level::trace)    == false);
    REQUIRE(should_log(logger_level, level::debug)    == false);
    REQUIRE(should_log(logger_level, level::info)     == false);
    REQUIRE(should_log(logger_level, level::warn)     == false);
    REQUIRE(should_log(logger_level, level::error)    == false);
    REQUIRE(should_log(logger_level, level::critical) == true);
    REQUIRE(should_log(logger_level, level::off)      == true);
}

TEST_CASE("should_log is symmetric with same level [level][filtering]") {
    // 同一级别应该始终通过
    REQUIRE(should_log(level::trace,    level::trace)    == true);
    REQUIRE(should_log(level::debug,    level::debug)    == true);
    REQUIRE(should_log(level::info,     level::info)     == true);
    REQUIRE(should_log(level::warn,     level::warn)     == true);
    REQUIRE(should_log(level::error,    level::error)    == true);
    REQUIRE(should_log(level::critical, level::critical) == true);
    REQUIRE(should_log(level::off,      level::off)      == true);
}
