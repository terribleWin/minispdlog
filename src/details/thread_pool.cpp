#include "minispdlog/details/thread_pool.h"
#include "minispdlog/logger.h"

#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <utility>

namespace minispdlog {
namespace details {

namespace {
constexpr auto kLongIdle = std::chrono::seconds(10);
}  // namespace

thread_pool::thread_pool(std::size_t queue_size, std::size_t threads_n,
                         async_queue_type queue_type)
    : thread_pool(thread_pool_options{queue_size, threads_n, queue_type}) {}

thread_pool::thread_pool(const thread_pool_options& opts)
    : queue_type_(opts.queue_type) {
    auto threads_n = opts.thread_count;
    const auto queue_size = opts.queue_size;
    if (threads_n == 0 || threads_n > 1000) {
        throw std::invalid_argument("thread_pool: threads_n must be 1-1000");
    }
    if (queue_size == 0) {
        throw std::invalid_argument("thread_pool: queue_size must be > 0");
    }

    wake_batch_ = opts.wake_batch == 0 ? 1 : opts.wake_batch;
    if (opts.wake_interval_us == 0) {
        wake_batch_ = 1;
        wake_interval_ = std::chrono::microseconds(1);
        wake_interval_ns_ = 0;
    } else {
        wake_interval_ = std::chrono::microseconds(opts.wake_interval_us);
        wake_interval_ns_ = static_cast<std::uint64_t>(opts.wake_interval_us) * 1000ull;
    }

    if (queue_type_ == async_queue_type::lockfree) {
        threads_n = 1;
        lockfree_q_ = std::make_unique<mpsc_queue<item_type>>(queue_size);
        worker_count_ = 1;
        threads_.emplace_back([this] { worker_loop_lockfree_(); });
    } else {
        blocking_q_ = std::make_unique<mpmc_blocking_queue<item_type>>(queue_size);
        worker_count_ = threads_n;
        for (std::size_t i = 0; i < threads_n; ++i) {
            threads_.emplace_back([this] { worker_loop_blocking_(); });
        }
    }
}

void thread_pool::shutdown() {
    const bool already = shutting_down_.exchange(true, std::memory_order_acq_rel);
    if (!already) {
        force_wake_all_();
        try {
            if (queue_type_ == async_queue_type::lockfree) {
                async_msg terminate_msg(async_msg_type::terminate);
                static_cast<void>(
                    enqueue_lockfree_(std::move(terminate_msg), async_overflow_policy::block));
                force_wake_all_();
            } else if (blocking_q_) {
                for (std::size_t i = 0; i < threads_.size(); ++i) {
                    async_msg terminate_msg(async_msg_type::terminate);
                    blocking_q_->enqueue(std::move(terminate_msg), true);
                }
                force_wake_all_();
            }
        } catch (...) {
        }
    }

    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

thread_pool::~thread_pool() {
    try {
        shutdown();
    } catch (...) {
    }
}

std::uint64_t thread_pool::now_ns_() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void thread_pool::notify_consumer_() noexcept {
    park_seq_.fetch_add(1, std::memory_order_release);
    if (lockfree_q_) {
        park_cv_.notify_one();
    }
    if (blocking_q_) {
        blocking_q_->wake_one();
    }
}

void thread_pool::force_wake_() noexcept {
    unnotified_.store(0, std::memory_order_relaxed);
    notify_consumer_();
}

void thread_pool::force_wake_all_() noexcept {
    unnotified_.store(0, std::memory_order_relaxed);
    park_seq_.fetch_add(1, std::memory_order_release);
    if (lockfree_q_) {
        park_cv_.notify_all();
    }
    if (blocking_q_) {
        blocking_q_->wake_all();
    }
}

void thread_pool::maybe_wake_log_() noexcept {
    const auto n = unnotified_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1) {
        batch_start_ns_.store(now_ns_(), std::memory_order_relaxed);
    }

    const auto idle = idle_workers_.load(std::memory_order_relaxed);
    if (idle >= worker_count_ || n >= wake_batch_) {
        unnotified_.store(0, std::memory_order_relaxed);
        notify_consumer_();
        return;
    }
    if (wake_interval_ns_ == 0) {
        unnotified_.store(0, std::memory_order_relaxed);
        notify_consumer_();
        return;
    }

    const auto start = batch_start_ns_.load(std::memory_order_relaxed);
    if (now_ns_() - start >= wake_interval_ns_) {
        unnotified_.store(0, std::memory_order_relaxed);
        notify_consumer_();
    }
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

bool thread_pool::enqueue_blocking_(async_msg&& msg, async_overflow_policy policy, bool notify) {
    switch (policy) {
        case async_overflow_policy::overrun_oldest: {
            const auto before = blocking_q_->overrun_count();
            blocking_q_->enqueue_nowait(std::move(msg), notify);
            const auto dropped = blocking_q_->overrun_count() - before;
            if (dropped > 0) {
                pending_logs_.fetch_sub(dropped, std::memory_order_release);
                pending_logs_.notify_all();
            }
            return true;
        }
        case async_overflow_policy::discard_new:
            if (!blocking_q_->try_enqueue(std::move(msg), notify)) {
                discard_count_.fetch_add(1, std::memory_order_relaxed);
                msg.notify_ack();
                return false;
            }
            return true;
        case async_overflow_policy::block:
        default:
            blocking_q_->enqueue(std::move(msg), notify);
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
    return true;
}

void thread_pool::enqueue_log_(async_msg&& msg, async_overflow_policy policy) {
    if (queue_type_ == async_queue_type::lockfree &&
        policy == async_overflow_policy::overrun_oldest) {
        throw std::invalid_argument(
            "thread_pool: overrun_oldest is not supported on the lock-free backend; "
            "use block or discard_new");
    }

    pending_logs_.fetch_add(1, std::memory_order_relaxed);
    const bool queued = (queue_type_ == async_queue_type::lockfree)
                            ? enqueue_lockfree_(std::move(msg), policy)
                            : enqueue_blocking_(std::move(msg), policy, false);
    if (!queued) {
        on_log_completed_();
        return;
    }
    maybe_wake_log_();
}

void thread_pool::post_log(logger* worker, const log_msg& msg, async_overflow_policy policy) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        if (worker != nullptr) {
            worker->backend_sink_it_(msg);
        }
        return;
    }
    enqueue_log_(async_msg(async_msg_type::log, worker, msg), policy);
}

void thread_pool::post_log(std::shared_ptr<logger>&& logger_ptr, const log_msg& msg,
                           async_overflow_policy policy) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        if (logger_ptr) {
            logger_ptr->backend_sink_it_(msg);
        }
        return;
    }
    enqueue_log_(async_msg(async_msg_type::log, std::move(logger_ptr), msg), policy);
}

void thread_pool::post_log_nowait(logger* worker, const log_msg& msg) {
    post_log(worker, msg, async_overflow_policy::discard_new);
}

void thread_pool::post_log_nowait(std::shared_ptr<logger>&& logger_ptr, const log_msg& msg) {
    post_log(std::move(logger_ptr), msg, async_overflow_policy::discard_new);
}

void thread_pool::post_flush(logger* worker, bool wait) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        if (worker != nullptr) {
            worker->backend_flush_();
        }
        return;
    }

    async_msg flush_msg(async_msg_type::flush, worker);
    std::future<void> done;
    if (wait) {
        flush_msg.ack = std::make_shared<std::promise<void>>();
        done = flush_msg.ack->get_future();
    }

    if (queue_type_ == async_queue_type::lockfree) {
        static_cast<void>(
            enqueue_lockfree_(std::move(flush_msg), async_overflow_policy::block));
    } else {
        blocking_q_->enqueue(std::move(flush_msg), true);
    }
    force_wake_();

    if (wait) {
        done.wait();
        wait_for_pending_logs_();
        if (worker != nullptr) {
            worker->backend_flush_();
        }
    }
}

void thread_pool::post_flush(std::shared_ptr<logger>&& logger_ptr, bool wait) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        if (logger_ptr) {
            logger_ptr->backend_flush_();
        }
        return;
    }

    auto keep = logger_ptr;
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
        blocking_q_->enqueue(std::move(flush_msg), true);
    }
    force_wake_();

    if (wait) {
        done.wait();
        wait_for_pending_logs_();
        if (keep) {
            keep->backend_flush_();
        }
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
        idle_workers_.fetch_add(1, std::memory_order_relaxed);
        bool ok = blocking_q_->dequeue_for(incoming, wake_interval_);
        if (!ok && !shutting_down_.load(std::memory_order_acquire)) {
            ok = blocking_q_->dequeue_for(incoming, kLongIdle);
        }
        idle_workers_.fetch_sub(1, std::memory_order_relaxed);
        if (!ok) {
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

        idle_workers_.fetch_add(1, std::memory_order_relaxed);
        bool have = false;
        {
            std::unique_lock<std::mutex> lock(park_mu_);
            have = lockfree_q_->pop(incoming);
            if (!have) {
                const auto seq = park_seq_.load(std::memory_order_acquire);
                park_cv_.wait_for(lock, wake_interval_, [this, seq] {
                    return shutting_down_.load(std::memory_order_acquire) ||
                           park_seq_.load(std::memory_order_acquire) != seq;
                });
                have = lockfree_q_->pop(incoming);
            }
            if (!have && !shutting_down_.load(std::memory_order_acquire)) {
                const auto seq = park_seq_.load(std::memory_order_acquire);
                park_cv_.wait_for(lock, kLongIdle, [this, seq] {
                    return shutting_down_.load(std::memory_order_acquire) ||
                           park_seq_.load(std::memory_order_acquire) != seq;
                });
            }
        }
        idle_workers_.fetch_sub(1, std::memory_order_relaxed);

        if (!have) {
            have = lockfree_q_->pop(incoming);
        }
        if (have) {
            if (!process_msg_(incoming)) {
                break;
            }
        } else if (shutting_down_.load(std::memory_order_acquire) && lockfree_q_->empty()) {
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
            if (incoming.worker != nullptr) {
                incoming.worker->backend_sink_it_(incoming);
            }
            on_log_completed_();
            return true;
        case async_msg_type::flush:
            if (incoming.worker != nullptr) {
                incoming.worker->backend_flush_();
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
