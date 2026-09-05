#include "minispdlog/details/thread_pool.h"
#include "minispdlog/logger.h"

#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <utility>

namespace minispdlog {
namespace details {

thread_pool::thread_pool(std::size_t queue_size, std::size_t threads_n,
                         async_queue_type queue_type)
    : queue_type_(queue_type) {
    if (threads_n == 0 || threads_n > 1000) {
        throw std::invalid_argument("thread_pool: threads_n must be 1-1000");
    }
    if (queue_size == 0) {
        throw std::invalid_argument("thread_pool: queue_size must be > 0");
    }

    if (queue_type_ == async_queue_type::lockfree) {
        if (threads_n != 1) {
            threads_n = 1;
        }
        lockfree_q_ = std::make_unique<mpsc_queue<item_type>>(queue_size);
        threads_.emplace_back([this] { worker_loop_lockfree_(); });
    } else {
        blocking_q_ = std::make_unique<mpmc_blocking_queue<item_type>>(queue_size);
        for (std::size_t i = 0; i < threads_n; ++i) {
            threads_.emplace_back([this] { worker_loop_blocking_(); });
        }
    }
}

thread_pool::~thread_pool() {
    try {
        shutting_down_.store(true, std::memory_order_release);
        notify_consumer_();

        if (queue_type_ == async_queue_type::lockfree) {
            async_msg terminate_msg(async_msg_type::terminate);
            static_cast<void>(
                enqueue_lockfree_(std::move(terminate_msg), async_overflow_policy::block));
        } else {
            for (std::size_t i = 0; i < threads_.size(); ++i) {
                async_msg terminate_msg(async_msg_type::terminate);
                blocking_q_->enqueue(std::move(terminate_msg));
            }
        }

        for (auto& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
    } catch (...) {
    }
}

void thread_pool::notify_consumer_() noexcept {
    notify_seq_.fetch_add(1, std::memory_order_release);
    notify_seq_.notify_one();
}

void thread_pool::on_log_completed_() noexcept {
    pending_logs_.fetch_sub(1, std::memory_order_release);
    pending_logs_.notify_all();
}

void thread_pool::wait_for_pending_logs_() noexcept {
    for (;;) {
        const auto pending = pending_logs_.load(std::memory_order_acquire);
        if (pending == 0) {
            return;
        }
        pending_logs_.wait(pending, std::memory_order_acquire);
    }
}

bool thread_pool::enqueue_blocking_(async_msg&& msg, async_overflow_policy policy) {
    switch (policy) {
        case async_overflow_policy::overrun_oldest: {
            const auto before = blocking_q_->overrun_count();
            blocking_q_->enqueue_nowait(std::move(msg));
            const auto dropped = blocking_q_->overrun_count() - before;
            if (dropped > 0) {
                pending_logs_.fetch_sub(dropped, std::memory_order_release);
                pending_logs_.notify_all();
            }
            return true;
        }
        case async_overflow_policy::discard_new:
            if (!blocking_q_->try_enqueue(std::move(msg))) {
                discard_count_.fetch_add(1, std::memory_order_relaxed);
                msg.notify_ack();
                return false;
            }
            return true;
        case async_overflow_policy::block:
        default:
            blocking_q_->enqueue(std::move(msg));
            return true;
    }
}

bool thread_pool::enqueue_lockfree_(async_msg&& msg, async_overflow_policy policy) {
    if (policy == async_overflow_policy::overrun_oldest) {
        throw std::invalid_argument(
            "thread_pool: overrun_oldest is not supported on the lock-free backend; "
            "use block or discard_new");
    }

    if (policy == async_overflow_policy::discard_new) {
        if (!lockfree_q_->push(std::move(msg))) {
            discard_count_.fetch_add(1, std::memory_order_relaxed);
            msg.notify_ack();
            return false;
        }
        notify_consumer_();
        return true;
    }

    while (!lockfree_q_->push(std::move(msg))) {
        if (shutting_down_.load(std::memory_order_acquire)) {
            discard_count_.fetch_add(1, std::memory_order_relaxed);
            msg.notify_ack();
            return false;
        }
        std::this_thread::yield();
    }
    notify_consumer_();
    return true;
}

void thread_pool::post_log(std::shared_ptr<logger>&& logger_ptr, const log_msg& msg,
                           async_overflow_policy policy) {
    if (queue_type_ == async_queue_type::lockfree &&
        policy == async_overflow_policy::overrun_oldest) {
        throw std::invalid_argument(
            "thread_pool: overrun_oldest is not supported on the lock-free backend; "
            "use block or discard_new");
    }

    async_msg async_m(async_msg_type::log, std::move(logger_ptr), msg);
    pending_logs_.fetch_add(1, std::memory_order_relaxed);
    const bool queued = (queue_type_ == async_queue_type::lockfree)
                            ? enqueue_lockfree_(std::move(async_m), policy)
                            : enqueue_blocking_(std::move(async_m), policy);
    if (!queued) {
        on_log_completed_();
    }
}

void thread_pool::post_log_nowait(std::shared_ptr<logger>&& logger_ptr, const log_msg& msg) {
    post_log(std::move(logger_ptr), msg, async_overflow_policy::discard_new);
}

void thread_pool::post_flush(std::shared_ptr<logger>&& logger_ptr, bool wait) {
    async_msg flush_msg(async_msg_type::flush, std::move(logger_ptr));
    std::future<void> done;
    if (wait) {
        flush_msg.ack = std::make_shared<std::promise<void>>();
        done = flush_msg.ack->get_future();
    }

    if (queue_type_ == async_queue_type::lockfree) {
        static_cast<void>(
            enqueue_lockfree_(std::move(flush_msg), async_overflow_policy::block));
    } else {
        blocking_q_->enqueue(std::move(flush_msg));
    }

    if (wait) {
        done.wait();
        wait_for_pending_logs_();
    }
}

std::size_t thread_pool::overrun_count() const {
    if (blocking_q_) {
        return blocking_q_->overrun_count();
    }
    return 0;
}

std::size_t thread_pool::discard_count() const {
    return discard_count_.load(std::memory_order_relaxed);
}

void thread_pool::worker_loop_blocking_() {
    while (true) {
        async_msg incoming;
        if (!blocking_q_->dequeue_for(incoming, std::chrono::seconds(10))) {
            continue;
        }
        if (!process_msg_(incoming)) {
            break;
        }
    }
}

void thread_pool::worker_loop_lockfree_() {
    async_msg incoming;
    while (true) {
        if (lockfree_q_->pop(incoming)) {
            if (!process_msg_(incoming)) {
                break;
            }
            continue;
        }

        const auto seq = notify_seq_.load(std::memory_order_acquire);
        if (lockfree_q_->pop(incoming)) {
            if (!process_msg_(incoming)) {
                break;
            }
            continue;
        }
        notify_seq_.wait(seq, std::memory_order_acquire);

        if (shutting_down_.load(std::memory_order_acquire) && lockfree_q_->empty()) {
            if (!lockfree_q_->pop(incoming)) {
                break;
            }
            if (!process_msg_(incoming)) {
                break;
            }
        }
    }
}

bool thread_pool::process_msg_(async_msg& incoming) {
    switch (incoming.msg_type) {
        case async_msg_type::log:
            if (incoming.worker_ptr) {
                incoming.worker_ptr->backend_sink_it_(incoming);
            }
            on_log_completed_();
            return true;
        case async_msg_type::flush:
            if (incoming.worker_ptr) {
                incoming.worker_ptr->backend_flush_();
            }
            incoming.notify_ack();
            return true;
        case async_msg_type::terminate:
            return false;
    }
    return true;
}

}  // namespace details
}  // namespace minispdlog
