// 异步 API 统一入口
#pragma once

// C++20 deprecates free atomic_load/store for shared_ptr; keep them for
// libstdc++ 11 (Ubuntu 22.04) which lacks std::atomic<std::shared_ptr<T>>.
#define _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING

#include "async_config.h"
#include "details/async_msg.h"
#include "details/thread_pool.h"
#include "lockfree_queue.h"
#include "registry.h"
#include "sinks/file_sink.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace minispdlog {

using ::minispdlog::async_queue_type;
using ::minispdlog::async_overflow_policy;
using ::minispdlog::thread_pool_options;

// Drain loggers, join the global pool, then drop registered loggers.
MINISPDLOG_API void shutdown();

// ========== 全局线程池管理器（单例） ==========

class thread_pool_manager {
public:
    static thread_pool_manager& instance() {
        static thread_pool_manager mgr;
        return mgr;
    }

    void init(std::size_t queue_size, std::size_t threads,
              async_queue_type queue_type = async_queue_type::blocking) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto old = std::atomic_load_explicit(&pool_, std::memory_order_relaxed);
        std::atomic_store_explicit(
            &pool_, std::shared_ptr<details::thread_pool>{}, std::memory_order_release);
        old.reset();
        auto next = std::make_shared<details::thread_pool>(queue_size, threads, queue_type);
        std::atomic_store_explicit(&pool_, std::move(next), std::memory_order_release);
    }

    void init(const thread_pool_options& opts) {
        init(opts.queue_size, opts.thread_count, opts.queue_type);
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto old = std::atomic_load_explicit(&pool_, std::memory_order_relaxed);
        std::atomic_store_explicit(
            &pool_, std::shared_ptr<details::thread_pool>{}, std::memory_order_release);
        old.reset();
    }

    [[nodiscard]] std::shared_ptr<details::thread_pool> get() const {
        return std::atomic_load_explicit(&pool_, std::memory_order_acquire);
    }

    details::thread_pool* pool() { return get().get(); }

private:
    thread_pool_manager() = default;
    mutable std::mutex mutex_;
    std::shared_ptr<details::thread_pool> pool_;
};

inline void init_thread_pool(std::size_t queue_size, std::size_t threads) {
    thread_pool_manager::instance().init(queue_size, threads, async_queue_type::blocking);
}

inline void init_thread_pool(const thread_pool_options& opts) {
    thread_pool_manager::instance().init(opts);
}

inline void init_lockfree_thread_pool(std::size_t queue_size) {
    thread_pool_options opts;
    opts.queue_size = queue_size;
    opts.thread_count = 1;
    opts.queue_type = async_queue_type::lockfree;
    thread_pool_manager::instance().init(opts);
}

inline details::thread_pool* get_thread_pool() {
    return thread_pool_manager::instance().pool();
}

// ========== async_logger ==========

class async_logger : public logger {
public:
    async_logger(std::string name, std::vector<sinks::sink_ptr> sinks,
                 async_overflow_policy overflow_policy = async_overflow_policy::block)
        : logger(std::move(name), std::move(sinks))
        , overflow_policy_(overflow_policy) {}

    [[nodiscard]] async_overflow_policy overflow_policy() const { return overflow_policy_; }

    void flush() override {
        auto pool = thread_pool_manager::instance().get();
        if (!pool) {
            backend_flush_();
            return;
        }
        pool->post_flush(shared_from_this(), true);
    }

protected:
    void sink_it_(const details::log_msg& msg) override {
        auto pool = thread_pool_manager::instance().get();
        if (!pool) {
            backend_sink_it_(msg);
            return;
        }
        pool->post_log(shared_from_this(), msg, overflow_policy_);
    }

private:
    async_overflow_policy overflow_policy_;
};

inline std::shared_ptr<logger> async_file_mt(
    const std::string& logger_name,
    const std::string& filename,
    bool truncate,
    async_overflow_policy overflow_policy = async_overflow_policy::block) {
    auto sink = std::make_shared<sinks::file_sink_mt>(filename, truncate);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger =
        std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

}  // namespace minispdlog
