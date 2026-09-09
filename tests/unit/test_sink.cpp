#include "framework/doctest.h"
#include "framework/mock_sink.h"
#include "framework/test_fixture.h"
#include "minispdlog/minispdlog.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

using namespace minispdlog;
using minispdlog::tests::mock_sink_mt;

// ============================================================
// 测试套件：Sink 系统 (sinks/)
// 标签: [sink]
// ============================================================

TEST_CASE("base sink default level is trace [sink]") {
    auto mock = std::make_shared<mock_sink_mt>();
    REQUIRE(mock->get_level() == level::trace);
    // trace 级别应该允许所有日志
    REQUIRE(mock->should_log(level::trace) == true);
    REQUIRE(mock->should_log(level::debug) == true);
    REQUIRE(mock->should_log(level::info)  == true);
    REQUIRE(mock->should_log(level::error) == true);
}

TEST_CASE("sink level filtering works correctly [sink][level]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_level(level::warn);

    REQUIRE(mock->should_log(level::trace)    == false);
    REQUIRE(mock->should_log(level::debug)    == false);
    REQUIRE(mock->should_log(level::info)     == false);
    REQUIRE(mock->should_log(level::warn)     == true);  // 边界
    REQUIRE(mock->should_log(level::error)    == true);
    REQUIRE(mock->should_log(level::critical) == true);
}

TEST_CASE("sink log captures formatted messages [sink]") {
    auto mock = std::make_shared<mock_sink_mt>();

    details::log_msg msg("TestLogger", level::info, "Hello, test!");
    mock->log(msg);

    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->last_contains("Hello, test!"));
}

TEST_CASE("sink log does not filter at sink level [sink][level]") {
    // 注意：base_sink::log() 不检查 should_log，直接调用 sink_it_()
    // 级别过滤由 logger 层负责。此测试验证 sink 层不过滤的行为。
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_level(level::error);

    details::log_msg info_msg("Test", level::info, "info message");
    details::log_msg err_msg("Test", level::error, "error message");

    mock->log(info_msg);  // sink 层不过滤，仍然会捕获
    mock->log(err_msg);   // 也会捕获

    REQUIRE(mock->message_count() == 2);
    REQUIRE(mock->contains("info message"));
    REQUIRE(mock->contains("error message"));
}

TEST_CASE("sink flush does not throw [sink]") {
    auto mock = std::make_shared<mock_sink_mt>();
    // flush 不应该抛出异常，mock_sink 的 flush_ 是空操作
    REQUIRE_NOTHROW(mock->flush());
}

TEST_CASE("sink set_formatter changes output format [sink][formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    // 默认 formatter 会包含时间戳等信息
    mock->set_formatter(std::make_unique<pattern_formatter>("%v"));  // 只保留消息内容
    details::log_msg msg("Test", level::info, "raw message");
    mock->log(msg);

    REQUIRE(mock->message_count() == 1);
    // 自定义格式只包含 %v + 新行
    REQUIRE(mock->at(0) == "raw message\n");
}

TEST_CASE("sink set_pattern changes output format [sink][formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_pattern("[%L] %v");
    details::log_msg msg("Test", level::info, "patterned");
    mock->log(msg);

    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->at(0) == "[info] patterned\n");
}

TEST_CASE("mock_sink clear removes all messages [sink]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::log_msg msg("Test", level::info, "msg1");
    mock->log(msg);
    REQUIRE(mock->message_count() == 1);

    mock->clear();
    REQUIRE(mock->message_count() == 0);
}

TEST_CASE("mock_sink contains searches all messages [sink]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->log(details::log_msg("Test", level::info, "alpha"));
    mock->log(details::log_msg("Test", level::info, "beta"));
    mock->log(details::log_msg("Test", level::info, "gamma"));

    REQUIRE(mock->contains("alpha") == true);
    REQUIRE(mock->contains("beta")  == true);
    REQUIRE(mock->contains("delta") == false);
}

TEST_CASE("console_sink_mt is thread-safe [sink][thread]") {
    auto console = std::make_shared<sinks::console_sink_mt>();
    console->set_level(level::off);  // 关闭输出，避免测试输出污染
    // 多线程并发写入，不应崩溃（thread-safety 的 smoke test）
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([console, i]() {
            for (int j = 0; j < 100; ++j) {
                details::log_msg msg("MT", level::info, "thread test");
                console->log(msg);
            }
        });
    }
    for (auto& t : threads) t.join();

    // 只要没崩溃就算通过（ASan/TSan 会在 CI 中检测 data race）
    REQUIRE(true);
}

TEST_CASE("stderr_sink_mt only outputs error and above [sink]") {
    auto err_sink = std::make_shared<sinks::stderr_sink_mt>();
    err_sink->set_level(level::error);

    REQUIRE(err_sink->should_log(level::debug) == false);
    REQUIRE(err_sink->should_log(level::error) == true);
    REQUIRE(err_sink->should_log(level::critical) == true);
}

TEST_CASE("null_mutex has no overhead [sink]") {
    // null_mutex 的 lock/unlock 是空操作，用于单线程场景
    sinks::null_mutex nm;
    nm.lock();
    nm.unlock();  // 不应崩溃、不应有副作用
    REQUIRE(true);
}

TEST_CASE("null_sink formats into a byte counter [sink]") {
    auto sink = std::make_shared<sinks::null_sink_st>();
    sink->set_pattern("%v");
    sink->log(details::log_msg("n", level::info, "hello"));
    REQUIRE(sink->bytes_written() == 6);
}

TEST_CASE("file_sink writes LF text [sink][file]") {
    tests::test_fixture fx;
    const auto path = fx.temp_path("file.log").string();
    auto sink = std::make_shared<sinks::file_sink_st>(path, true);
    sink->set_pattern("%v");
    sink->log(details::log_msg("f", level::info, "line"));
    sink->flush();

    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(text == "line\n");
    REQUIRE(text.find('\r') == std::string::npos);
}

TEST_CASE("file_sink throws on invalid path [sink][file]") {
    // 在 Windows 上尝试写入非法路径，在 Linux 上尝试写入不存在的目录
    // 注意：这个测试的行为可能因平台而异，使用 REQUIRE_THROWS 捕获异常
#ifdef _WIN32
    // Windows 上某些路径会失败
    REQUIRE_THROWS_AS(
        std::make_shared<sinks::file_sink_mt>("\\/\\invalid::path.log"),
        std::runtime_error
    );
#else
    // Linux 上尝试写入一个不可能创建的目录
    REQUIRE_THROWS_AS(
        std::make_shared<sinks::file_sink_mt>("/nonexistent_dir_12345/test.log"),
        std::runtime_error
    );
#endif
}
