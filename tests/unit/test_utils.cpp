#include "framework/doctest.h"
#include "minispdlog/details/utils.h"
#include <thread>
#include <sstream>
#include <iomanip>
#include <cmath>

using namespace minispdlog;
using namespace minispdlog::details;

TEST_CASE("format_time default format [utils][time]") {
    auto now = log_clock::now();
    auto str = format_time(now);
    CHECK(str.size() == 19);
    CHECK(str[4] == '-');
    CHECK(str[7] == '-');
    CHECK(str[10] == ' ');
    CHECK(str[13] == ':');
    CHECK(str[16] == ':');
}
TEST_CASE("format_time custom format [utils][time]") {
    auto now = log_clock::now();
    auto str = format_time(now, "%H:%M:%S");
    // 只输出时分秒，长度 8
    CHECK(str.size() == 8);
    CHECK(str[2] == ':');
    CHECK(str[5] == ':');
}

TEST_CASE("format_time roundtrip [utils][time]") {
    // 策略 C：往返校验
    // 用当前时间 -> format -> 解析回 time_t -> 验证差值 < 1s
    auto now = log_clock::now();
    auto str = format_time(now, "%Y-%m-%d %H:%M:%S");

    std::tm tm_parsed = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm_parsed, "%Y-%m-%d %H:%M:%S");
    REQUIRE(iss.fail() == false);

    auto parsed_time_t = std::mktime(&tm_parsed);
    auto original_time_t = log_clock::to_time_t(now);
    auto diff = std::abs(std::difftime(parsed_time_t, original_time_t));
    CHECK(diff < 1.0);  // 误差 < 1 秒
}

TEST_CASE("get_timestamp_ms is positive and monotonic [utils][time]") {
    auto t1 = get_timestamp_ms();
    auto t2 = get_timestamp_ms();
    CHECK(t1 > 0);
    CHECK(t2 >= t1);
}

TEST_CASE("get_timestamp_ms magnitude [utils][time]") {
    // 2024-2033 年合理范围: 1.7e12 ~ 2.0e12
    auto ts = get_timestamp_ms();
    CHECK(ts > 1'700'000'000'000L);
    CHECK(ts < 2'000'000'000'000L);
}

TEST_CASE("get_thread_id is non-zero [utils][thread]") {
    CHECK(get_thread_id() != 0);
}

TEST_CASE("get_thread_id differs across threads [utils][thread]") {
    size_t main_id = get_thread_id();
    size_t other_id = 0;
    std::thread t([&other_id]() {
        other_id = get_thread_id();
    });
    t.join();
    CHECK(other_id != 0);
    CHECK(other_id != main_id);
}

TEST_CASE("get_pid is stable for the process [utils][thread]") {
    const auto a = get_pid();
    const auto b = get_pid();
    CHECK(a != 0);
    CHECK(a == b);
}

TEST_CASE("ltrim removes leading spaces [utils][string]") {
    std::string s = "  hello";
    auto& ref = ltrim(s);
    CHECK(s == "hello");
    CHECK(&ref == &s);  // 返回原引用
}

TEST_CASE("ltrim no leading spaces keeps unchanged [utils][string]") {
    std::string s = "hello";
    ltrim(s);
    CHECK(s == "hello");
}

TEST_CASE("ltrim all spaces becomes empty [utils][string]") {
    std::string s = "   ";
    ltrim(s);
    CHECK(s == "");
}

TEST_CASE("ltrim empty string stays empty [utils][string]") {
    std::string s = "";
    ltrim(s);
    CHECK(s == "");
}

TEST_CASE("rtrim removes trailing spaces [utils][string]") {
    std::string s = "hello  ";
    auto& ref = rtrim(s);
    CHECK(s == "hello");
    CHECK(&ref == &s);
}

TEST_CASE("rtrim no trailing spaces keeps unchanged [utils][string]") {
    std::string s = "hello";
    rtrim(s);
    CHECK(s == "hello");
}

TEST_CASE("rtrim all spaces becomes empty [utils][string]") {
    std::string s = "   ";
    rtrim(s);
    CHECK(s == "");
}

TEST_CASE("trim removes both leading and trailing spaces [utils][string]") {
    std::string s = "  hello  ";
    auto& ref = trim(s);
    CHECK(s == "hello");
    CHECK(&ref == &s);
}

TEST_CASE("trim no spaces keeps unchanged [utils][string]") {
    std::string s = "hello";
    trim(s);
    CHECK(s == "hello");
}

TEST_CASE("trim all spaces becomes empty [utils][string]") {
    std::string s = "     ";
    trim(s);
    CHECK(s == "");
}

TEST_CASE("trim empty string stays empty [utils][string]") {
    std::string s = "";
    trim(s);
    CHECK(s == "");
}

TEST_CASE("trim handles tab and newline characters [utils][string]") {
    std::string s = "\t\n hello \n\t";
    trim(s);
    CHECK(s == "hello");
}

TEST_CASE("trim mixed whitespace types [utils][string]") {
    std::string s = " \t\r\n foo bar \n\t\r ";
    trim(s);
    CHECK(s == "foo bar");
}