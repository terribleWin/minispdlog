#include "framework/doctest.h"
#include "framework/mock_sink.h"
#include "minispdlog/async.h"
#include "minispdlog/logger.h"
#include "minispdlog/minispdlog.h"
#include "minispdlog/pattern_formatter.h"
#include "minispdlog/sinks/callback_sink.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace minispdlog;
using minispdlog::sinks::callback_sink_mt;
using minispdlog::sinks::callback_sink_st;
using minispdlog::tests::mock_sink_mt;

TEST_CASE("callback_sink delivers formatted line [sink][callback][gui]") {
    std::vector<std::string> lines;
    std::mutex lines_mu;

    auto sink = std::make_shared<callback_sink_mt>([&](const std::string& formatted) {
        std::lock_guard<std::mutex> lock(lines_mu);
        lines.push_back(formatted);
    });
    sink->set_formatter(std::make_unique<pattern_formatter>("%v"));

    logger lg("cb", sink);
    lg.info("hello gui");

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == "hello gui\n");
}

TEST_CASE("callback_sink works with multi-sink logger [sink][callback][gui]") {
    std::vector<std::string> lines;
    auto cb = std::make_shared<callback_sink_mt>([&](const std::string& formatted) {
        lines.push_back(formatted);
    });
    cb->set_formatter(std::make_unique<pattern_formatter>("[%L] %v"));
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_formatter(std::make_unique<pattern_formatter>("[%L] %v"));

    logger lg("multi_cb", logger::sink_list{mock, cb});
    lg.warn("attention");

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0].find("warn") != std::string::npos);
    REQUIRE(lines[0].find("attention") != std::string::npos);
    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->last_contains("attention"));
}

TEST_CASE("callback_sink rejects empty callback [sink][callback]") {
    REQUIRE_THROWS_AS(callback_sink_mt(callback_sink_mt::callback_t{}), std::invalid_argument);
    REQUIRE_THROWS_AS(callback_sink_mt(callback_sink_mt::record_callback_t{}), std::invalid_argument);
    REQUIRE_THROWS_AS(callback_sink_st(callback_sink_st::formatted_callback_t{}), std::invalid_argument);
}

TEST_CASE("callback_sink record callback exposes log_msg fields [sink][callback]") {
    struct captured {
        std::string logger_name;
        level lvl{};
        std::string payload;
        std::string formatted;
    } rec;

    auto sink = std::make_shared<callback_sink_st>(
        [&](const details::log_msg& msg, const std::string& formatted) {
            rec.logger_name.assign(msg.logger_name.data(), msg.logger_name.size());
            rec.lvl = msg.lvl;
            rec.payload.assign(msg.payload.data(), msg.payload.size());
            rec.formatted = formatted;
        });
    sink->set_formatter(std::make_unique<pattern_formatter>("[%L] %v"));

    logger lg("metrics", sink);
    lg.error("disk full");

    REQUIRE(rec.logger_name == "metrics");
    REQUIRE(rec.lvl == level::error);
    REQUIRE(rec.payload == "disk full");
    REQUIRE(rec.formatted.find("error") != std::string::npos);
    REQUIRE(rec.formatted.find("disk full") != std::string::npos);
}

TEST_CASE("callback_sink flush callback runs on flush [sink][callback]") {
    std::atomic<int> logs{0};
    std::atomic<int> flushes{0};

    auto sink = std::make_shared<callback_sink_mt>(
        [&](const std::string&) { logs.fetch_add(1, std::memory_order_relaxed); },
        [&]() { flushes.fetch_add(1, std::memory_order_relaxed); });

    logger lg("flush-cb", sink);
    lg.info("one");
    lg.info("two");
    REQUIRE(flushes.load() == 0);
    lg.flush();
    REQUIRE(logs.load() == 2);
    REQUIRE(flushes.load() == 1);

    sink->set_flush_callback([&]() { flushes.fetch_add(10, std::memory_order_relaxed); });
    lg.flush();
    REQUIRE(flushes.load() == 11);
}

TEST_CASE("callback_sink flush without hook is a no-op [sink][callback]") {
    auto sink = std::make_shared<callback_sink_st>([](const std::string&) {});
    logger lg("flush-none", sink);
    lg.info("ok");
    lg.flush();
}

TEST_CASE("callback_sink_mt serializes concurrent logs [sink][callback][thread]") {
    std::vector<std::string> lines;
    auto sink = std::make_shared<callback_sink_mt>([&](const std::string& formatted) {
        lines.push_back(formatted);
    });
    sink->set_formatter(std::make_unique<pattern_formatter>("%v"));

    logger lg("cb-conc", sink);
    constexpr int kThreads = 4;
    constexpr int kPerThread = 50;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&lg, t]() {
            for (int i = 0; i < kPerThread; ++i) {
                lg.info("t{}-i{}", t, i);
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    REQUIRE(lines.size() == static_cast<std::size_t>(kThreads * kPerThread));
}

TEST_CASE("callback_logger factories register loggers [sink][callback][registry]") {
    std::vector<std::string> mt_lines;
    std::vector<level> st_levels;

    auto mt = callback_logger_mt("cb-factory-mt", [&](const std::string& line) {
        mt_lines.push_back(line);
    });
    auto st = callback_logger_st(
        "cb-factory-st",
        [&](const details::log_msg& msg, const std::string&) { st_levels.push_back(msg.lvl); });

    REQUIRE(get("cb-factory-mt") != nullptr);
    REQUIRE(get("cb-factory-st") != nullptr);
    mt->warn("factory-mt");
    st->error("factory-st");
    REQUIRE_FALSE(mt_lines.empty());
    REQUIRE(mt_lines.back().find("factory-mt") != std::string::npos);
    REQUIRE(st_levels.size() == 1);
    REQUIRE(st_levels[0] == level::error);

    drop("cb-factory-mt");
    drop("cb-factory-st");
}

TEST_CASE("async_callback_mt delivers after flush [sink][callback][async]") {
    std::mutex mu;
    std::vector<std::string> lines;
    int flushes = 0;

    shutdown();
    init_thread_pool(1024, 1);
    auto lg = async_callback_mt(
        "cb-async",
        [&](const details::log_msg& msg, const std::string& formatted) {
            std::lock_guard<std::mutex> lock(mu);
            REQUIRE(msg.lvl == level::warn);
            lines.push_back(formatted);
        },
        [&]() {
            std::lock_guard<std::mutex> lock(mu);
            ++flushes;
        });
    lg->warn("async-callback");
    lg->flush();
    shutdown();

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0].find("async-callback") != std::string::npos);
    REQUIRE(flushes >= 1);
}
