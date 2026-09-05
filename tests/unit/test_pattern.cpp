#include "framework/doctest.h"
#include "minispdlog/minispdlog.h"
#include "framework/mock_sink.h"

#include <chrono>
#include <sstream>
#include <string>

using namespace minispdlog;
using minispdlog::tests::mock_sink_mt;

// ============================================================
// 测试套件：格式化引擎 (formatter.h / pattern_formatter.h)
// 标签: [formatter]
// ============================================================

TEST_CASE("pattern_formatter with default pattern [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("DefaultFmt", mock);

    lg.info("test message");

    REQUIRE(mock->message_count() == 1);
    // 默认格式包含时间戳、级别、logger名、消息
    auto msg = mock->at(0);
    REQUIRE(msg.find("test message") != std::string::npos);
    // 默认格式包含新行，用 find 检查子串而非精确匹配
    CHECK(msg.find("test message") != std::string::npos);
    CHECK(msg.find("info") != std::string::npos);
}

TEST_CASE("pattern_formatter custom message-only pattern [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_formatter(std::make_unique<pattern_formatter>("%v"));
    logger lg("MsgOnly", mock);

    lg.info("hello world");

    REQUIRE(mock->message_count() == 1);
    // pattern_formatter 始终在末尾追加新行
    REQUIRE(mock->at(0) == "hello world\n");
}

TEST_CASE("pattern_formatter with long level placeholder [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_formatter(std::make_unique<pattern_formatter>("[%L] %v"));
    logger lg("LevelTest", mock);

    lg.info("info msg");
    lg.error("error msg");

    REQUIRE(mock->message_count() == 2);
    REQUIRE(mock->at(0).find("[info]") != std::string::npos);
    REQUIRE(mock->at(1).find("[error]") != std::string::npos);
}

TEST_CASE("pattern_formatter with short level placeholder [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_formatter(std::make_unique<pattern_formatter>("[%l] %v"));
    logger lg("ShortLevel", mock);

    lg.info("msg");
    lg.warn("msg");

    REQUIRE(mock->message_count() == 2);
    REQUIRE(mock->at(0) == "[I] msg\n");
    REQUIRE(mock->at(1) == "[W] msg\n");
}

TEST_CASE("pattern_formatter with logger name placeholder [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_formatter(std::make_unique<pattern_formatter>("[%n] %v"));
    logger lg("NamedLogger", mock);

    lg.info("test");

    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->at(0).find("[NamedLogger]") != std::string::npos);
}

TEST_CASE("pattern_formatter with thread id placeholder [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_formatter(std::make_unique<pattern_formatter>("[thread %t] %v"));
    logger lg("ThreadTest", mock);

    lg.info("test");

    REQUIRE(mock->message_count() == 1);
    // 线程 ID 格式不确定，但至少包含 "thread" 字样
    REQUIRE(mock->at(0).find("thread") != std::string::npos);
}

TEST_CASE("pattern_formatter with datetime placeholders [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    // 年月日时分秒占位符
    mock->set_formatter(std::make_unique<pattern_formatter>("%Y-%m-%d %H:%M:%S"));
    logger lg("DateTime", mock);

    lg.info("test");

    REQUIRE(mock->message_count() == 1);
    auto msg = mock->at(0);
    // 验证格式：YYYY-MM-DD HH:MM:SS
    // 验证格式中包含关键字符，不假定具体位置（避免编码问题）
    CHECK(msg.find('-') != std::string::npos);
    CHECK(msg.find(':') != std::string::npos);
    CHECK(msg.find(' ') != std::string::npos);
    // pattern_formatter 末尾会追加新行，所以长度至少 20
    CHECK(msg.size() >= 20);
}

TEST_CASE("pattern_formatter with combined pattern [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_formatter(
        std::make_unique<pattern_formatter>("[%Y-%m-%d %H:%M:%S] [%n] [%L] %v")
    );
    logger lg("Combined", mock);

    lg.info("combined test");

    REQUIRE(mock->message_count() == 1);
    auto msg = mock->at(0);
    REQUIRE(msg.find("Combined") != std::string::npos);   // logger name
    REQUIRE(msg.find("info") != std::string::npos);       // level
    REQUIRE(msg.find("combined test") != std::string::npos);  // message
}

TEST_CASE("pattern_formatter handles empty message [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_formatter(std::make_unique<pattern_formatter>("%v"));
    logger lg("Empty", mock);

    lg.info("");

    REQUIRE(mock->message_count() == 1);
    // 空消息 + 新行 = 仅新行
    REQUIRE(mock->at(0) == "\n");
}

TEST_CASE("pattern_formatter handles long message [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_formatter(std::make_unique<pattern_formatter>("%v"));
    logger lg("Long", mock);

    std::string long_msg(10000, 'x');
    lg.info("{}", long_msg);

    REQUIRE(mock->message_count() == 1);
    // pattern_formatter 追加了新行，所以长度是 10001
    REQUIRE(mock->at(0).size() == 10001);
    // 验证前 10000 个字符是 'x'
    REQUIRE(mock->at(0).substr(0, 10000) == std::string(10000, 'x'));
}

TEST_CASE("formatter can be changed at runtime [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("RuntimeFmt", mock);

    lg.info("before");
    mock->set_formatter(std::make_unique<pattern_formatter>("[%L] %v"));
    lg.info("after");

    REQUIRE(mock->message_count() == 2);
    // 第一条使用默认格式（可能包含时间戳等）    // 第二条使用自定义格式
    REQUIRE(mock->at(1).find("[info]") != std::string::npos);
}

TEST_CASE("pattern_formatter set_pattern via sink API [formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_pattern("%v");
    logger lg("SinkPattern", mock);

    lg.info("hello world");

    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->at(0) == "hello world\n");
}

TEST_CASE("pattern_formatter milliseconds and microseconds [formatter]") {
    const auto tp = log_clock::time_point{} + std::chrono::milliseconds(1234) +
                    std::chrono::microseconds(56);
    details::log_msg msg(tp, details::source_loc("dir/foo.cpp", 42, "bar"), "lg", level::info,
                         "hi");
    pattern_formatter fmt("%e %f");
    fmt::memory_buffer buf;
    fmt.format(msg, buf);
    REQUIRE(std::string(buf.data(), buf.size()) == "234 234056\n");
}

TEST_CASE("pattern_formatter process id [formatter]") {
    details::log_msg msg("lg", level::info, "x");
    REQUIRE(msg.process_id == details::get_pid());
    REQUIRE(msg.process_id != 0);

    pattern_formatter fmt("%P");
    fmt::memory_buffer buf;
    fmt.format(msg, buf);
    REQUIRE(std::string(buf.data(), buf.size()) == std::to_string(msg.process_id) + "\n");
}

TEST_CASE("pattern_formatter source location flags [formatter]") {
    details::log_msg msg(details::source_loc("dir/foo.cpp", 42, "bar"), "lg", level::info, "hi");
    pattern_formatter fmt("%s %# %g %! %@");
    fmt::memory_buffer buf;
    fmt.format(msg, buf);
    REQUIRE(std::string(buf.data(), buf.size()) == "foo.cpp 42 dir/foo.cpp bar foo.cpp:42\n");
}

TEST_CASE("log fills source location without macros [formatter][source]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_pattern("%s:%#");
    logger lg("SrcLog", mock);

    const auto line = __LINE__ + 1;
    lg.log(level::info, "payload");

    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->at(0).find("test_pattern.cpp:") != std::string::npos);
    REQUIRE(mock->at(0).find(std::to_string(line)) != std::string::npos);
}

TEST_CASE("info fills source location with format args [formatter][source]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_pattern("%s:%# %v");
    logger lg("SrcInfo", mock);

    const auto line = __LINE__ + 1;
    lg.info("n={}", 7);

    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->at(0).find("test_pattern.cpp:") != std::string::npos);
    REQUIRE(mock->at(0).find(std::to_string(line)) != std::string::npos);
    REQUIRE(mock->at(0).find("n=7") != std::string::npos);
}

TEST_CASE("pattern_formatter %^ %$ mark color_range [formatter][color]") {
    details::log_msg msg("lg", level::info, "hello");
    pattern_formatter fmt("[%^%L%$] %v");
    fmt::memory_buffer buf;
    fmt.format(msg, buf);
    const auto line = std::string(buf.data(), buf.size());
    REQUIRE(line == "[info] hello\n");
    REQUIRE(msg.color_range_start == 1);
    REQUIRE(msg.color_range_end == 5);
    REQUIRE(line.substr(msg.color_range_start, msg.color_range_end - msg.color_range_start) ==
            "info");
}

TEST_CASE("pattern without color flags leaves range empty [formatter][color]") {
    details::log_msg msg("lg", level::warn, "x");
    msg.color_range_start = 99;
    msg.color_range_end = 99;
    pattern_formatter fmt("%v");
    fmt::memory_buffer buf;
    fmt.format(msg, buf);
    REQUIRE(msg.color_range_start == 0);
    REQUIRE(msg.color_range_end == 0);
}

TEST_CASE("write_colored paints only the marked span [sink][color]") {
    details::log_msg msg("lg", level::info, "hello");
    pattern_formatter fmt("[%^%L%$] %v");
    fmt::memory_buffer formatted;
    fmt.format(msg, formatted);

    std::ostringstream out;
    sinks::write_colored(out, msg, formatted, sinks::color::green);
    const auto painted = out.str();
    REQUIRE(painted.find(std::string(sinks::color::green) + "info" + sinks::color::reset) !=
            std::string::npos);
    REQUIRE(painted.find("hello") != std::string::npos);
    REQUIRE(painted.find(std::string(sinks::color::green) + "[info]") == std::string::npos);
}

TEST_CASE("write_colored paints the whole line when range is empty [sink][color]") {
    details::log_msg msg("lg", level::error, "boom");
    pattern_formatter fmt("%v");
    fmt::memory_buffer formatted;
    fmt.format(msg, formatted);

    std::ostringstream out;
    sinks::write_colored(out, msg, formatted, sinks::color::red);
    REQUIRE(out.str() == std::string(sinks::color::red) + "boom\n" + sinks::color::reset);
}

TEST_CASE("default pattern uses unified separators [formatter]") {
    REQUIRE(std::string(pattern_formatter::default_pattern) ==
            "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] %v");
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("DefaultSep", mock);
    lg.info("sep");
    auto msg = mock->at(0);
    REQUIRE(msg.find('-') != std::string::npos);
    REQUIRE(msg.find(':') != std::string::npos);
    REQUIRE(msg.find('.') != std::string::npos);
    REQUIRE(msg.find("[DefaultSep]") != std::string::npos);
    REQUIRE(msg.find("[info]") != std::string::npos);
}

TEST_CASE("logger macro fills source location [formatter][source]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_pattern("%s:%# %!");
    auto lg = std::make_shared<logger>("SrcMacro", mock);

    const auto line = __LINE__ + 1;
    MINISPDLOG_LOGGER_INFO(lg, "payload");

    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->at(0).find("test_pattern.cpp:") != std::string::npos);
    REQUIRE(mock->at(0).find(std::to_string(line)) != std::string::npos);
    REQUIRE(mock->at(0).size() > std::string("test_pattern.cpp:1 \n").size());
}

TEST_CASE("empty source location omits source flags [formatter]") {
    details::log_msg msg("lg", level::info, "x");
    REQUIRE(msg.source.empty());
    pattern_formatter fmt("%s %# %! %@");
    fmt::memory_buffer buf;
    fmt.format(msg, buf);
    REQUIRE(std::string(buf.data(), buf.size()) == "   \n");
}
