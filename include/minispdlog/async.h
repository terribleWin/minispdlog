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
#include "batch_config.h"
#include "sinks/buffered_file_sink.h"
#include "sinks/callback_sink.h"
#include "sinks/file_sink.h"
#include "sinks/json_sink.h"

#include <atomic>
#include <cstdint>
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
        thread_pool_options opts;
        opts.queue_size = queue_size;
        opts.thread_count = threads;
        opts.queue_type = queue_type;
        init(opts);
    }

    void init(const thread_pool_options& opts) {
        auto next = std::make_shared<details::thread_pool>(opts);
        std::shared_ptr<details::thread_pool> old;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            old = std::atomic_load_explicit(&pool_, std::memory_order_relaxed);
            std::atomic_store_explicit(&pool_, next, std::memory_order_release);
            raw_.store(next.get(), std::memory_order_release);
            generation_.fetch_add(1, std::memory_order_release);
        }
        if (old) {
            old->shutdown();
        }
    }

    void shutdown() {
        std::shared_ptr<details::thread_pool> old;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            old = std::atomic_load_explicit(&pool_, std::memory_order_relaxed);
            std::atomic_store_explicit(
                &pool_, std::shared_ptr<details::thread_pool>{}, std::memory_order_release);
            raw_.store(nullptr, std::memory_order_release);
            generation_.fetch_add(1, std::memory_order_release);
        }
        if (old) {
            old->shutdown();
        }
    }

    [[nodiscard]] std::shared_ptr<details::thread_pool> get() const {
        return std::atomic_load_explicit(&pool_, std::memory_order_acquire);
    }

    [[nodiscard]] details::thread_pool* pool() const {
        return raw_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t generation() const {
        return generation_.load(std::memory_order_acquire);
    }

private:
    thread_pool_manager() = default;
    mutable std::mutex mutex_;
    std::shared_ptr<details::thread_pool> pool_;
    std::atomic<details::thread_pool*> raw_{nullptr};
    std::atomic<std::uint64_t> generation_{0};
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

    ~async_logger() override {
        try {
            if (pool_hold_ && pool_hold_->active()) {
                pool_hold_->post_flush(this, true);
            }
        } catch (...) {
        }
    }

    [[nodiscard]] async_overflow_policy overflow_policy() const { return overflow_policy_; }

    void flush() override {
        auto* pool = live_pool_();
        if (pool == nullptr) {
            backend_flush_();
            return;
        }
        pool->post_flush(this, true);
    }

protected:
    void sink_it_(const details::log_msg& msg) override {
        auto* pool = live_pool_();
        if (pool == nullptr) {
            backend_sink_it_(msg);
            return;
        }
        pool->post_log(this, msg, overflow_policy_);
    }

private:
    details::thread_pool* live_pool_() {
        auto& mgr = thread_pool_manager::instance();
        const auto gen = mgr.generation();
        if (gen != pool_gen_) {
            pool_hold_ = mgr.get();
            pool_gen_ = gen;
        }
        return pool_hold_.get();
    }

    async_overflow_policy overflow_policy_;
    std::uint64_t pool_gen_{~std::uint64_t{0}};
    std::shared_ptr<details::thread_pool> pool_hold_;
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

inline std::shared_ptr<logger> async_buffered_file_mt(
    const std::string& logger_name,
    const std::string& filename,
    bool truncate,
    async_overflow_policy overflow_policy = async_overflow_policy::block,
    batch_config cfg = {}) {
    auto sink = std::make_shared<sinks::buffered_file_sink_mt>(filename, truncate, cfg);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger =
        std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

inline std::shared_ptr<logger> async_json_file_mt(
    const std::string& logger_name,
    const std::string& filename,
    bool truncate,
    async_overflow_policy overflow_policy = async_overflow_policy::block,
    batch_config cfg = {},
    json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_file_sink_mt>(filename, truncate, cfg);
    sink->json() = std::move(fmt);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger =
        std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

inline std::shared_ptr<logger> async_json_rotating_mt(
    const std::string& logger_name,
    const std::string& filename,
    std::size_t max_size,
    std::size_t max_files,
    async_overflow_policy overflow_policy = async_overflow_policy::block,
    json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_rotating_file_sink_mt>(filename, max_size, max_files);
    sink->json() = std::move(fmt);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger =
        std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

inline std::shared_ptr<logger> async_callback_mt(
    const std::string& logger_name,
    sinks::callback_sink_mt::formatted_callback_t on_log,
    sinks::callback_sink_mt::flush_callback_t on_flush = {},
    async_overflow_policy overflow_policy = async_overflow_policy::block) {
    auto sink = std::make_shared<sinks::callback_sink_mt>(std::move(on_log), std::move(on_flush));
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger =
        std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

inline std::shared_ptr<logger> async_callback_mt(
    const std::string& logger_name,
    sinks::callback_sink_mt::record_callback_t on_log,
    sinks::callback_sink_mt::flush_callback_t on_flush = {},
    async_overflow_policy overflow_policy = async_overflow_policy::block) {
    auto sink = std::make_shared<sinks::callback_sink_mt>(std::move(on_log), std::move(on_flush));
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger =
        std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

}  // namespace minispdlog
