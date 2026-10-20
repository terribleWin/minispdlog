#include "framework/doctest.h"
#include "framework/mock_sink.h"
#include "minispdlog/minispdlog.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace minispdlog;
using minispdlog::tests::mock_sink_mt;

namespace {

/** @brief 在第一条日志上暂停，用于观测 logger 的 Sink 快照语义。 */
class snapshot_gate_sink : public sinks::base_sink<std::mutex> {
public:
    /** @brief 等待日志线程进入 Sink。 */
    [[nodiscard]] bool wait_until_entered(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [this] { return entered_; });
    }

    /** @brief 释放已暂停的日志线程。 */
    void release() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            released_ = true;
        }
        cv_.notify_all();
    }

protected:
    void sink_it_(const details::log_msg&) override {
        std::unique_lock<std::mutex> lock(mu_);
        entered_ = true;
        cv_.notify_all();
        cv_.wait(lock, [this] { return released_; });
    }

    void flush_() override {}

private:
    std::mutex mu_;
    std::condition_variable cv_;
    bool entered_{false};
    bool released_{false};
};

} // namespace

/** @file test_logger.cpp @brief 覆盖 logger 的格式化、过滤、刷新与并发 Sink 快照。 */

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

    lg.warn("warning");                        ///< 达到 info，但尚未达到 error。
    REQUIRE(mock_info->message_count() == 1);  ///< info sink 接收
    REQUIRE(mock_error->message_count() == 0); ///< error sink 不接收
    lg.error("error");                         ///< 同时达到 info 与 error。
    REQUIRE(mock_info->message_count() == 2);  ///< 两个 sink 都接收
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
        std::unique_ptr<formatter> clone() const override { return std::make_unique<prefix_formatter>(); }
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

    lg.info("no auto flush");
    REQUIRE(mock->flush_count() == 0);

    lg.error("auto flush");
    REQUIRE(mock->flush_count() == 1);
    lg.critical("auto flush again");
    REQUIRE(mock->flush_count() == 2);
}

TEST_CASE("logger manual flush [logger][flush]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("ManualFlush", mock);
    lg.info("before flush");
    REQUIRE_NOTHROW(lg.flush());
    REQUIRE(mock->flush_count() == 1);
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
    for (auto& t : threads)
        t.join();

    /// 多线程安全：所有消息都应该被捕获（mock_sink_mt 有 mutex 保护）
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
            /// 两个级别都接受 info，因此主 Sink 应收到全部日志。
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

TEST_CASE("logger in-flight write keeps its copy-on-write sink snapshot [logger][thread]") {
    auto gate = std::make_shared<snapshot_gate_sink>();
    auto captured = std::make_shared<mock_sink_mt>();
    logger lg("CowSnapshot", logger::sink_list{gate, captured});

    std::thread writer([&lg] { lg.info("in-flight"); });
    const bool entered = gate->wait_until_entered(std::chrono::seconds(1));
    if (entered) {
        lg.remove_sink(captured);
    }
    gate->release();
    writer.join();

    REQUIRE(entered);
    REQUIRE(captured->message_count() == 1);
    REQUIRE(captured->last_contains("in-flight"));
    REQUIRE(lg.sinks().size() == 1);

    lg.info("after removal");
    REQUIRE(captured->message_count() == 1);
}

TEST_CASE("logger should_log respects logger level [logger][level]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("ShouldLogTest", mock);
    lg.set_level(level::warn);

    REQUIRE(lg.should_log(level::trace) == false);
    REQUIRE(lg.should_log(level::debug) == false);
    REQUIRE(lg.should_log(level::info) == false);
    REQUIRE(lg.should_log(level::warn) == true);
    REQUIRE(lg.should_log(level::error) == true);
    REQUIRE(lg.should_log(level::critical) == true);
}

TEST_CASE("logger member functions remain runtime filtered [logger][level]") {
    auto mock = std::make_shared<mock_sink_mt>();
    logger lg("CompileTest", mock);

    lg.trace("trace msg");
    lg.debug("debug msg");
    lg.info("info msg");

    /// 成员函数不由编译期宏删除；编译期过滤只作用于 MINISPDLOG_* 宏。
    REQUIRE(mock->message_count() == 3);
}

TEST_CASE("logger macros honor the configured compile-time level [logger][compile]") {
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<logger>("MacroCompileLevel", mock);

    MINISPDLOG_LOGGER_TRACE(lg, "trace msg");
    MINISPDLOG_LOGGER_DEBUG(lg, "debug msg");
    MINISPDLOG_LOGGER_INFO(lg, "info msg");

    std::size_t expected = 0;
#if MINISPDLOG_ACTIVE_LEVEL <= MINISPDLOG_LEVEL_TRACE
    ++expected;
#endif
#if MINISPDLOG_ACTIVE_LEVEL <= MINISPDLOG_LEVEL_DEBUG
    ++expected;
#endif
#if MINISPDLOG_ACTIVE_LEVEL <= MINISPDLOG_LEVEL_INFO
    ++expected;
#endif
    REQUIRE(mock->message_count() == expected);
}
