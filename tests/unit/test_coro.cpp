#include "framework/doctest.h"
#include "framework/mock_sink.h"
#include "minispdlog/async.h"
#include "minispdlog/coro.h"
#include "minispdlog/registry.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace minispdlog;
using minispdlog::tests::mock_sink_mt;

namespace {

/** @brief 测试专用自动销毁协程；用 promise/future 观察异步完成。 */
struct detached_task {
    struct promise_type {
        detached_task get_return_object() const noexcept { return {}; }
        std::suspend_never initial_suspend() const noexcept { return {}; }
        std::suspend_never final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}
        void unhandled_exception() const noexcept { std::terminate(); }
    };
};

detached_task wait_for_flush(std::shared_ptr<logger> target, coro::resume_scheduler scheduler,
                             std::promise<void>* completed, std::exception_ptr* error,
                             std::thread::id* resumed_on = nullptr) {
    try {
        co_await coro::flush(std::move(target), std::move(scheduler));
    } catch (...) {
        *error = std::current_exception();
    }
    if (resumed_on != nullptr) {
        *resumed_on = std::this_thread::get_id();
    }
    completed->set_value();
}

detached_task wait_for_flush_and_observe_release(std::shared_ptr<logger> target, std::weak_ptr<logger> observed,
                                                 std::promise<void>* completed, std::exception_ptr* error,
                                                 bool* released_before_resume) {
    try {
        co_await coro::flush(std::move(target));
    } catch (...) {
        *error = std::current_exception();
    }
    *released_before_resume = observed.expired();
    completed->set_value();
}

detached_task wait_for_flush_all(std::promise<void>* completed, std::exception_ptr* error) {
    try {
        co_await coro::flush_all();
    } catch (...) {
        *error = std::current_exception();
    }
    completed->set_value();
}

detached_task wait_for_shutdown(std::promise<void>* completed, std::exception_ptr* error) {
    try {
        co_await coro::shutdown();
    } catch (...) {
        *error = std::current_exception();
    }
    completed->set_value();
}

/** @brief 通过门闩控制 flush 完成时机。 */
class gate_sink final : public sinks::sink {
public:
    void log(const details::log_msg&) override { ++records_; }

    void flush() override {
        std::unique_lock<std::mutex> lock(mutex_);
        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this] { return released_; });
    }

    void set_level(level value) override { level_.store(value, std::memory_order_relaxed); }
    level get_level() const override { return level_.load(std::memory_order_relaxed); }
    bool should_log(level value) const override { return value >= get_level(); }
    void set_pattern(std::string) override {}
    void set_formatter(std::unique_ptr<formatter>) override {}

    bool wait_until_entered(std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, timeout, [this] { return entered_; });
    }

    bool entered() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entered_;
    }

    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        changed_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<level> level_{level::trace};
    std::atomic<std::size_t> records_{0};
    bool entered_{false};
    bool released_{false};
};

/** @brief 可切换为抛异常的 Sink，用于验证异步工作线程不会退出。 */
class throwing_flush_sink final : public sinks::sink {
public:
    void log(const details::log_msg&) override { ++records_; }

    void flush() override {
        ++flushes_;
        if (throw_on_flush_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("flush failed");
        }
    }

    void set_level(level value) override { level_.store(value, std::memory_order_relaxed); }
    level get_level() const override { return level_.load(std::memory_order_relaxed); }
    bool should_log(level value) const override { return value >= get_level(); }
    void set_pattern(std::string) override {}
    void set_formatter(std::unique_ptr<formatter>) override {}

    std::atomic<bool> throw_on_flush_{true};
    std::atomic<std::size_t> records_{0};
    std::atomic<std::size_t> flushes_{0};

private:
    std::atomic<level> level_{level::trace};
};

struct thread_pool_guard {
    ~thread_pool_guard() { thread_pool_manager::instance().shutdown(); }
};

/** @brief 保证测试注册的 logger 在离开作用域时被移除。 */
struct registry_entry_guard {
    explicit registry_entry_guard(std::string logger_name) : logger_name_(std::move(logger_name)) {}
    ~registry_entry_guard() { registry::instance().drop(logger_name_); }

    std::string logger_name_;
};

} // namespace

TEST_CASE("coroutine flush suspends without blocking the caller [coro][async]") {
    auto sink = std::make_shared<gate_sink>();
    auto target = std::make_shared<logger>("coro-gate", sink);
    std::promise<void> completed;
    auto done = completed.get_future();
    std::exception_ptr error;
    std::thread::id resumed_on;
    const auto caller = std::this_thread::get_id();

    wait_for_flush(target, {}, &completed, &error, &resumed_on);

    REQUIRE(sink->wait_until_entered());
    CHECK(done.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    sink->release();
    REQUIRE(done.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(error == nullptr);
    CHECK(resumed_on != caller);
}

TEST_CASE("coroutine flush keeps logger alive while suspended [coro][lifetime]") {
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto sink = std::make_shared<gate_sink>();
        auto target = std::make_shared<logger>("coro-lifetime-" + std::to_string(attempt), sink);
        std::weak_ptr<logger> weak = target;
        std::promise<void> completed;
        auto done = completed.get_future();
        std::exception_ptr error;
        bool released_before_resume = false;

        wait_for_flush_and_observe_release(target, weak, &completed, &error, &released_before_resume);
        target.reset();

        REQUIRE(sink->wait_until_entered());
        CHECK_FALSE(weak.expired());
        sink->release();
        REQUIRE(done.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        CHECK(error == nullptr);
        CHECK(released_before_resume);
        CHECK(weak.expired());
    }
}

TEST_CASE("coroutine flush supports a caller supplied resume scheduler [coro][scheduler]") {
    auto sink = std::make_shared<mock_sink_mt>();
    auto target = std::make_shared<logger>("coro-scheduler", sink);
    std::mutex mutex;
    std::condition_variable queued;
    std::deque<coro::resume_callback> continuations;
    std::promise<void> completed;
    auto done = completed.get_future();
    std::exception_ptr error;

    auto scheduler = [&](coro::resume_callback continuation) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            continuations.push_back(continuation);
        }
        queued.notify_one();
    };
    wait_for_flush(target, scheduler, &completed, &error);

    coro::resume_callback continuation;
    {
        std::unique_lock<std::mutex> lock(mutex);
        REQUIRE(queued.wait_for(lock, std::chrono::seconds(2), [&] { return !continuations.empty(); }));
        continuation = continuations.front();
        continuations.pop_front();
    }
    CHECK(done.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    continuation();
    REQUIRE(done.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(error == nullptr);
}

TEST_CASE("coroutine scheduler exception cannot resume a destroyed frame twice [coro][scheduler][error]") {
    auto sink = std::make_shared<mock_sink_mt>();
    auto target = std::make_shared<logger>("coro-scheduler-error", sink);
    coro::resume_callback queued_resume;
    std::promise<void> completed;
    auto done = completed.get_future();
    std::exception_ptr error;

    auto scheduler = [&](coro::resume_callback resume) {
        queued_resume = resume;
        throw std::runtime_error("resume scheduling failed");
    };
    wait_for_flush(target, scheduler, &completed, &error);

    REQUIRE(done.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    REQUIRE(error != nullptr);
    CHECK_THROWS_WITH(std::rethrow_exception(error), "resume scheduling failed");
    REQUIRE(static_cast<bool>(queued_resume));
    /// 调度器入队后抛异常时，延迟回调只能成为空操作。
    CHECK_NOTHROW(queued_resume());
}

TEST_CASE("coroutine flush rejects a null logger at the resume point [coro][error]") {
    std::promise<void> completed;
    auto done = completed.get_future();
    std::exception_ptr error;

    wait_for_flush(nullptr, {}, &completed, &error);

    REQUIRE(done.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    REQUIRE(error != nullptr);
    CHECK_THROWS_AS(std::rethrow_exception(error), std::invalid_argument);
    CHECK_THROWS_WITH(std::rethrow_exception(error), "minispdlog::coro::flush requires a logger");
}

TEST_CASE("coroutine flush_all refreshes registered loggers [coro][registry]") {
    auto sink = std::make_shared<mock_sink_mt>();
    auto target = std::make_shared<logger>("coro-flush-all", sink);
    registry::instance().register_logger(target);
    registry_entry_guard cleanup(target->name());
    std::promise<void> completed;
    auto done = completed.get_future();
    std::exception_ptr error;

    wait_for_flush_all(&completed, &error);

    REQUIRE(done.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(error == nullptr);
    CHECK(sink->flush_count() == 1);
}

TEST_CASE("coroutine control operations execute in FIFO order [coro][ordering]") {
    auto first_sink = std::make_shared<gate_sink>();
    auto second_sink = std::make_shared<gate_sink>();
    auto first = std::make_shared<logger>("coro-first", first_sink);
    auto second = std::make_shared<logger>("coro-second", second_sink);
    std::promise<void> first_completed;
    std::promise<void> second_completed;
    auto first_done = first_completed.get_future();
    auto second_done = second_completed.get_future();
    std::exception_ptr first_error;
    std::exception_ptr second_error;

    wait_for_flush(first, {}, &first_completed, &first_error);
    REQUIRE(first_sink->wait_until_entered());
    wait_for_flush(second, {}, &second_completed, &second_error);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(second_sink->entered());
    first_sink->release();
    REQUIRE(first_done.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    REQUIRE(second_sink->wait_until_entered());
    second_sink->release();
    REQUIRE(second_done.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(first_error == nullptr);
    CHECK(second_error == nullptr);
}

TEST_CASE("async flush exception reaches coroutine and worker remains usable [coro][async][error]") {
    init_thread_pool(32, 1);
    thread_pool_guard guard;
    auto sink = std::make_shared<throwing_flush_sink>();
    std::vector<sinks::sink_ptr> sinks{sink};
    auto target = std::make_shared<async_logger>("coro-error", std::move(sinks));
    std::promise<void> completed;
    auto done = completed.get_future();
    std::exception_ptr error;

    target->info("before-error");
    wait_for_flush(target, {}, &completed, &error);
    REQUIRE(done.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    REQUIRE(error != nullptr);
    CHECK_THROWS_WITH(std::rethrow_exception(error), "flush failed");

    sink->throw_on_flush_.store(false, std::memory_order_relaxed);
    target->info("after-error");
    CHECK_NOTHROW(target->flush());
    CHECK(sink->records_.load(std::memory_order_relaxed) == 2);
}

TEST_CASE("coroutine shutdown drains the pool and clears it [coro][async][shutdown]") {
    init_thread_pool(64, 1);
    auto sink = std::make_shared<mock_sink_mt>();
    std::vector<sinks::sink_ptr> sinks{sink};
    auto target = std::make_shared<async_logger>("coro-shutdown", std::move(sinks));
    for (int i = 0; i < 20; ++i) {
        target->info("record {}", i);
    }
    std::promise<void> completed;
    auto done = completed.get_future();
    std::exception_ptr error;

    wait_for_shutdown(&completed, &error);

    REQUIRE(done.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    CHECK(error == nullptr);
    CHECK(get_thread_pool() == nullptr);
    CHECK(sink->message_count() == 20);
}
