#include "framework/doctest.h"
#include "framework/mock_sink.h"
#include "framework/test_fixture.h"
#include "minispdlog/async.h"
#include "minispdlog/minispdlog.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace minispdlog;
using minispdlog::tests::mock_sink_mt;
using minispdlog::tests::test_fixture;

namespace {
/**
 * @brief 可由测试线程暂停的 Sink，用于稳定构造队列已满状态。
 */
class gate_sink : public sinks::base_sink<std::mutex> {
public:
    /** @brief 等待至少 `count` 条日志进入 Sink。 */
    [[nodiscard]] bool wait_until_entered(std::size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [this, count] { return entered_ >= count; });
    }

    /** @brief 允许已进入及后续的日志继续执行。 */
    void release() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            open_ = true;
        }
        cv_.notify_all();
    }

    /** @brief 返回进入 Sink 的日志数量。 */
    [[nodiscard]] std::size_t message_count() const {
        std::lock_guard<std::mutex> lock(mu_);
        return entered_;
    }

    /** @brief 返回 Sink 刷新次数。 */
    [[nodiscard]] std::size_t flush_count() const {
        std::lock_guard<std::mutex> lock(mu_);
        return flushes_;
    }

protected:
    void sink_it_(const details::log_msg&) override {
        std::unique_lock<std::mutex> lock(mu_);
        ++entered_;
        cv_.notify_all();
        cv_.wait(lock, [this] { return open_; });
    }

    void flush_() override {
        std::lock_guard<std::mutex> lock(mu_);
        ++flushes_;
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool open_{false};
    std::size_t entered_{0};
    std::size_t flushes_{0};
};

/** @brief 即使断言失败，也会在线程池析构前打开闸门。 */
class gate_release_guard {
public:
    explicit gate_release_guard(std::shared_ptr<gate_sink> sink) : sink_(std::move(sink)) {}

    ~gate_release_guard() { sink_->release(); }

    gate_release_guard(const gate_release_guard&) = delete;
    gate_release_guard& operator=(const gate_release_guard&) = delete;

private:
    std::shared_ptr<gate_sink> sink_;
};
} ///< 结束匿名命名空间。

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
        const auto payload = "msg" + std::to_string(i);
        details::log_msg msg("tp", level::info, payload);
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
    auto sink = std::make_shared<gate_sink>();
    details::thread_pool pool(2, 1);
    gate_release_guard release_on_exit(sink);
    auto lg = std::make_shared<logger>("nowait", sink);

    details::log_msg first("nowait", level::info, "first");
    pool.post_log_nowait(std::shared_ptr<logger>(lg), first);
    REQUIRE(sink->wait_until_entered(1, std::chrono::seconds(1)));

    for (int i = 0; i < 3; ++i) {
        details::log_msg msg("nowait", level::info, "x");
        pool.post_log_nowait(std::shared_ptr<logger>(lg), msg);
    }
    REQUIRE(pool.discard_count() == 1);
    REQUIRE(pool.overrun_count() == 0);
    sink->release();
    pool.post_flush(std::shared_ptr<logger>(lg), true);
    REQUIRE(sink->message_count() == 3);
}

TEST_CASE("thread_pool overrun_oldest drops oldest [async][thread_pool]") {
    auto sink = std::make_shared<gate_sink>();
    details::thread_pool pool(2, 1);
    gate_release_guard release_on_exit(sink);
    auto lg = std::make_shared<logger>("overrun", sink);

    details::log_msg first("overrun", level::info, "first");
    pool.post_log(std::shared_ptr<logger>(lg), first, async_overflow_policy::overrun_oldest);
    REQUIRE(sink->wait_until_entered(1, std::chrono::seconds(1)));

    for (int i = 0; i < 3; ++i) {
        details::log_msg msg("overrun", level::info, "test");
        pool.post_log(std::shared_ptr<logger>(lg), msg, async_overflow_policy::overrun_oldest);
    }
    REQUIRE(pool.overrun_count() == 1);
    REQUIRE(pool.discard_count() == 0);

    sink->release();
    pool.post_flush(std::shared_ptr<logger>(lg), true);
    REQUIRE(sink->message_count() == 3);
}

TEST_CASE("thread_pool waiting flush observes pending log completion [async][thread_pool][flush]") {
    auto sink = std::make_shared<gate_sink>();
    details::thread_pool pool(2, 1);
    gate_release_guard release_on_exit(sink);
    auto lg = std::make_shared<logger>("pending", sink);

    details::log_msg msg("pending", level::info, "held");
    pool.post_log(std::shared_ptr<logger>(lg), msg);
    REQUIRE(sink->wait_until_entered(1, std::chrono::seconds(1)));

    auto flush = std::async(std::launch::async, [&pool, lg] { pool.post_flush(std::shared_ptr<logger>(lg), true); });
    REQUIRE(flush.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);

    sink->release();
    REQUIRE(flush.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    flush.get();
    REQUIRE(sink->message_count() == 1);
    REQUIRE(sink->flush_count() >= 1);
}

TEST_CASE("overrun_oldest preserves flush and pending accounting [async][thread_pool][flush]") {
    auto gate = std::make_shared<gate_sink>();
    auto pool = std::make_shared<details::thread_pool>(1, 1);
    gate_release_guard release_on_exit(gate);
    auto gate_logger = std::make_shared<logger>("overrun-gate", gate);
    auto control_sink = std::make_shared<mock_sink_mt>();
    auto control_logger = std::make_shared<logger>("overrun-control", control_sink);

    details::log_msg held("overrun-gate", level::info, "held");
    pool->post_log(gate_logger, held);
    REQUIRE(gate->wait_until_entered(1, std::chrono::seconds(1)));

    /// 非等待 flush 在返回前已确定入队，因此此处无需依赖调度时序。
    pool->post_flush(control_logger, false);
    details::log_msg replacement("overrun-gate", level::info, "replacement");
    pool->post_log(gate_logger, replacement, async_overflow_policy::overrun_oldest);

    auto completed = std::make_shared<std::atomic<bool>>(false);
    std::promise<void> invocation;
    auto invoked = invocation.get_future();
    std::thread waiter([pool, gate_logger, completed, signal = std::move(invocation)]() mutable {
        signal.set_value();
        pool->post_flush(gate_logger, true);
        completed->store(true, std::memory_order_release);
    });
    invoked.wait();
    gate->release();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!completed->load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool finished = completed->load(std::memory_order_acquire);
    if (finished) {
        waiter.join();
    } else {
        /// 失败路径不在析构阶段永久卡住；所有被引用对象均由线程共享持有。
        waiter.detach();
    }

    REQUIRE(finished);
    REQUIRE(control_sink->flush_count() == 1);
    REQUIRE(pool->overrun_count() == 0);
    REQUIRE(pool->discard_count() == 1);
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

TEST_CASE("get_thread_pool snapshot survives cross-thread reconfigure [async][thread_pool][thread]") {
    shutdown();
    init_thread_pool(64, 1);
    auto* first = get_thread_pool();
    REQUIRE(first != nullptr);
    REQUIRE(first->active());

    std::thread replace([] { init_thread_pool(128, 1); });
    replace.join();

    /// 当前线程在下次查询前持有旧快照，跨线程替换后指针仍可安全观察。
    REQUIRE_FALSE(first->active());
    auto* second = get_thread_pool();
    REQUIRE(second != nullptr);
    REQUIRE(second->active());

    std::thread stop([] { thread_pool_manager::instance().shutdown(); });
    stop.join();
    REQUIRE_FALSE(second->active());
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

TEST_CASE("async_logger switches to synchronous delivery after shutdown [async][shutdown]") {
    shutdown();
    init_thread_pool(64, 1);

    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<async_logger>("after_shutdown", std::vector<sinks::sink_ptr>{mock});
    lg->info("queued");
    lg->flush();
    REQUIRE(mock->message_count() == 1);

    shutdown();
    const auto flushes_before = mock->flush_count();
    lg->info("synchronous fallback");
    REQUIRE(mock->message_count() == 2);
    REQUIRE(mock->last_contains("synchronous fallback"));
    lg->flush();
    REQUIRE(mock->flush_count() == flushes_before + 1);
}

TEST_CASE("thread_pool block policy leaves overflow counters unchanged [async][thread_pool]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(5, 1);
    auto lg = std::make_shared<logger>("count", mock);

    for (int i = 0; i < 20; ++i) {
        details::log_msg msg("count", level::info, "test");
        pool.post_log(std::shared_ptr<logger>(lg), msg, async_overflow_policy::block);
    }
    pool.post_flush(std::shared_ptr<logger>(lg), true);
    REQUIRE(mock->message_count() == 20);
    REQUIRE(pool.overrun_count() == 0);
    REQUIRE(pool.discard_count() == 0);
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
        const auto payload = "lockfree-" + std::to_string(i);
        details::log_msg msg("lf", level::info, payload);
        pool.post_log(std::shared_ptr<logger>(lg), msg, async_overflow_policy::block);
    }
    pool.post_flush(std::shared_ptr<logger>(lg), true);
    REQUIRE(mock->message_count() == static_cast<std::size_t>(n));
}

TEST_CASE("lockfree thread_pool discard_new counts drops [async][lockfree]") {
    auto sink = std::make_shared<gate_sink>();
    details::thread_pool pool(4, 1, async_queue_type::lockfree);
    gate_release_guard release_on_exit(sink);
    auto lg = std::make_shared<logger>("drop", sink);

    details::log_msg first("drop", level::info, "first");
    pool.post_log(std::shared_ptr<logger>(lg), first, async_overflow_policy::discard_new);
    REQUIRE(sink->wait_until_entered(1, std::chrono::seconds(1)));

    for (int i = 0; i < 5; ++i) {
        details::log_msg msg("drop", level::info, "x");
        pool.post_log(std::shared_ptr<logger>(lg), msg, async_overflow_policy::discard_new);
    }
    REQUIRE(pool.discard_count() == 1);
    sink->release();
    pool.post_flush(std::shared_ptr<logger>(lg), true);
    REQUIRE(sink->message_count() == 5);
}

TEST_CASE("lockfree thread_pool rejects overrun_oldest [async][lockfree]") {
    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(8, 1, async_queue_type::lockfree);
    auto lg = std::make_shared<logger>("reject", mock);
    details::log_msg msg("reject", level::info, "x");

    REQUIRE_THROWS_AS(pool.post_log(std::shared_ptr<logger>(lg), msg, async_overflow_policy::overrun_oldest),
                      std::invalid_argument);
    REQUIRE(pool.overrun_count() == 0);
    REQUIRE(pool.discard_count() == 0);
}

TEST_CASE("lockfree async_logger rejects overrun_oldest [async][lockfree]") {
    shutdown();
    init_lockfree_thread_pool(32);
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<async_logger>("async_lf_overrun", std::vector<sinks::sink_ptr>{mock},
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
    auto lg =
        std::make_shared<async_logger>("async_lf", std::vector<sinks::sink_ptr>{mock}, async_overflow_policy::block);
    lg->info("hello lockfree async");
    lg->flush();
    REQUIRE(mock->message_count() >= 1);
    shutdown();
}

TEST_CASE("async payload shorter than inline slot is delivered intact [async]") {
    shutdown();
    init_thread_pool(1024, 1);
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_pattern("%n %v");
    auto lg = std::make_shared<async_logger>("short", std::vector<sinks::sink_ptr>{mock});
    lg->info("hi");
    lg->flush();
    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->at(0) == "short hi\n");
    shutdown();
}

TEST_CASE("async payload larger than inline slot is delivered intact [async]") {
    shutdown();
    init_thread_pool(1024, 1);
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_pattern("%v");
    auto lg = std::make_shared<async_logger>("long", std::vector<sinks::sink_ptr>{mock});
    const std::string payload(200, 'x');
    lg->info("{}", payload);
    lg->flush();
    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->at(0) == payload + "\n");
    shutdown();
}

TEST_CASE("async_logger destructor drains without an explicit flush [async]") {
    shutdown();
    init_thread_pool(1024, 1);
    auto mock = std::make_shared<mock_sink_mt>();
    mock->set_pattern("%v");
    {
        auto lg = std::make_shared<async_logger>("dtor", std::vector<sinks::sink_ptr>{mock});
        lg->info("from-dtor");
        lg.reset();
    }
    REQUIRE(mock->message_count() == 1);
    REQUIRE(mock->at(0) == "from-dtor\n");
    shutdown();
}

namespace {

bool wait_for_count(const std::shared_ptr<mock_sink_mt>& mock, std::size_t n, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (mock->message_count() < n) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void require_wake_interval_delivery(async_queue_type queue_type) {
    thread_pool_options opts;
    opts.queue_size = 1024;
    opts.thread_count = 1;
    opts.queue_type = queue_type;
    opts.wake_batch = 100000;
    opts.wake_interval_us = 100;

    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(opts);
    auto lg = std::make_shared<logger>("wake", mock);
    details::log_msg msg("wake", level::info, "no-flush");
    pool.post_log(lg, msg);

    REQUIRE(wait_for_count(mock, 1, std::chrono::milliseconds(50)));
    REQUIRE(mock->last_contains("no-flush"));
}

} ///< 结束匿名命名空间。

TEST_CASE("blocking pool delivers a log without flush via N/T wake [async][wake]") {
    require_wake_interval_delivery(async_queue_type::blocking);
}

TEST_CASE("lockfree pool delivers a log without flush via N/T wake [async][wake][lockfree]") {
    require_wake_interval_delivery(async_queue_type::lockfree);
}

TEST_CASE("blocking pool delivers a short burst without flush [async][wake]") {
    thread_pool_options opts;
    opts.queue_size = 1024;
    opts.thread_count = 1;
    opts.wake_batch = 64;
    opts.wake_interval_us = 100;

    auto mock = std::make_shared<mock_sink_mt>();
    details::thread_pool pool(opts);
    auto lg = std::make_shared<logger>("burst", mock);
    const int n = 3;
    for (int i = 0; i < n; ++i) {
        details::log_msg msg("burst", level::info, "b");
        pool.post_log(lg, msg);
    }
    REQUIRE(wait_for_count(mock, static_cast<std::size_t>(n), std::chrono::milliseconds(50)));
}

TEST_CASE("init_thread_pool options keep wake_batch [async][wake][factory]") {
    shutdown();
    thread_pool_options opts;
    opts.queue_size = 1024;
    opts.thread_count = 1;
    opts.wake_batch = 32;
    opts.wake_interval_us = 50;
    init_thread_pool(opts);

    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<async_logger>("opts", std::vector<sinks::sink_ptr>{mock});
    lg->info("via-opts");
    REQUIRE(wait_for_count(mock, 1, std::chrono::milliseconds(50)));
    lg->flush();
    shutdown();
}
