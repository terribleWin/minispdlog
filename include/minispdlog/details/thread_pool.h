#pragma once

#include "../async_config.h"
#include "../common.h"
#include "async_msg.h"
#include "mpmc_blocking_q.h"
#include "mpsc_queue.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace minispdlog {
namespace details {

class MINISPDLOG_API thread_pool {
public:
    using item_type = async_msg;

    thread_pool(std::size_t queue_size, std::size_t threads_n,
                async_queue_type queue_type = async_queue_type::blocking);
    explicit thread_pool(const thread_pool_options& opts);

    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;
    ~thread_pool();

    // Stop workers and join. Idempotent; also called from the destructor.
    void shutdown();
    [[nodiscard]] bool active() const noexcept {
        return !shutting_down_.load(std::memory_order_acquire);
    }

    // Hot path: raw logger pointer, no shared_ptr bump. Caller must keep the
    // logger alive until queued records are processed (async_logger does this
    // in its destructor).
    void post_log(logger* worker, const log_msg& msg,
                  async_overflow_policy policy = async_overflow_policy::block);
    void post_log(std::shared_ptr<logger>&& logger_ptr, const log_msg& msg,
                  async_overflow_policy policy = async_overflow_policy::block);
    void post_log(const std::shared_ptr<logger>& logger_ptr, const log_msg& msg,
                  async_overflow_policy policy = async_overflow_policy::block) {
        post_log(std::shared_ptr<logger>(logger_ptr), msg, policy);
    }
    void post_log_nowait(logger* worker, const log_msg& msg);
    void post_log_nowait(std::shared_ptr<logger>&& logger_ptr, const log_msg& msg);

    void post_flush(logger* worker, bool wait = false);
    void post_flush(std::shared_ptr<logger>&& logger_ptr, bool wait = false);
    void post_flush(const std::shared_ptr<logger>& logger_ptr, bool wait = false) {
        post_flush(std::shared_ptr<logger>(logger_ptr), wait);
    }

    [[nodiscard]] std::size_t overrun_count() const;
    [[nodiscard]] std::size_t discard_count() const;
    [[nodiscard]] async_queue_type queue_type() const noexcept { return queue_type_; }
    [[nodiscard]] std::size_t worker_count() const noexcept { return threads_.size(); }

private:
    void worker_loop_blocking_();
    void worker_loop_lockfree_();
    bool process_msg_(async_msg& incoming);
    void enqueue_log_(async_msg&& msg, async_overflow_policy policy);
    [[nodiscard]] bool enqueue_blocking_(async_msg&& msg, async_overflow_policy policy,
                                         bool notify);
    [[nodiscard]] bool enqueue_lockfree_(async_msg&& msg, async_overflow_policy policy);
    void maybe_wake_log_() noexcept;
    void force_wake_() noexcept;
    void force_wake_all_() noexcept;
    void notify_consumer_() noexcept;
    void on_log_completed_() noexcept;
    void wait_for_pending_logs_() noexcept;

    async_queue_type queue_type_;
    std::size_t worker_count_{1};
    std::size_t wake_batch_{64};
    std::chrono::microseconds wake_interval_{100};

    std::unique_ptr<mpmc_blocking_queue<item_type>> blocking_q_;
    std::unique_ptr<mpsc_queue<item_type>> lockfree_q_;

    std::mutex park_mu_;
    std::condition_variable park_cv_;
    std::atomic<std::uint64_t> park_seq_{0};
    std::atomic<std::size_t> idle_workers_{0};
    std::atomic<std::size_t> unnotified_{0};

    std::atomic<bool> shutting_down_{false};
    std::atomic<std::size_t> discard_count_{0};
    std::atomic<std::size_t> pending_logs_{0};

    std::vector<std::thread> threads_;
};

}  // namespace details
}  // namespace minispdlog
