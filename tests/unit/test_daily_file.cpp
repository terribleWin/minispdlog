#include "framework/doctest.h"
#include "framework/test_fixture.h"
#include "minispdlog/minispdlog.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

using namespace minispdlog;
using minispdlog::tests::test_fixture;

namespace {

std::tm local_tm_of(log_clock::time_point tp) {
    const auto time = log_clock::to_time_t(tp);
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &time);
#else
    localtime_r(&time, &out);
#endif
    return out;
}

details::log_msg make_msg(log_clock::time_point tp, string_view_t payload) {
    return details::log_msg(tp, details::source_loc{}, "daily", level::info, payload);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool file_exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// Times far enough ahead that they cross the sink's next local midnight.
log_clock::time_point next_calendar_day(int days_ahead) {
    return log_clock::now() + std::chrono::hours(24 * days_ahead + 12);
}

} // namespace

TEST_CASE("daily calc_filename inserts YYYY-MM-DD before extension [sink][daily]") {
    REQUIRE(sinks::daily_file_sink_mt::calc_filename("logs/app.log", 2026, 9, 5) == "logs/app.2026-09-05.log");
    REQUIRE(sinks::daily_file_sink_st::calc_filename("logs/mylog", 2026, 1, 2) == "logs/mylog.2026-01-02");
}

TEST_CASE("daily calc_filename uses tm fields [sink][daily]") {
    std::tm date{};
    date.tm_year = 2026 - 1900;
    date.tm_mon = 8;
    date.tm_mday = 5;
    REQUIRE(sinks::daily_file_sink_mt::calc_filename("app.txt", date) == "app.2026-09-05.txt");
}

TEST_CASE("daily_file_sink rejects invalid rotation time [sink][daily]") {
    test_fixture fx;
    const auto path = fx.temp_path("bad.log").string();
    REQUIRE_THROWS_AS(sinks::daily_file_sink_mt(path, 24, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(sinks::daily_file_sink_mt(path, 0, 60), std::invalid_argument);
    REQUIRE_THROWS_AS(sinks::daily_file_sink_mt("", 0, 0), std::invalid_argument);
}

TEST_CASE("daily_file_sink writes today's dated file [sink][daily]") {
    test_fixture fx;
    const auto base = fx.temp_path("app.log").string();
    auto sink = std::make_shared<sinks::daily_file_sink_mt>(base);
    sink->set_pattern("%v");

    const auto now = log_clock::now();
    sink->log(make_msg(now, "today-line"));
    sink->flush();

    const auto today = sinks::daily_file_sink_mt::calc_filename(base, local_tm_of(now));
    REQUIRE(file_exists(today));
    REQUIRE(sink->filename() == today);
    REQUIRE(read_file(today).find("today-line") != std::string::npos);
}

TEST_CASE("same-day messages do not open a second file [sink][daily]") {
    test_fixture fx;
    const auto base = fx.temp_path("app.log").string();
    auto sink = std::make_shared<sinks::daily_file_sink_mt>(base);
    sink->set_pattern("%v");

    const auto now = log_clock::now();
    sink->log(make_msg(now, "one"));
    sink->log(make_msg(now + std::chrono::minutes(1), "two"));
    sink->flush();

    const auto today = sinks::daily_file_sink_mt::calc_filename(base, local_tm_of(now));
    REQUIRE(file_exists(today));
    const auto text = read_file(today);
    REQUIRE(text.find("one") != std::string::npos);
    REQUIRE(text.find("two") != std::string::npos);
}

TEST_CASE("daily_file_sink rotates when message crosses the day boundary [sink][daily]") {
    test_fixture fx;
    const auto base = fx.temp_path("app.log").string();
    auto sink = std::make_shared<sinks::daily_file_sink_mt>(base, 0, 0);
    sink->set_pattern("%v");

    const auto day1 = next_calendar_day(1);
    const auto day2 = next_calendar_day(2);
    sink->log(make_msg(day1, "first-day"));
    sink->log(make_msg(day2, "second-day"));
    sink->flush();

    const auto file1 = sinks::daily_file_sink_mt::calc_filename(base, local_tm_of(day1));
    const auto file2 = sinks::daily_file_sink_mt::calc_filename(base, local_tm_of(day2));
    REQUIRE(file1 != file2);
    REQUIRE(file_exists(file1));
    REQUIRE(file_exists(file2));
    REQUIRE(read_file(file1).find("first-day") != std::string::npos);
    REQUIRE(read_file(file2).find("second-day") != std::string::npos);
}

TEST_CASE("daily max_files deletes dated files older than the window [sink][daily]") {
    test_fixture fx;
    const auto base = fx.temp_path("app.log").string();
    auto sink = std::make_shared<sinks::daily_file_sink_mt>(base, 0, 0, false, 2);
    sink->set_pattern("%v");

    const auto d1 = next_calendar_day(1);
    const auto d2 = next_calendar_day(2);
    const auto d3 = next_calendar_day(3);
    sink->log(make_msg(d1, "d1"));
    sink->log(make_msg(d2, "d2"));
    sink->log(make_msg(d3, "d3"));
    sink->flush();

    const auto file1 = sinks::daily_file_sink_mt::calc_filename(base, local_tm_of(d1));
    const auto file2 = sinks::daily_file_sink_mt::calc_filename(base, local_tm_of(d2));
    const auto file3 = sinks::daily_file_sink_mt::calc_filename(base, local_tm_of(d3));
    REQUIRE_FALSE(file_exists(file1));
    REQUIRE(file_exists(file2));
    REQUIRE(file_exists(file3));
}

TEST_CASE("daily_logger_mt factory registers and writes [sink][daily][registry]") {
    test_fixture fx;
    const auto base = fx.temp_path("factory.log").string();
    auto lg = daily_logger_mt("daily-factory", base);
    lg->set_pattern("%v");
    lg->info("from-factory");
    lg->flush();

    const auto today = sinks::daily_file_sink_mt::calc_filename(base, local_tm_of(log_clock::now()));
    REQUIRE(get("daily-factory") != nullptr);
    REQUIRE(file_exists(today));
    REQUIRE(read_file(today).find("from-factory") != std::string::npos);
    drop("daily-factory");
}
