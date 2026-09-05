#include "framework/doctest.h"
#include "minispdlog/logger.h"
#include "minispdlog/pattern_formatter.h"
#include "minispdlog/sinks/callback_sink.h"

#include <mutex>
#include <string>
#include <vector>

using namespace minispdlog;
using minispdlog::sinks::callback_sink_mt;

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
    auto sink = std::make_shared<callback_sink_mt>([&](const std::string& formatted) {
        lines.push_back(formatted);
    });
    sink->set_formatter(std::make_unique<pattern_formatter>("[%L] %v"));

    logger lg("multi_cb", sink);
    lg.warn("attention");

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0].find("warn") != std::string::npos);
    REQUIRE(lines[0].find("attention") != std::string::npos);
}

TEST_CASE("callback_sink rejects empty callback [sink][callback]") {
    REQUIRE_THROWS_AS(
        std::make_shared<callback_sink_mt>(callback_sink_mt::callback_t{}),
        std::invalid_argument);
}
