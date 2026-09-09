#include "framework/doctest.h"
#include "framework/mock_sink.h"
#include "framework/test_fixture.h"
#include "minispdlog/async.h"
#include "minispdlog/json_formatter.h"
#include "minispdlog/minispdlog.h"
#include "minispdlog/pattern_formatter.h"
#include "minispdlog/sinks/callback_sink.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace minispdlog;
using minispdlog::tests::mock_sink_mt;
using minispdlog::tests::test_fixture;

namespace {

std::string format_json(const details::log_msg& msg) {
    json_formatter fmt;
    fmt::memory_buffer buf;
    fmt.format(msg, buf);
    return {buf.data(), buf.size()};
}

std::string format_json_with(json_formatter& fmt, const details::log_msg& msg) {
    fmt::memory_buffer buf;
    fmt.format(msg, buf);
    return {buf.data(), buf.size()};
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::string> json_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

void require_json_object_line(const std::string& line) {
    REQUIRE_FALSE(line.empty());
    REQUIRE(line.front() == '{');
    REQUIRE(line.back() == '}');
    REQUIRE(line.find("\"time\":") != std::string::npos);
    REQUIRE(line.find("\"ts\":") != std::string::npos);
    REQUIRE(line.find("\"level\":") != std::string::npos);
    REQUIRE(line.find("\"level_num\":") != std::string::npos);
    REQUIRE(line.find("\"logger\":") != std::string::npos);
    REQUIRE(line.find("\"msg\":") != std::string::npos);
    REQUIRE(line.find("\"tid\":") != std::string::npos);
    REQUIRE(line.find("\"pid\":") != std::string::npos);
}

void require_ndjson(const std::string& text, std::size_t expected) {
    const auto lines = json_lines(text);
    REQUIRE(lines.size() == expected);
    for (const auto& line : lines) {
        require_json_object_line(line);
    }
}

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

std::tm utc_tm_of(log_clock::time_point tp) {
    const auto time = log_clock::to_time_t(tp);
    std::tm out{};
#ifdef _WIN32
    gmtime_s(&out, &time);
#else
    gmtime_r(&time, &out);
#endif
    return out;
}

std::string expected_time_field(log_clock::time_point tp) {
    const auto tm = utc_tm_of(tp);
    const auto duration = tp.time_since_epoch();
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(duration);
    const int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(duration - secs).count());
    return fmt::format("\"time\":\"{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z\"",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                       tm.tm_sec, ms);
}

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool is_digits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

std::string json_quoted_field(const std::string& line, std::string_view key) {
    const auto needle = std::string("\"") + std::string(key) + "\":\"";
    const auto start = line.find(needle);
    REQUIRE(start != std::string::npos);
    std::size_t i = start + needle.size();
    std::string value;
    bool escaped = false;
    for (; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) {
            value.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            value.push_back(c);
            escaped = true;
            continue;
        }
        if (c == '"') {
            return value;
        }
        value.push_back(c);
    }
    FAIL("unterminated JSON string field");
    return {};
}

std::string json_number_field(const std::string& line, std::string_view key) {
    const auto needle = std::string("\"") + std::string(key) + "\":";
    const auto start = line.find(needle);
    REQUIRE(start != std::string::npos);
    std::size_t i = start + needle.size();
    if (i < line.size() && line[i] == '-') {
        ++i;
    }
    const auto begin = i;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
        ++i;
    }
    REQUIRE(i > begin);
    return line.substr(start + needle.size(), i - (start + needle.size()));
}

void require_file_record_shape(const std::string& line) {
    require_json_object_line(line);
    const auto time = json_quoted_field(line, "time");
    REQUIRE(time.size() == 24);
    REQUIRE(time[4] == '-');
    REQUIRE(time[7] == '-');
    REQUIRE(time[10] == 'T');
    REQUIRE(time[13] == ':');
    REQUIRE(time[16] == ':');
    REQUIRE(time[19] == '.');
    REQUIRE(time[23] == 'Z');
    REQUIRE(is_digits(time.substr(0, 4)));
    REQUIRE(is_digits(time.substr(20, 3)));
    REQUIRE(is_digits(json_number_field(line, "ts")));
    REQUIRE(is_digits(json_number_field(line, "level_num")));
    REQUIRE(is_digits(json_number_field(line, "tid")));
    REQUIRE(is_digits(json_number_field(line, "pid")));
    REQUIRE_FALSE(json_quoted_field(line, "level").empty());
}

} // namespace

TEST_CASE("json_formatter emits JSON Lines with core fields [formatter][json]") {
    details::log_msg msg("app", level::info, "hello");
    const auto line = format_json(msg);

    REQUIRE(line.back() == '\n');
    require_json_object_line(line.substr(0, line.size() - 1));
    REQUIRE(line.find("\"level\":\"info\"") != std::string::npos);
    REQUIRE(line.find("\"logger\":\"app\"") != std::string::npos);
    REQUIRE(line.find("\"msg\":\"hello\"") != std::string::npos);
    REQUIRE(line.find("\"source\"") == std::string::npos);
}

TEST_CASE("json_formatter emits every level name [formatter][json]") {
    const std::array<std::pair<level, const char*>, 7> cases{{
        {level::trace, "trace"},
        {level::debug, "debug"},
        {level::info, "info"},
        {level::warn, "warn"},
        {level::error, "error"},
        {level::critical, "critical"},
        {level::off, "off"},
    }};
    for (const auto& [lvl, name] : cases) {
        details::log_msg msg("lv", lvl, "x");
        const auto line = format_json(msg);
        REQUIRE(line.find(std::string("\"level\":\"") + name + "\"") != std::string::npos);
        REQUIRE(json_number_field(line.substr(0, line.size() - 1), "level_num") ==
                std::to_string(static_cast<int>(lvl)));
    }
}

TEST_CASE("json_formatter ts matches log_msg time [formatter][json]") {
    const auto tp = log_clock::time_point(std::chrono::milliseconds(1'700'000'000'123));
    details::log_msg msg(tp, details::source_loc{}, "lg", level::info, "x");
    const auto line = format_json(msg);
    REQUIRE(line.find("\"ts\":1700000000123") != std::string::npos);
    REQUIRE(line.find(expected_time_field(tp)) != std::string::npos);
}

TEST_CASE("json_formatter empty payload and logger [formatter][json]") {
    details::log_msg msg("", level::debug, "");
    const auto line = format_json(msg);
    REQUIRE(line.find("\"logger\":\"\"") != std::string::npos);
    REQUIRE(line.find("\"msg\":\"\"") != std::string::npos);
}

TEST_CASE("json_formatter escapes quotes backslash and controls [formatter][json]") {
    details::log_msg msg("lg", level::warn, "say \"hi\"\\\n\t\x01");
    const auto line = format_json(msg);
    REQUIRE(line.find("\"msg\":\"say \\\"hi\\\"\\\\\\n\\t\\u0001\"") != std::string::npos);
}

TEST_CASE("json_formatter escapes remaining control characters [formatter][json]") {
    details::log_msg msg("lg", level::info, "a\b\f\rb");
    const auto line = format_json(msg);
    REQUIRE(line.find("\"msg\":\"a\\b\\f\\rb\"") != std::string::npos);
}

TEST_CASE("json_formatter passes through utf-8 [formatter][json]") {
    details::log_msg msg("中文", level::info, "你好");
    const auto line = format_json(msg);
    REQUIRE(line.find("\"logger\":\"中文\"") != std::string::npos);
    REQUIRE(line.find("\"msg\":\"你好\"") != std::string::npos);
}

TEST_CASE("json_formatter escapes logger name [formatter][json]") {
    details::log_msg msg("a\"b\\c", level::info, "x");
    const auto line = format_json(msg);
    REQUIRE(line.find("\"logger\":\"a\\\"b\\\\c\"") != std::string::npos);
}

TEST_CASE("json_formatter includes source when present [formatter][json]") {
    details::source_loc loc("app.cpp", 42, "main");
    details::log_msg msg(loc, "svc", level::error, "boom");
    const auto line = format_json(msg);
    REQUIRE(line.find("\"source\":{\"file\":\"app.cpp\",\"line\":42,\"func\":\"main\"}") !=
            std::string::npos);
}

TEST_CASE("json_formatter escapes backslashes in source path [formatter][json]") {
    details::source_loc loc("C:\\temp\\a.cpp", 7, "f");
    details::log_msg msg(loc, "svc", level::info, "x");
    const auto line = format_json(msg);
    REQUIRE(line.find("\"file\":\"C:\\\\temp\\\\a.cpp\"") != std::string::npos);
}

TEST_CASE("json_formatter source with null file and func [formatter][json]") {
    details::source_loc loc(nullptr, 9, nullptr);
    details::log_msg msg(loc, "svc", level::info, "x");
    const auto line = format_json(msg);
    REQUIRE(line.find("\"source\":{\"file\":\"\",\"line\":9,\"func\":\"\"}") != std::string::npos);
}

TEST_CASE("json_formatter escapes unicode line separators [formatter][json]") {
    std::string payload = "a";
    payload += "\xE2\x80\xA8";
    payload += "b";
    payload += "\xE2\x80\xA9";
    payload += "c";
    details::log_msg msg("lg", level::info, payload);
    const auto line = format_json(msg);
    REQUIRE(line.find("\"msg\":\"a\\u2028b\\u2029c\"") != std::string::npos);
}

TEST_CASE("json_formatter clone matches original [formatter][json]") {
    details::log_msg msg("n", level::debug, "x");
    json_formatter fmt;
    auto cloned = fmt.clone();
    fmt::memory_buffer a;
    fmt::memory_buffer b;
    fmt.format(msg, a);
    cloned->format(msg, b);
    REQUIRE(std::string(a.data(), a.size()) == std::string(b.data(), b.size()));
}

TEST_CASE("json_formatter can be installed on any sink [sink][json]") {
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_formatter(std::make_unique<json_formatter>());
    logger lg("any", mock);
    lg.info("from-mock");
    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->at(0).find("\"msg\":\"from-mock\"") != std::string::npos);
    REQUIRE(mock->at(0).find("\"logger\":\"any\"") != std::string::npos);
}

TEST_CASE("json_formatter on callback_sink [sink][json][callback]") {
    std::vector<std::string> lines;
    auto sink = std::make_shared<sinks::callback_sink_mt>([&](const std::string& formatted) {
        lines.push_back(formatted);
    });
    sink->set_formatter(std::make_unique<json_formatter>());
    logger lg("cb-json", sink);
    lg.error("via-callback");
    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0].find("\"msg\":\"via-callback\"") != std::string::npos);
    REQUIRE(lines[0].find("\"level\":\"error\"") != std::string::npos);
}

TEST_CASE("json_file_sink writes NDJSON and ignores set_pattern [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("app.json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_mt>(path, true);
    logger lg("file", sink);
    lg.set_pattern("[%L] %v");
    lg.info("one");
    lg.error("two");
    lg.flush();
    sink.reset();

    const auto text = read_file(path);
    require_ndjson(text, 2);
    REQUIRE(text.find("\"msg\":\"one\"") != std::string::npos);
    REQUIRE(text.find("\"msg\":\"two\"") != std::string::npos);
    REQUIRE(text.find("\"level\":\"error\"") != std::string::npos);
    REQUIRE(text.find("[info]") == std::string::npos);
}

TEST_CASE("json_file_sink_st writes JSON Lines [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("st.json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_st>(path, true);
    sink->log(details::log_msg("st", level::warn, "single-thread"));
    sink->flush();
    REQUIRE(read_file(path).find("\"msg\":\"single-thread\"") != std::string::npos);
}

TEST_CASE("json_file_sink truncate replaces previous content [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("trunc.json.log").string();
    {
        auto sink = std::make_shared<sinks::json_file_sink_mt>(path, true);
        sink->log(details::log_msg("t", level::info, "old"));
        sink->flush();
    }
    auto sink = std::make_shared<sinks::json_file_sink_mt>(path, true);
    sink->log(details::log_msg("t", level::info, "new"));
    sink->flush();
    const auto text = read_file(path);
    REQUIRE(text.find("\"msg\":\"new\"") != std::string::npos);
    REQUIRE(text.find("\"msg\":\"old\"") == std::string::npos);
}

TEST_CASE("json_file_sink append keeps previous lines [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("append.json.log").string();
    {
        auto sink = std::make_shared<sinks::json_file_sink_mt>(path, true);
        sink->log(details::log_msg("t", level::info, "first"));
        sink->flush();
    }
    auto sink = std::make_shared<sinks::json_file_sink_mt>(path, false);
    sink->log(details::log_msg("t", level::info, "second"));
    sink->flush();
    const auto text = read_file(path);
    require_ndjson(text, 2);
    REQUIRE(text.find("\"msg\":\"first\"") != std::string::npos);
    REQUIRE(text.find("\"msg\":\"second\"") != std::string::npos);
}

TEST_CASE("json_file_sink throws when the path cannot be opened [sink][json]") {
    test_fixture fx;
    REQUIRE_THROWS_AS(sinks::json_file_sink_mt(fx.temp_dir().string(), true), std::runtime_error);
}

TEST_CASE("json_console_sink ignores set_pattern [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("stdout.json.log");
    {
        tests::stdio_redirect capture(stdout, path);
        sinks::json_console_sink_st sink;
        sink.set_pattern("%v");
        sink.log(details::log_msg("out", level::info, "console-json"));
        sink.flush();
    }
    const auto text = read_file(path);
    REQUIRE(text.find("\"msg\":\"console-json\"") != std::string::npos);
    REQUIRE(text.find("\"logger\":\"out\"") != std::string::npos);
    REQUIRE(text.find("[info]") == std::string::npos);
}

TEST_CASE("json_logger_mt factory registers and writes [sink][json][registry]") {
    test_fixture fx;
    const auto path = fx.temp_path("factory.json.log").string();
    auto lg = json_logger_mt("json-factory", path, true);
    lg->set_pattern("%v");
    lg->info("from-factory");
    lg->flush();

    REQUIRE(get("json-factory") != nullptr);
    const auto text = read_file(path);
    REQUIRE(text.find("\"msg\":\"from-factory\"") != std::string::npos);
    REQUIRE(text.find("[info]") == std::string::npos);
    drop("json-factory");
}

TEST_CASE("json_logger_st factory registers [sink][json][registry]") {
    test_fixture fx;
    const auto path = fx.temp_path("factory-st.json.log").string();
    auto lg = json_logger_st("json-factory-st", path, true);
    lg->warn("st-factory");
    lg->flush();
    REQUIRE(get("json-factory-st") != nullptr);
    REQUIRE(read_file(path).find("\"level\":\"warn\"") != std::string::npos);
    drop("json-factory-st");
}

TEST_CASE("stdout_json factories register loggers [sink][json][registry]") {
    auto mt = stdout_json_mt("json-stdout-mt");
    auto st = stdout_json_st("json-stdout-st");
    REQUIRE(get("json-stdout-mt") != nullptr);
    REQUIRE(get("json-stdout-st") != nullptr);
    drop("json-stdout-mt");
    drop("json-stdout-st");
}

TEST_CASE("async_json_file_mt writes JSON after flush [sink][json][async]") {
    test_fixture fx;
    const auto path = fx.temp_path("async.json.log").string();
    shutdown();
    init_thread_pool(1024, 1);
    auto lg = async_json_file_mt("json-async", path, true);
    lg->warn("async-json");
    lg->flush();
    shutdown();

    REQUIRE(read_file(path).find("\"msg\":\"async-json\"") != std::string::npos);
    REQUIRE(read_file(path).find("\"level\":\"warn\"") != std::string::npos);
}

TEST_CASE("json_formatter stress formats many lines quickly [formatter][json][stress]") {
    json_formatter fmt;
    const auto payload = std::string(128, 'x');
    details::log_msg msg("bench", level::info, payload);
    constexpr int kCount = 8000;
    const auto start = std::chrono::steady_clock::now();
    std::size_t bytes = 0;
    for (int i = 0; i < kCount; ++i) {
        fmt::memory_buffer buf;
        fmt.format(msg, buf);
        bytes += buf.size();
        REQUIRE(buf.size() > 0);
        REQUIRE(buf.data()[0] == '{');
        REQUIRE(buf.data()[buf.size() - 1] == '\n');
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(bytes > static_cast<std::size_t>(kCount) * 64);
    REQUIRE(elapsed < std::chrono::seconds(5));
}

TEST_CASE("json_formatter stress large payloads [formatter][json][stress]") {
    json_formatter fmt;
    const auto payload = std::string(2048, 'Y');
    details::log_msg msg("big", level::info, payload);
    constexpr int kCount = 400;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
        fmt::memory_buffer buf;
        fmt.format(msg, buf);
        const auto line = std::string(buf.data(), buf.size());
        REQUIRE(line.find("\"msg\":\"") != std::string::npos);
        REQUIRE(line.size() > 2048);
    }
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
}

TEST_CASE("json_file_sink_mt concurrent writes stay line-oriented [sink][json][stress]") {
    test_fixture fx;
    const auto path = fx.temp_path("concurrent.json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_mt>(path, true);
    logger lg("json-conc", sink);

    constexpr int kThreads = 4;
    constexpr int kPerThread = 200;
    const auto start = std::chrono::steady_clock::now();
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
    lg.flush();
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(8));

    const auto text = read_file(path);
    require_ndjson(text, static_cast<std::size_t>(kThreads * kPerThread));
    REQUIRE(text.find("\"msg\":\"t0-i0\"") != std::string::npos);
    REQUIRE(text.find("\"msg\":\"t3-i199\"") != std::string::npos);
}

TEST_CASE("async_json_file_mt burst completes after flush [sink][json][async][stress]") {
    test_fixture fx;
    const auto path = fx.temp_path("async-burst.json.log").string();
    shutdown();
    init_thread_pool(4096, 2);
    auto lg = async_json_file_mt("json-async-burst", path, true);

    constexpr int kCount = 1000;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
        lg->info("burst-{}", i);
    }
    lg->flush();
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(8));
    shutdown();

    const auto text = read_file(path);
    require_ndjson(text, static_cast<std::size_t>(kCount));
    REQUIRE(text.find("\"msg\":\"burst-0\"") != std::string::npos);
    REQUIRE(text.find("\"msg\":\"burst-999\"") != std::string::npos);
}

TEST_CASE("json_formatter wall time is UTC ISO-8601 of log_msg [formatter][json]") {
    const auto tp = log_clock::now();
    details::log_msg msg(tp, details::source_loc{}, "lg", level::info, "now");
    const auto line = format_json(msg);
    REQUIRE(line.find(expected_time_field(tp)) != std::string::npos);
}

TEST_CASE("json_formatter reuses wall clock within the same second [formatter][json]") {
    const auto tp = log_clock::time_point(std::chrono::seconds(1'700'000'000));
    details::log_msg a(tp, details::source_loc{}, "lg", level::info, "a");
    details::log_msg b(tp + std::chrono::milliseconds(250), details::source_loc{}, "lg", level::info,
                       "b");
    json_formatter fmt;
    fmt::memory_buffer first;
    fmt::memory_buffer second;
    fmt.format(a, first);
    fmt.format(b, second);
    const auto line1 = std::string(first.data(), first.size());
    const auto line2 = std::string(second.data(), second.size());
    REQUIRE(line1.find("\"time\":\"") != std::string::npos);
    REQUIRE(line1.substr(0, 28) == line2.substr(0, 28));
    REQUIRE(line1.find(".000Z\"") != std::string::npos);
    REQUIRE(line2.find(".250Z\"") != std::string::npos);
}

TEST_CASE("json_file_sink writes LF-only NDJSON [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("lf.json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_mt>(path, true);
    sink->log(details::log_msg("bin", level::info, "lf-only"));
    sink->flush();
    const auto text = read_file(path);
    REQUIRE(text.find('\r') == std::string::npos);
    REQUIRE(text.find('\n') != std::string::npos);
    require_ndjson(text, 1);
}

TEST_CASE("json_file_sink ignores set_formatter back to pattern [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("lock.json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_st>(path, true);
    sink->set_formatter(std::make_unique<pattern_formatter>("[%L] %v"));
    logger lg("lock", sink);
    lg.info("still-json");
    lg.flush();
    const auto text = read_file(path);
    REQUIRE(text.find("\"msg\":\"still-json\"") != std::string::npos);
    REQUIRE(text.find("[info]") == std::string::npos);
}

TEST_CASE("json_rotating_file_sink writes JSON and rotates [sink][json][rotating]") {
    test_fixture fx;
    const auto base = fx.temp_path("app.json.log").string();
    auto sink = std::make_shared<sinks::json_rotating_file_sink_mt>(base, 400, 2);
    logger lg("rot-json", sink);
    lg.set_pattern("[%L] %v");
    lg.set_formatter(std::make_unique<pattern_formatter>("%v"));
    const std::string payload(80, 'J');
    lg.info("{}", payload);
    lg.info("{}", payload);
    lg.info("{}", payload);
    lg.flush();

    REQUIRE(std::filesystem::exists(base));
    REQUIRE(std::filesystem::exists(sinks::rotating_file_sink_mt::calc_filename(base, 1)));
    const auto current = read_file(base);
    const auto archived = read_file(sinks::rotating_file_sink_mt::calc_filename(base, 1));
    REQUIRE(current.find("\"logger\":\"rot-json\"") != std::string::npos);
    REQUIRE(archived.find("\"logger\":\"rot-json\"") != std::string::npos);
    REQUIRE(current.find("[info]") == std::string::npos);
    REQUIRE(archived.find("[info]") == std::string::npos);
    REQUIRE(current.find('\r') == std::string::npos);
}

TEST_CASE("json_daily_file_sink writes JSON into the dated file [sink][json][daily]") {
    test_fixture fx;
    const auto base = fx.temp_path("app.json.log").string();
    auto sink = std::make_shared<sinks::json_daily_file_sink_mt>(base);
    sink->set_pattern("%v");
    const auto now = log_clock::now();
    sink->log(details::log_msg(now, details::source_loc{}, "daily-json", level::warn, "day-line"));
    sink->flush();

    const auto today = sinks::daily_file_sink_mt::calc_filename(base, local_tm_of(now));
    const auto text = read_file(today);
    REQUIRE(text.find("\"msg\":\"day-line\"") != std::string::npos);
    REQUIRE(text.find("\"logger\":\"daily-json\"") != std::string::npos);
    REQUIRE(text.find("\"level\":\"warn\"") != std::string::npos);
    REQUIRE(text.find("[warn]") == std::string::npos);
}

TEST_CASE("rotating_json_logger_mt factory registers [sink][json][registry]") {
    test_fixture fx;
    const auto path = fx.temp_path("factory-rot.json.log").string();
    auto lg = rotating_json_logger_mt("json-rot-factory", path, 1024 * 1024, 3);
    lg->set_pattern("%v");
    lg->error("from-rot-factory");
    lg->flush();
    REQUIRE(get("json-rot-factory") != nullptr);
    REQUIRE(read_file(path).find("\"msg\":\"from-rot-factory\"") != std::string::npos);
    drop("json-rot-factory");
}

TEST_CASE("daily_json_logger_mt factory registers [sink][json][registry][daily]") {
    test_fixture fx;
    const auto base = fx.temp_path("factory-day.json.log").string();
    auto lg = daily_json_logger_mt("json-day-factory", base);
    lg->info("from-day-factory");
    lg->flush();
    const auto today = sinks::daily_file_sink_mt::calc_filename(base, local_tm_of(log_clock::now()));
    REQUIRE(get("json-day-factory") != nullptr);
    REQUIRE(read_file(today).find("\"msg\":\"from-day-factory\"") != std::string::npos);
    drop("json-day-factory");
}

TEST_CASE("stderr_json factories register loggers [sink][json][registry]") {
    auto mt = stderr_json_mt("json-stderr-mt");
    auto st = stderr_json_st("json-stderr-st");
    REQUIRE(get("json-stderr-mt") != nullptr);
    REQUIRE(get("json-stderr-st") != nullptr);
    drop("json-stderr-mt");
    drop("json-stderr-st");
}

TEST_CASE("async_json_rotating_mt writes JSON after flush [sink][json][async][rotating]") {
    test_fixture fx;
    const auto path = fx.temp_path("async-rot.json.log").string();
    shutdown();
    init_thread_pool(1024, 1);
    auto lg = async_json_rotating_mt("json-async-rot", path, 1024 * 1024, 2);
    lg->warn("async-rot-json");
    lg->flush();
    shutdown();
    REQUIRE(read_file(path).find("\"msg\":\"async-rot-json\"") != std::string::npos);
    REQUIRE(read_file(path).find("\"level\":\"warn\"") != std::string::npos);
}

TEST_CASE("json_file_sink records are parseable NDJSON with stable field shapes [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("shape.json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_st>(path, true);
    logger lg("shape", sink);
    lg.info("hello {}", "json");
    lg.warn("path is C:\\temp\\app.log");
    lg.error("quote: \"boom\"");
    lg.flush();

    const auto text = read_file(path);
    REQUIRE(text.find('\r') == std::string::npos);
    const auto lines = json_lines(text);
    REQUIRE(lines.size() == 3);
    for (const auto& line : lines) {
        require_file_record_shape(line);
        REQUIRE(json_quoted_field(line, "logger") == "shape");
        REQUIRE(line.find("\"source\":{") != std::string::npos);
        REQUIRE(line.find("test_json.cpp") != std::string::npos);
    }
    REQUIRE(json_quoted_field(lines[0], "msg") == "hello json");
    REQUIRE(json_quoted_field(lines[0], "level") == "info");
    REQUIRE(json_quoted_field(lines[1], "msg") == "path is C:\\\\temp\\\\app.log");
    REQUIRE(json_quoted_field(lines[2], "msg") == "quote: \\\"boom\\\"");
}

TEST_CASE("json_file_sink writes utf-8 payload and logger name [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("utf8.json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_st>(path, true);
    logger lg("中文", sink);
    lg.info("你好");
    lg.flush();
    const auto line = json_lines(read_file(path)).at(0);
    require_file_record_shape(line);
    REQUIRE(json_quoted_field(line, "logger") == "中文");
    REQUIRE(json_quoted_field(line, "msg") == "你好");
}

TEST_CASE("json_file_sink holds then flushes a complete JSON line [sink][json][buffer]") {
    test_fixture fx;
    const auto path = fx.temp_path("held.json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_st>(path, true, batch_config::until_flush());
    logger lg("held", sink);
    lg.info("still-buffered");
    REQUIRE(read_file(path).find("still-buffered") == std::string::npos);
    lg.flush();
    const auto line = json_lines(read_file(path)).at(0);
    require_file_record_shape(line);
    REQUIRE(json_quoted_field(line, "msg") == "still-buffered");
}

TEST_CASE("json_file_sink salvage drops a torn JSON object on reopen [sink][json][recovery]") {
    test_fixture fx;
    const auto path = fx.temp_path("torn.json.log").string();
    write_file(path, "{\"time\":\"2026-01-01 00:00:00.000\",\"ts\":1,\"level\":\"info\","
                     "\"logger\":\"t\",\"msg\":\"ok\",\"tid\":1,\"pid\":1}\n{\"msg\":\"partial");
    auto sink = std::make_shared<sinks::json_file_sink_st>(path, false, batch_config::until_flush());
    logger lg("t", sink);
    lg.info("after");
    lg.flush();
    const auto lines = json_lines(read_file(path));
    REQUIRE(lines.size() == 2);
    REQUIRE(json_quoted_field(lines[0], "msg") == "ok");
    REQUIRE(json_quoted_field(lines[1], "msg") == "after");
    REQUIRE(read_file(path).find("partial") == std::string::npos);
}

TEST_CASE("json_file_sink allow_custom_formatting unlocks pattern text [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("unlock.json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_st>(path, true, batch_config::until_flush());
    sink->allow_custom_formatting(true);
    sink->set_pattern("%v");
    logger lg("unlock", sink);
    lg.info("plain-text");
    lg.flush();
    REQUIRE(read_file(path) == "plain-text\n");
}

TEST_CASE("json_stderr_sink writes JSON Lines to stderr [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("stderr.json.log");
    {
        tests::stdio_redirect capture(stderr, path);
        sinks::json_stderr_sink_st sink;
        sink.set_pattern("%v");
        sink.log(details::log_msg("err", level::error, "stderr-json"));
        sink.flush();
    }
    const auto text = read_file(path);
    REQUIRE(text.find("\"msg\":\"stderr-json\"") != std::string::npos);
    REQUIRE(text.find("\"logger\":\"err\"") != std::string::npos);
    REQUIRE(text.find("\"level\":\"error\"") != std::string::npos);
    REQUIRE(text.find("[error]") == std::string::npos);
}

TEST_CASE("json_logger_mt file is usable NDJSON after sourced info [sink][json][registry]") {
    test_fixture fx;
    const auto path = fx.temp_path("usable.json.log").string();
    auto lg = json_logger_mt("usable", path, true, batch_config::until_flush());
    lg->info("user {} login", 42);
    lg->flush();
    const auto line = json_lines(read_file(path)).at(0);
    require_file_record_shape(line);
    REQUIRE(json_quoted_field(line, "msg") == "user 42 login");
    REQUIRE(json_quoted_field(line, "logger") == "usable");
    REQUIRE(line.find("\"source\":{") != std::string::npos);
    drop("usable");
}

TEST_CASE("json_formatter resource fields survive clone [formatter][json]") {
    json_formatter fmt;
    fmt.add("service", "checkout");
    fmt.add_int("shard", 3);
    fmt.add_bool("canary", false);
    fmt.add_null("trace_id");
    auto cloned = fmt.clone();
    details::log_msg msg("n", level::info, "x");
    fmt::memory_buffer a;
    fmt::memory_buffer b;
    fmt.format(msg, a);
    cloned->format(msg, b);
    const auto line = std::string(a.data(), a.size());
    REQUIRE(std::string(a.data(), a.size()) == std::string(b.data(), b.size()));
    REQUIRE(json_quoted_field(line, "service") == "checkout");
    REQUIRE(json_number_field(line, "shard") == "3");
    REQUIRE(line.find("\"canary\":false") != std::string::npos);
    REQUIRE(line.find("\"trace_id\":null") != std::string::npos);
}

TEST_CASE("json_formatter skips reserved and empty resource keys [formatter][json]") {
    json_formatter fmt;
    fmt.add("msg", "nope");
    fmt.add("", "empty-key");
    fmt.add("service", "api");
    fmt.add("service", "checkout");
    details::log_msg msg("n", level::warn, "payload");
    const auto line = format_json_with(fmt, msg);

    REQUIRE(json_quoted_field(line, "msg") == "payload");
    REQUIRE(line.find("\"msg\":\"nope\"") == std::string::npos);
    REQUIRE(line.find("\"\":\"empty-key\"") == std::string::npos);
    REQUIRE(json_quoted_field(line, "service") == "checkout");
    std::size_t hits = 0;
    for (std::size_t pos = 0;
         (pos = line.find("\"service\":", pos)) != std::string::npos;
         pos += 10) {
        ++hits;
    }
    REQUIRE(hits == 1);
}

TEST_CASE("json_formatter with_host adds host [formatter][json]") {
    json_formatter fmt;
    fmt.with_host();
    details::log_msg msg("n", level::info, "x");
    fmt::memory_buffer buf;
    fmt.format(msg, buf);
    const auto line = std::string(buf.data(), buf.size());
    REQUIRE_FALSE(json_quoted_field(line, "host").empty());
}

TEST_CASE("json_file_sink json() context survives set_pattern [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("ctx.json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_st>(path, true);
    sink->json().add("service", "api").add_int("schema", 1);
    logger lg("ctx", sink);
    lg.set_pattern("[%L] %v");
    lg.info("kept");
    lg.flush();
    const auto line = json_lines(read_file(path)).at(0);
    require_file_record_shape(line);
    REQUIRE(json_quoted_field(line, "msg") == "kept");
    REQUIRE(json_quoted_field(line, "service") == "api");
    REQUIRE(json_number_field(line, "schema") == "1");
    REQUIRE(line.find("[info]") == std::string::npos);
}

TEST_CASE("json_logger_mt factory installs resource fields [sink][json][registry]") {
    test_fixture fx;
    const auto path = fx.temp_path("factory-fields.json.log").string();
    json_formatter fmt;
    fmt.add("env", "test");
    auto lg = json_logger_mt("json-fields", path, true, batch_config::until_flush(), std::move(fmt));
    lg->info("from-factory");
    lg->flush();
    const auto line = json_lines(read_file(path)).at(0);
    require_file_record_shape(line);
    REQUIRE(json_quoted_field(line, "msg") == "from-factory");
    REQUIRE(json_quoted_field(line, "env") == "test");
    drop("json-fields");
}

TEST_CASE("json() throws after custom formatting is enabled [sink][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("unlock-json.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_st>(path, true, batch_config::until_flush());
    sink->allow_custom_formatting(true);
    sink->set_pattern("%v");
    REQUIRE_THROWS_AS(sink->json(), std::logic_error);
}
