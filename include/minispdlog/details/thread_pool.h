#pragma once

#include "../async_config.h"
#include "../common.h"
#include "async_msg.h"
#include "mpmc_blocking_q.h"
#include "mpsc_queue.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace minispdlog {
namespace details {

class MINISPDLOG_API thread_pool {
public:
    using item_type = async_msg;

    thread_pool(std::size_t queue_size, std::size_t threads_n,
                async_queue_type queue_type = async_queue_type::blocking);

    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;
    ~thread_pool();

    void post_log(std::shared_ptr<logger>&& logger_ptr, const log_msg& msg,
                  async_overflow_policy policy = async_overflow_policy::block);
    void post_log_nowait(std::shared_ptr<logger>&& logger_ptr, const log_msg& msg);
    // If wait is true, blocks until queued logs are written (including in-flight
    // records on other workers).
    void post_flush(std::shared_ptr<logger>&& logger_ptr, bool wait = false);

    [[nodiscard]] std::size_t overrun_count() const;
    [[nodiscard]] std::size_t discard_count() const;
    [[nodiscard]] async_queue_type queue_type() const noexcept { return queue_type_; }
    [[nodiscard]] std::size_t worker_count() const noexcept { return threads_.size(); }

private:
    void worker_loop_blocking_();
    void worker_loop_lockfree_();
    bool process_msg_(async_msg& incoming);
    [[nodiscard]] bool enqueue_blocking_(async_msg&& msg, async_overflow_policy policy);
    [[nodiscard]] bool enqueue_lockfree_(async_msg&& msg, async_overflow_policy policy);
    void notify_consumer_() noexcept;
    void on_log_completed_() noexcept;
    void wait_for_pending_logs_() noexcept;

    async_queue_type queue_type_;
    std::unique_ptr<mpmc_blocking_queue<item_type>> blocking_q_;
    std::unique_ptr<mpsc_queue<item_type>> lockfree_q_;

    alignas(64) std::atomic<std::size_t> notify_seq_{0};
    std::atomic<bool> shutting_down_{false};
    std::atomic<std::size_t> discard_count_{0};
    std::atomic<std::size_t> pending_logs_{0};

    std::vector<std::thread> threads_;
};

}  // namespace details
}  // namespace minispdlog
