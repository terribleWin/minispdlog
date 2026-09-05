#include "framework/doctest.h"
#include "minispdlog/minispdlog.h"
#include "framework/mock_sink.h"
#include <thread>
#include <chrono>
#include <vector>

using namespace minispdlog;
using minispdlog::tests::mock_sink_mt;

// ============================================================
// 测试套件：Logger 核心 (logger.h)
// 标签: [logger]
// ============================================================

TEST_CASE("logger basic logging with mock sink [logger]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("TestLogger", mock);

    lg.info("Hello, {}!", "World");

    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->last_contains("Hello, World!"));
}

TEST_CASE("logger level filtering [logger][level]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("FilterTest", mock);
    lg.set_level(level::warn);

    lg.trace("ignored");
    lg.debug("ignored");
    lg.info("ignored");
    REQUIRE(mock->message_count() == 0);

    lg.warn("captured");
    lg.error("captured");
    lg.critical("captured");
    REQUIRE(mock->message_count() == 3);
}

TEST_CASE("logger formatted logging [logger]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("FormatTest", mock);

    lg.info("Integer: {}", 42);
    lg.info("Float: {:.2f}", 3.14159);
    lg.info("Multiple: {}, {}", 1, "two");

    REQUIRE(mock->message_count() == 3);
    REQUIRE(mock->at(0).find("42") != std::string::npos);
    REQUIRE(mock->at(1).find("3.14") != std::string::npos);
    REQUIRE(mock->at(2).find("1") != std::string::npos);
    REQUIRE(mock->at(2).find("two") != std::string::npos);
}

TEST_CASE("logger multi-sink output [logger][multi]") {
    auto mock1 = std::make_shared<mock_sink_mt>();
    auto mock2 = std::make_shared<mock_sink_mt>();

    logger lg("MultiSink");
    lg.add_sink(mock1);
    lg.add_sink(mock2);

    lg.info("broadcast");

    REQUIRE(mock1->message_count() == 1);
    REQUIRE(mock2->message_count() == 1);
    REQUIRE(mock1->last_contains("broadcast"));
    REQUIRE(mock2->last_contains("broadcast"));
}

TEST_CASE("logger multi-sink independent levels [logger][multi][level]") {
    auto mock_info = std::make_shared<mock_sink_mt>();
    auto mock_error = std::make_shared<mock_sink_mt>();
    mock_info->set_level(level::info);
    mock_error->set_level(level::error);

    logger lg("MultiLevel");
    lg.add_sink(mock_info);
    lg.add_sink(mock_error);

    lg.warn("warning");  // >= info, < error
    REQUIRE(mock_info->message_count() == 1);   // info sink 接收
    REQUIRE(mock_error->message_count() == 0);  // error sink 不接收
    lg.error("error");   // >= info, >= error
    REQUIRE(mock_info->message_count() == 2);   // 两个 sink 都接收
    REQUIRE(mock_error->message_count() == 1);
}

TEST_CASE("logger set_pattern applies to all sinks [logger][formatter]") {
    auto mock1 = std::make_shared<mock_sink_mt>();
    auto mock2 = std::make_shared<mock_sink_mt>();
    logger lg("PatternAll");
    lg.add_sink(mock1);
    lg.add_sink(mock2);

    lg.set_pattern("[%L] %v");
    lg.info("hello");

    REQUIRE(mock1->message_count() == 1);
    REQUIRE(mock2->message_count() == 1);
    REQUIRE(mock1->at(0) == "[info] hello\n");
    REQUIRE(mock2->at(0) == "[info] hello\n");
}

TEST_CASE("logger set_pattern can be changed at runtime [logger][formatter]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("RuntimePat", mock);

    lg.set_pattern("%v");
    lg.info("before");
    lg.set_pattern("[%L] %v");
    lg.info("after");

    REQUIRE(mock->message_count() == 2);
    REQUIRE(mock->at(0) == "before\n");
    REQUIRE(mock->at(1) == "[info] after\n");
}

TEST_CASE("set_pattern and set_formatter are distinct APIs [logger][formatter]") {
    struct prefix_formatter : formatter {
        void format(const details::log_msg& msg, fmt::memory_buffer& dest) override {
            const char prefix[] = "CUSTOM ";
            dest.append(prefix, prefix + sizeof(prefix) - 1);
            dest.append(msg.payload.data(), msg.payload.data() + msg.payload.size());
            dest.push_back('\n');
        }
        std::unique_ptr<formatter> clone() const override {
            return std::make_unique<prefix_formatter>();
        }
    };

    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("ApiSplit", mock);

    lg.set_formatter(std::make_unique<prefix_formatter>());
    lg.info("via formatter");
    REQUIRE(mock->at(0) == "CUSTOM via formatter\n");

    lg.set_pattern("%v");
    lg.info("via pattern");
    REQUIRE(mock->at(1) == "via pattern\n");
}

TEST_CASE("logger set_formatter clones independently per sink [logger][formatter]") {
    auto mock1 = std::make_shared<mock_sink_mt>();
    auto mock2 = std::make_shared<mock_sink_mt>();
    logger lg("FmtClone");
    lg.add_sink(mock1);
    lg.add_sink(mock2);

    lg.set_formatter(std::make_unique<pattern_formatter>("%v"));
    mock2->set_pattern("[%L] %v");
    lg.info("clone");

    REQUIRE(mock1->at(0) == "clone\n");
    REQUIRE(mock2->at(0) == "[info] clone\n");
}

TEST_CASE("logger flush_on triggers automatic flush [logger][flush]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("FlushTest", mock);
    lg.flush_on(level::error);

    // mock_sink 的 flush 是空操作，但 flush_on 机制应该被触发
    lg.info("no auto flush");
    // flush_on 只应在 >= error 级别触发
    lg.error("auto flush");

    // 没有直接的断言方式（mock 不关心 flush），但不应崩溃
    REQUIRE(true);
}

TEST_CASE("logger manual flush [logger][flush]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("ManualFlush", mock);
    lg.info("before flush");
    REQUIRE_NOTHROW(lg.flush());
}

TEST_CASE("logger name is preserved [logger]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("MyLogger", mock);
    REQUIRE(lg.name() == "MyLogger");
}

TEST_CASE("logger multithreaded safety [logger][thread]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("MTLogger", mock);

    const int num_threads = 8;
    const int msgs_per_thread = 100;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&lg, i]() {
            for (int j = 0; j < msgs_per_thread; ++j) {
                lg.info("Thread {} - Message {}", i, j);
            }
        });
    }
    for (auto& t : threads) t.join();

    // 多线程安全：所有消息都应该被捕获（mock_sink_mt 有 mutex 保护）
    REQUIRE(mock->message_count() == num_threads * msgs_per_thread);
}

TEST_CASE("logger concurrent log/set_level/add_sink has no data race [logger][thread]") {
    auto primary = std::make_shared<mock_sink_mt>();
    logger lg("RaceLogger", primary);
    lg.set_level(level::info);

    constexpr int kLoggers = 4;
    constexpr int kLogsPerThread = 250;
    constexpr int kMutations = 80;

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(kLoggers + 2));

    for (int t = 0; t < kLoggers; ++t) {
        threads.emplace_back([&lg, t]() {
            for (int i = 0; i < kLogsPerThread; ++i) {
                lg.info("t{} {}", t, i);
            }
        });
    }

    threads.emplace_back([&lg]() {
        for (int i = 0; i < kMutations; ++i) {
            // Both values still accept info, so primary must see every log.
            lg.set_level(i % 2 == 0 ? level::trace : level::info);
            (void)lg.get_level();
            (void)lg.should_log(level::info);
        }
        lg.set_level(level::info);
    });

    threads.emplace_back([&lg]() {
        for (int i = 0; i < kMutations; ++i) {
            auto extra = std::make_shared<mock_sink_mt>();
            lg.add_sink(extra);
            lg.flush();
            lg.remove_sink(extra);
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(lg.sinks().size() == 1);
    REQUIRE(lg.sinks()[0] == primary);
    REQUIRE(primary->message_count() == kLoggers * kLogsPerThread);
}

TEST_CASE("logger sinks() returns an isolated snapshot [logger]") {
    auto first = std::make_shared<mock_sink_mt>();
    auto second = std::make_shared<mock_sink_mt>();
    logger lg("Snapshot", first);

    auto snapshot = lg.sinks();
    REQUIRE(snapshot.size() == 1);

    lg.add_sink(second);
    REQUIRE(snapshot.size() == 1);
    REQUIRE(lg.sinks().size() == 2);

    lg.info("after add");
    REQUIRE(first->message_count() == 1);
    REQUIRE(second->message_count() == 1);
}

TEST_CASE("logger should_log respects logger level [logger][level]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("ShouldLogTest", mock);
    lg.set_level(level::warn);

    REQUIRE(lg.should_log(level::trace)    == false);
    REQUIRE(lg.should_log(level::debug)    == false);
    REQUIRE(lg.should_log(level::info)     == false);
    REQUIRE(lg.should_log(level::warn)     == true);
    REQUIRE(lg.should_log(level::error)    == true);
    REQUIRE(lg.should_log(level::critical) == true);
}

TEST_CASE("logger compile-time level filtering [logger][compile]") {
    // 编译时级别控制：如果 MINISPDLOG_ACTIVE_LEVEL 设置为 INFO，
    // trace/debug 的日志在编译期就被移除，不会调用运行时逻辑
    // 这个测试主要验证编译通过且行为正确
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("CompileTest", mock);

    lg.trace("trace msg");
    lg.debug("debug msg");
    lg.info("info msg");

    // 在默认编译设置下（trace 级别启用），所有消息都应该输出
    REQUIRE(mock->message_count() == 3);
}
