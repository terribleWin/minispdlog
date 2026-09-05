#include "framework/doctest.h"
#include "framework/test_fixture.h"
#include "minispdlog/minispdlog.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace minispdlog;
using minispdlog::tests::test_fixture;

namespace {

details::log_msg make_msg(string_view_t payload) {
    return details::log_msg("rotating", level::info, payload);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool file_exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

size_t file_size(const std::filesystem::path& path) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<size_t>(n);
}

std::string indexed(const std::string& base, size_t index) {
    return sinks::rotating_file_sink_mt::calc_filename(base, index);
}

void write_n(sinks::rotating_file_sink_mt& sink, int n, const std::string& payload) {
    for (int i = 0; i < n; ++i) {
        sink.log(make_msg(payload));
    }
    sink.flush();
}

} // namespace

TEST_CASE("rotating calc_filename inserts index before extension [sink][rotating]") {
    REQUIRE(sinks::rotating_file_sink_mt::calc_filename("logs/mylog.txt", 0) == "logs/mylog.txt");
    REQUIRE(sinks::rotating_file_sink_mt::calc_filename("logs/mylog.txt", 1) == "logs/mylog.1.txt");
    REQUIRE(sinks::rotating_file_sink_mt::calc_filename("logs/mylog.txt", 3) == "logs/mylog.3.txt");
    REQUIRE(sinks::rotating_file_sink_st::calc_filename("logs/mylog", 0) == "logs/mylog");
    REQUIRE(sinks::rotating_file_sink_st::calc_filename("logs/mylog", 2) == "logs/mylog.2");
}

TEST_CASE("rotating calc_filename ignores dots in directory names [sink][rotating]") {
    REQUIRE(sinks::rotating_file_sink_mt::calc_filename("logs.v2/mylog", 1) == "logs.v2/mylog.1");
    REQUIRE(sinks::rotating_file_sink_mt::calc_filename("logs\\mylog.txt", 1) == "logs\\mylog.1.txt");
}

TEST_CASE("rotating_file_sink rejects zero max_size or max_files [sink][rotating]") {
    test_fixture fx;
    const auto path = fx.temp_path("bad.log").string();
    REQUIRE_THROWS_AS(sinks::rotating_file_sink_mt(path, 0, 3), std::invalid_argument);
    REQUIRE_THROWS_AS(sinks::rotating_file_sink_mt(path, 100, 0), std::invalid_argument);
}

TEST_CASE("rotating_file_sink writes the current file under max_size [sink][rotating]") {
    test_fixture fx;
    const auto base = fx.temp_path("app.log").string();
    auto sink = std::make_shared<sinks::rotating_file_sink_mt>(base, 1024, 3);
    sink->set_pattern("%v");
    sink->log(make_msg("keep-current"));
    sink->flush();

    REQUIRE(sink->filename() == base);
    REQUIRE(file_exists(base));
    REQUIRE(read_file(base).find("keep-current") != std::string::npos);
    REQUIRE_FALSE(file_exists(indexed(base, 1)));
}

TEST_CASE("rotating_file_sink rotates when size would exceed max_size [sink][rotating]") {
    test_fixture fx;
    const auto base = fx.temp_path("size.log").string();
    constexpr size_t max_size = 80;
    auto sink = std::make_shared<sinks::rotating_file_sink_mt>(base, max_size, 2);
    sink->set_pattern("%v");

    const std::string chunk(50, 'A');
    write_n(*sink, 3, chunk);

    REQUIRE(file_exists(base));
    REQUIRE(file_exists(indexed(base, 1)));
    REQUIRE(file_size(base) > 0);
    REQUIRE(file_size(base) <= max_size);
    REQUIRE(file_size(indexed(base, 1)) > 0);
}

TEST_CASE("rotating_file_sink keeps at most current plus max_files [sink][rotating]") {
    test_fixture fx;
    const auto base = fx.temp_path("cap.log").string();
    constexpr size_t max_size = 80;
    constexpr size_t max_files = 3;
    auto sink = std::make_shared<sinks::rotating_file_sink_mt>(base, max_size, max_files);
    sink->set_pattern("%v");

    write_n(*sink, 40, std::string(40, 'B'));

    int existing = 0;
    for (size_t i = 0; i <= max_files; ++i) {
        if (file_exists(indexed(base, i))) {
            ++existing;
        }
    }
    REQUIRE(existing == static_cast<int>(max_files + 1));
    REQUIRE_FALSE(file_exists(indexed(base, max_files + 1)));
}

TEST_CASE("rotated file keeps earlier lines, current file has later ones [sink][rotating]") {
    test_fixture fx;
    const auto base = fx.temp_path("content.log").string();
    auto sink = std::make_shared<sinks::rotating_file_sink_mt>(base, 50, 2);
    sink->set_pattern("%v");

    sink->log(make_msg("FIRST"));
    sink->log(make_msg("SECOND"));
    sink->log(make_msg(std::string(30, 'X')));
    sink->log(make_msg(std::string(30, 'Y')));
    sink->log(make_msg("AFTER_ROTATION"));
    sink->flush();

    const auto current = read_file(base);
    const auto rotated = read_file(indexed(base, 1));
    REQUIRE(file_exists(indexed(base, 1)));
    REQUIRE(current.find("AFTER_ROTATION") != std::string::npos);
    REQUIRE(rotated.find("FIRST") != std::string::npos);
    REQUIRE(rotated.find("SECOND") != std::string::npos);
    REQUIRE(rotated.find("AFTER_ROTATION") == std::string::npos);
}

TEST_CASE("rotating_file_sink works with max_files = 1 [sink][rotating]") {
    test_fixture fx;
    const auto base = fx.temp_path("single.log").string();
    auto sink = std::make_shared<sinks::rotating_file_sink_mt>(base, 60, 1);
    sink->set_pattern("%v");
    write_n(*sink, 20, std::string(30, 'C'));

    REQUIRE(file_exists(base));
    REQUIRE(file_exists(indexed(base, 1)));
    REQUIRE_FALSE(file_exists(indexed(base, 2)));
}

TEST_CASE("rotating_logger_mt factory registers and writes [sink][rotating][registry]") {
    test_fixture fx;
    const auto base = fx.temp_path("factory.log").string();
    auto lg = rotating_logger_mt("rotating-factory", base, 4096, 2);
    lg->set_pattern("%v");
    lg->info("from-factory");
    lg->flush();

    REQUIRE(get("rotating-factory") != nullptr);
    REQUIRE(file_exists(base));
    REQUIRE(read_file(base).find("from-factory") != std::string::npos);
    drop("rotating-factory");
}

TEST_CASE("rotating_file_sink_mt accepts concurrent writes [sink][rotating]") {
    test_fixture fx;
    const auto base = fx.temp_path("concurrent.log").string();
    auto sink = std::make_shared<sinks::rotating_file_sink_mt>(base, 1024, 3);
    sink->set_pattern("%v");
    logger lg("rotating-concurrent", sink);

    auto worker = [&lg](int thread_id) {
        for (int i = 0; i < 20; ++i) {
            lg.info("Thread {} - Message {}", thread_id, i);
        }
    };

    std::vector<std::thread> threads;
    threads.emplace_back(worker, 1);
    threads.emplace_back(worker, 2);
    threads.emplace_back(worker, 3);
    for (auto& t : threads) {
        t.join();
    }
    lg.flush();

    REQUIRE(file_size(base) + file_size(indexed(base, 1)) + file_size(indexed(base, 2)) > 0);
    const auto text = read_file(base) + read_file(indexed(base, 1)) + read_file(indexed(base, 2));
    REQUIRE(text.find("Thread 1") != std::string::npos);
    REQUIRE(text.find("Thread 2") != std::string::npos);
    REQUIRE(text.find("Thread 3") != std::string::npos);
}
