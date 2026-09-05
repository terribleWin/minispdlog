#include "framework/doctest.h"
#include "framework/mock_sink.h"
#include "framework/test_fixture.h"
#include "minispdlog/async.h"
#include "minispdlog/minispdlog.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace minispdlog;
using minispdlog::tests::mock_sink_mt;
using minispdlog::tests::test_fixture;

TEST_CASE("async_logger inherits from logger [async]") {
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<async_logger>("async_test", std::vector<sinks::sink_ptr>{mock});
    REQUIRE(lg != nullptr);
    REQUIRE(lg->name() == "async_test");
}

TEST_CASE("async_logger without pool falls back to sync write [async]") {
    shutdown();
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<async_logger>("nopool", std::vector<sinks::sink_ptr>{mock});
    lg->info("sync fallback");
    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->last_contains("sync fallback"));
}

TEST_CASE("thread_pool post_log then flush waits for delivery [async][thread_pool]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(1024, 2);
    auto lg = std::make_shared<logger>("tp_test", mock);
    details::log_msg msg("tp_test", level::info, "async message");

    pool.post_log(std::shared_ptr<logger>(lg), msg);
    pool.post_flush(std::shared_ptr<logger>(lg), true);

    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->last_contains("async message"));
}

TEST_CASE("thread_pool handles multiple messages [async][thread_pool]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(4096, 2);
    auto lg = std::make_shared<logger>("tp", mock);

    const int num_msgs = 100;
    for (int i = 0; i < num_msgs; ++i) {
        details::log_msg msg("tp", level::info, "msg" + std::to_string(i));
        pool.post_log(std::shared_ptr<logger>(lg), msg);
    }
    pool.post_flush(std::shared_ptr<logger>(lg), true);

    REQUIRE(mock->message_count() == static_cast<std::size_t>(num_msgs));
}

TEST_CASE("thread_pool multithreaded post [async][thread_pool][thread]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(8192, 4);
    auto lg = std::make_shared<logger>("mtp", mock);

    const int num_threads = 8;
    const int msgs_per_thread = 100;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < msgs_per_thread; ++j) {
                details::log_msg msg("mtp", level::info, "test");
                pool.post_log(std::shared_ptr<logger>(lg), msg);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    pool.post_flush(std::shared_ptr<logger>(lg), true);

    REQUIRE(mock->message_count() == static_cast<std::size_t>(num_threads * msgs_per_thread));
}

TEST_CASE("thread_pool destructor drains queued messages [async][thread_pool]") {
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<logger>("shutdown", mock);
    const int n = 50;
    {
        details::thread_pool pool(1024, 2);
        for (int i = 0; i < n; ++i) {
            details::log_msg msg("shutdown", level::info, "test");
            pool.post_log(std::shared_ptr<logger>(lg), msg);
        }
        pool.post_flush(std::shared_ptr<logger>(lg), true);
    }
    REQUIRE(mock->message_count() == static_cast<std::size_t>(n));
}

TEST_CASE("thread_pool post_log_nowait discards newest [async][thread_pool]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(4, 1);
    auto lg = std::make_shared<logger>("nowait", mock);
    for (int i = 0; i < 10000; ++i) {
        details::log_msg msg("nowait", level::info, "x");
        pool.post_log_nowait(std::shared_ptr<logger>(lg), msg);
    }
    pool.post_flush(std::shared_ptr<logger>(lg), true);
    REQUIRE(pool.discard_count() > 0);
    REQUIRE(pool.overrun_count() == 0);
}

TEST_CASE("thread_pool overrun_oldest drops oldest [async][thread_pool]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(10, 1);
    auto lg = std::make_shared<logger>("overrun", mock);
    for (int i = 0; i < 100; ++i) {
        details::log_msg msg("overrun", level::info, "test");
        pool.post_log(std::shared_ptr<logger>(lg), msg, async_overflow_policy::overrun_oldest);
    }
    pool.post_flush(std::shared_ptr<logger>(lg), true);
    REQUIRE(pool.overrun_count() > 0);
}

TEST_CASE("async_logger flush waits until messages are visible [async]") {
    shutdown();
    init_thread_pool(8192, 2);

    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<async_logger>("flush_wait", std::vector<sinks::sink_ptr>{mock});

    constexpr int n = 500;
    for (int i = 0; i < n; ++i) {
        lg->info("m{}", i);
    }
    lg->flush();
    REQUIRE(mock->message_count() == static_cast<std::size_t>(n));

    shutdown();
}

TEST_CASE("async_file factory creates registered logger [async][factory]") {
    shutdown();
    init_thread_pool(1024, 2);

    test_fixture fx;
    auto path = fx.temp_path("async_test.log");
    auto lg = async_file_mt("async_file_test", path.string(), true);
    REQUIRE(lg != nullptr);
    REQUIRE(lg->name() == "async_file_test");
    REQUIRE(get("async_file_test") != nullptr);

    lg->info("async file test message");
    lg->flush();

    std::ifstream in(path);
    REQUIRE(in.good());
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(contents.find("async file test message") != std::string::npos);

    shutdown();
}

TEST_CASE("init_thread_pool replaces the previous pool [async]") {
    shutdown();
    init_thread_pool(1024, 2);
    REQUIRE(get_thread_pool() != nullptr);
    REQUIRE(get_thread_pool()->worker_count() == 2);

    init_thread_pool(2048, 4);
    auto* pool = get_thread_pool();
    REQUIRE(pool != nullptr);
    REQUIRE(pool->worker_count() == 4);

    shutdown();
    REQUIRE(get_thread_pool() == nullptr);
}

TEST_CASE("shutdown drains async logger then joins the pool [async]") {
    shutdown();
    init_thread_pool(4096, 2);

    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<async_logger>("drain", std::vector<sinks::sink_ptr>{mock});
    register_logger(lg);
    lg->info("before shutdown");
    shutdown();

    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->last_contains("before shutdown"));
    REQUIRE(get_thread_pool() == nullptr);
}

TEST_CASE("thread_pool overrun count is consistent [async][thread_pool]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(5, 1);
    auto lg = std::make_shared<logger>("count", mock);

    size_t initial_overruns = pool.overrun_count();
    for (int i = 0; i < 20; ++i) {
        details::log_msg msg("count", level::info, "test");
        pool.post_log(std::shared_ptr<logger>(lg), msg, async_overflow_policy::overrun_oldest);
    }
    pool.post_flush(std::shared_ptr<logger>(lg), true);
    REQUIRE(pool.overrun_count() >= initial_overruns);
}

TEST_CASE("lockfree thread_pool forces single worker [async][lockfree]") {
    details::thread_pool pool(1024, 4, async_queue_type::lockfree);
    REQUIRE(pool.queue_type() == async_queue_type::lockfree);
    REQUIRE(pool.worker_count() == 1);
}

TEST_CASE("lockfree thread_pool delivers messages [async][lockfree][thread]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(2048, 1, async_queue_type::lockfree);
    auto lg = std::make_shared<logger>("lf", mock);

    const int n = 200;
    for (int i = 0; i < n; ++i) {
        details::log_msg msg("lf", level::info, "lockfree-" + std::to_string(i));
        pool.post_log(std::shared_ptr<logger>(lg), msg, async_overflow_policy::block);
    }
    pool.post_flush(std::shared_ptr<logger>(lg), true);
    REQUIRE(mock->message_count() == static_cast<std::size_t>(n));
}

TEST_CASE("lockfree thread_pool discard_new counts drops [async][lockfree]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(4, 1, async_queue_type::lockfree);
    auto lg = std::make_shared<logger>("drop", mock);

    for (int i = 0; i < 20000; ++i) {
        details::log_msg msg("drop", level::info, "x");
        pool.post_log(std::shared_ptr<logger>(lg), msg, async_overflow_policy::discard_new);
    }
    pool.post_flush(std::shared_ptr<logger>(lg), true);
    REQUIRE(pool.discard_count() > 0);
}

TEST_CASE("lockfree thread_pool rejects overrun_oldest [async][lockfree]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(8, 1, async_queue_type::lockfree);
    auto lg = std::make_shared<logger>("reject", mock);
    details::log_msg msg("reject", level::info, "x");

    REQUIRE_THROWS_AS(
        pool.post_log(std::shared_ptr<logger>(lg), msg, async_overflow_policy::overrun_oldest),
        std::invalid_argument);
    REQUIRE(pool.overrun_count() == 0);
    REQUIRE(pool.discard_count() == 0);
}

TEST_CASE("lockfree async_logger rejects overrun_oldest [async][lockfree]") {
    shutdown();
    init_lockfree_thread_pool(32);
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<async_logger>(
        "async_lf_overrun", std::vector<sinks::sink_ptr>{mock},
        async_overflow_policy::overrun_oldest);
    REQUIRE_THROWS_AS(lg->info("must not remap to discard_new"), std::invalid_argument);
    REQUIRE(mock->message_count() == 0);
    shutdown();
}

TEST_CASE("init_lockfree_thread_pool wires global pool [async][lockfree][factory]") {
    shutdown();
    init_lockfree_thread_pool(1024);
    auto* pool = get_thread_pool();
    REQUIRE(pool != nullptr);
    REQUIRE(pool->queue_type() == async_queue_type::lockfree);
    REQUIRE(pool->worker_count() == 1);

    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<async_logger>(
        "async_lf", std::vector<sinks::sink_ptr>{mock}, async_overflow_policy::block);
    lg->info("hello lockfree async");
    lg->flush();
    REQUIRE(mock->message_count() >= 1);
    shutdown();
}
