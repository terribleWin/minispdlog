#include "framework/doctest.h"
#include "framework/test_fixture.h"
#include "minispdlog/async.h"
#include "minispdlog/details/durable_file.h"
#include "minispdlog/minispdlog.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace minispdlog;
using minispdlog::tests::test_fixture;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::size_t count_lines(const std::string& text) {
    std::size_t n = 0;
    for (char c : text) {
        if (c == '\n') {
            ++n;
        }
    }
    return n;
}

batch_config hold() {
    return batch_config::until_flush();
}

} // namespace

TEST_CASE("buffered_file_sink holds lines until flush [sink][buffer]") {
    test_fixture fx;
    const auto path = fx.temp_path("hold.log").string();
    auto sink = std::make_shared<sinks::buffered_file_sink_st>(path, true, hold());
    sink->set_pattern("%v");
    logger lg("hold", sink);
    lg.info("secret");

    REQUIRE(read_file(path).find("secret") == std::string::npos);
    REQUIRE(sink->queued_bytes() > 0);

    sink->flush();
    REQUIRE(read_file(path).find("secret") != std::string::npos);
    REQUIRE(sink->queued_bytes() == 0);
}

TEST_CASE("buffered_file_sink commits when record count hits the limit [sink][buffer]") {
    test_fixture fx;
    const auto path = fx.temp_path("count.log").string();
    batch_config cfg = hold();
    cfg.max_records = 2;
    auto sink = std::make_shared<sinks::buffered_file_sink_st>(path, true, cfg);
    sink->set_pattern("%v");
    logger lg("count", sink);
    lg.info("one");
    REQUIRE(read_file(path).find("one") == std::string::npos);
    lg.info("two");
    REQUIRE(read_file(path).find("one") != std::string::npos);
    REQUIRE(read_file(path).find("two") != std::string::npos);
}

TEST_CASE("buffered_file_sink commits when byte threshold is reached [sink][buffer]") {
    test_fixture fx;
    const auto path = fx.temp_path("bytes.log").string();
    batch_config cfg = hold();
    cfg.max_bytes = 5;
    auto sink = std::make_shared<sinks::buffered_file_sink_st>(path, true, cfg);
    sink->set_pattern("%v");
    logger lg("bytes", sink);
    lg.info("hi");
    REQUIRE(read_file(path).empty());
    lg.info("hi");
    REQUIRE(count_lines(read_file(path)) == 2);
}

TEST_CASE("buffered_file_sink age threshold commits on the next log [sink][buffer]") {
    test_fixture fx;
    const auto path = fx.temp_path("age.log").string();
    batch_config cfg = hold();
    cfg.max_age = std::chrono::milliseconds{20};
    auto sink = std::make_shared<sinks::buffered_file_sink_st>(path, true, cfg);
    sink->set_pattern("%v");
    logger lg("age", sink);
    lg.info("old");
    REQUIRE(read_file(path).find("old") == std::string::npos);
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    lg.info("new");
    REQUIRE(read_file(path).find("old") != std::string::npos);
    REQUIRE(read_file(path).find("new") != std::string::npos);
}

TEST_CASE("buffered_file_sink_mt concurrent logs all reach disk after flush [sink][buffer][stress]") {
    test_fixture fx;
    const auto path = fx.temp_path("mt.log").string();
    auto sink = std::make_shared<sinks::buffered_file_sink_mt>(path, true, hold());
    sink->set_pattern("%v");
    logger lg("mt", sink);

    constexpr int kThreads = 4;
    constexpr int kEach = 200;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&lg] {
            for (int i = 0; i < kEach; ++i) {
                lg.info("line");
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    lg.flush();
    REQUIRE(count_lines(read_file(path)) == static_cast<std::size_t>(kThreads * kEach));
}

TEST_CASE("salvage drops a torn last line on open [sink][buffer][recovery]") {
    test_fixture fx;
    const auto path = fx.temp_path("torn.log").string();
    write_file(path, "keep\npartial");
    auto sink = std::make_shared<sinks::buffered_file_sink_st>(path, false, hold());
    sink->flush();
    REQUIRE(read_file(path) == "keep\n");
}

TEST_CASE("WAL replay appends a batch that never reached the main file [sink][buffer][recovery]") {
    test_fixture fx;
    const auto path = fx.temp_path("wal.log").string();
    write_file(path, "keep\n");
    details::write_wal_sidecar(path, 5, "more\n");
    REQUIRE(std::filesystem::exists(details::wal_path_for(path)));

    auto sink = std::make_shared<sinks::buffered_file_sink_st>(path, false, hold());
    sink->flush();
    REQUIRE(read_file(path) == "keep\nmore\n");
    REQUIRE_FALSE(std::filesystem::exists(details::wal_path_for(path)));
}

TEST_CASE("WAL replay repairs a torn write into the main file [sink][buffer][recovery]") {
    test_fixture fx;
    const auto path = fx.temp_path("torn-wal.log").string();
    write_file(path, "keep\nxx");
    details::write_wal_sidecar(path, 5, "more\n");

    auto sink = std::make_shared<sinks::buffered_file_sink_st>(path, false, hold());
    sink->flush();
    REQUIRE(read_file(path) == "keep\nmore\n");
}

TEST_CASE("WAL replay is a no-op when the main file already has the batch [sink][buffer][recovery]") {
    test_fixture fx;
    const auto path = fx.temp_path("dup-wal.log").string();
    write_file(path, "keep\nmore\n");
    details::write_wal_sidecar(path, 5, "more\n");

    auto sink = std::make_shared<sinks::buffered_file_sink_st>(path, false, hold());
    sink->flush();
    REQUIRE(read_file(path) == "keep\nmore\n");
    REQUIRE_FALSE(std::filesystem::exists(details::wal_path_for(path)));
}

TEST_CASE("truncate open discards a leftover WAL [sink][buffer][recovery]") {
    test_fixture fx;
    const auto path = fx.temp_path("trunc.log").string();
    write_file(path, "old\n");
    details::write_wal_sidecar(path, 0, "stale\n");
    auto sink = std::make_shared<sinks::buffered_file_sink_st>(path, true, hold());
    sink->set_pattern("%v");
    logger lg("trunc", sink);
    lg.info("fresh");
    lg.flush();
    REQUIRE(read_file(path).find("old") == std::string::npos);
    REQUIRE(read_file(path).find("stale") == std::string::npos);
    REQUIRE(read_file(path).find("fresh") != std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(details::wal_path_for(path)));
}

TEST_CASE("dump_buffered_logs writes the front buffer without flush [sink][buffer][recovery]") {
    test_fixture fx;
    const auto path = fx.temp_path("dump.log").string();
    auto sink = std::make_shared<sinks::buffered_file_sink_st>(path, true, hold());
    sink->set_pattern("%v");
    logger lg("dump", sink);
    lg.info("crash-me");
    REQUIRE(read_file(path).find("crash-me") == std::string::npos);
    dump_buffered_logs();
    REQUIRE(read_file(path).find("crash-me") != std::string::npos);
    REQUIRE(sink->queued_bytes() == 0);
}

TEST_CASE("install_crash_flush is idempotent [sink][buffer][recovery]") {
    REQUIRE_NOTHROW(install_crash_flush());
    REQUIRE_NOTHROW(install_crash_flush());
}

TEST_CASE("buffered_logger_mt factory registers and writes [sink][buffer][registry]") {
    test_fixture fx;
    const auto path = fx.temp_path("factory.log").string();
    auto lg = buffered_logger_mt("buf-factory", path, true, hold());
    lg->set_pattern("%v");
    lg->info("via-factory");
    lg->flush();
    REQUIRE(read_file(path).find("via-factory") != std::string::npos);
    drop("buf-factory");
}

TEST_CASE("json_file_sink uses the same batch hold path [sink][buffer][json]") {
    test_fixture fx;
    const auto path = fx.temp_path("json-hold.log").string();
    auto sink = std::make_shared<sinks::json_file_sink_st>(path, true, hold());
    logger lg("json-hold", sink);
    lg.info("held-json");
    REQUIRE(read_file(path).find("held-json") == std::string::npos);
    lg.flush();
    const auto text = read_file(path);
    REQUIRE(text.find("held-json") != std::string::npos);
    REQUIRE(text.find('\r') == std::string::npos);
    REQUIRE(text.back() == '\n');
}

TEST_CASE("async_buffered_file_mt writes after flush [sink][buffer][async]") {
    test_fixture fx;
    shutdown();
    init_thread_pool(256, 1);
    const auto path = fx.temp_path("async-buf.log").string();
    auto lg = async_buffered_file_mt("async-buf", path, true, async_overflow_policy::block, hold());
    lg->set_pattern("%v");
    lg->info("async-held");
    lg->flush();
    REQUIRE(read_file(path).find("async-held") != std::string::npos);
    shutdown();
}
