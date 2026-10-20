#pragma once

#include "async_config.h"
#include "batch_config.h"
#include "details/async_msg.h"
#include "details/thread_pool.h"
#include "lockfree_queue.h"
#include "registry.h"
#include "sinks/buffered_file_sink.h"
#include "sinks/callback_sink.h"
#include "sinks/file_sink.h"
#include "sinks/json_sink.h"
#include "sinks/network_sink.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace minispdlog {

using ::minispdlog::async_overflow_policy;
using ::minispdlog::async_queue_type;
using ::minispdlog::thread_pool_options;

/// 排空已注册日志器、停止全局线程池并释放注册表中的日志器。
MINISPDLOG_API void shutdown();

/**
 * @brief 管理进程级异步线程池的线程安全单例。
 *
 * 线程池通过 C++20 原子共享指针发布。日志热路径使用按线程缓存的共享快照：
 * 配置版本未变化时只读取轻量版本号，重配置后才刷新共享所有权。
 */
class thread_pool_manager {
public:
    /// 获取全局线程池管理器。
    static thread_pool_manager& instance() {
        static thread_pool_manager mgr;
        return mgr;
    }

    /**
     * @brief 使用基础参数替换全局线程池。
     * @param queue_size 队列精确容量。
     * @param threads 工作线程数量。
     * @param queue_type 队列后端类型。
     */
    void init(std::size_t queue_size, std::size_t threads, async_queue_type queue_type = async_queue_type::blocking) {
        thread_pool_options opts;
        opts.queue_size = queue_size;
        opts.thread_count = threads;
        opts.queue_type = queue_type;
        init(opts);
    }

    /**
     * @brief 使用完整选项替换全局线程池，并排空旧线程池。
     * @param opts 线程池初始化选项。
     */
    void init(const thread_pool_options& opts) {
        auto next = std::make_shared<details::thread_pool>(opts);
        std::shared_ptr<details::thread_pool> old;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            old = pool_.exchange(std::move(next), std::memory_order_acq_rel);
            generation_.fetch_add(1, std::memory_order_release);
        }
        if (old) {
            old->shutdown();
        }
    }

    /// 移除并排空当前全局线程池；重复调用安全。
    void shutdown() {
        std::shared_ptr<details::thread_pool> old;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            old = pool_.exchange({}, std::memory_order_acq_rel);
            generation_.fetch_add(1, std::memory_order_release);
        }
        if (old) {
            old->shutdown();
        }
    }

    /// 获取当前线程池的共享所有权；未初始化时返回空指针。
    [[nodiscard]] std::shared_ptr<details::thread_pool> get() const { return pool_.load(std::memory_order_acquire); }

    /**
     * @brief 获取当前线程池的非拥有型快照。
     * @return 未初始化时返回 nullptr。
     * @note 返回对象由当前线程的共享快照保持存活；配置版本不变时不会反复修改
     *       共享指针引用计数。并发重配置后旧线程池可能已停止，但指针不会悬空。
     */
    [[nodiscard]] details::thread_pool* pool() const {
        struct local_snapshot {
            std::uint64_t generation{0};
            std::shared_ptr<details::thread_pool> pool;
            bool initialized{false};
        };
        thread_local local_snapshot snapshot;

        const auto current_generation = generation_.load(std::memory_order_acquire);
        if (!snapshot.initialized || snapshot.generation != current_generation) {
            snapshot.pool = pool_.load(std::memory_order_acquire);
            snapshot.generation = current_generation;
            snapshot.initialized = true;
        }
        return snapshot.pool.get();
    }

    /// 获取线程池每次初始化或关闭时递增的版本号。
    [[nodiscard]] std::uint64_t generation() const { return generation_.load(std::memory_order_acquire); }

private:
    thread_pool_manager() = default;
    /// 串行化线程池替换操作。
    mutable std::mutex mutex_;
    /// 原子发布且拥有当前线程池。
    details::atomic_shared_ptr<details::thread_pool> pool_{};
    /// 线程池配置版本号。
    std::atomic<std::uint64_t> generation_{0};
};

/** @brief 初始化阻塞队列全局线程池。 */
inline void init_thread_pool(std::size_t queue_size, std::size_t threads) {
    thread_pool_manager::instance().init(queue_size, threads, async_queue_type::blocking);
}

/** @brief 使用完整选项初始化全局线程池。 */
inline void init_thread_pool(const thread_pool_options& opts) {
    thread_pool_manager::instance().init(opts);
}

/** @brief 初始化单消费者无锁队列全局线程池。 */
inline void init_lockfree_thread_pool(std::size_t queue_size) {
    thread_pool_options opts;
    opts.queue_size = queue_size;
    opts.thread_count = 1;
    opts.queue_type = async_queue_type::lockfree;
    thread_pool_manager::instance().init(opts);
}

/**
 * @brief 获取当前全局线程池的非拥有型快照。
 * @note 当前线程会保持返回对象存活到下次调用；需要跨调用长期使用时仍应通过管理器
 *       get() 显式持有共享所有权。
 */
inline details::thread_pool* get_thread_pool() {
    return thread_pool_manager::instance().pool();
}

/**
 * @brief 将日志记录投递到全局线程池的异步日志器。
 *
 * 每个生产者线程缓存线程池共享所有权，并在配置版本变化时更新；没有全局线程池
 * 或线程池正在关闭时，会退化为同步后端写入，从而避免丢失记录。
 */
class async_logger : public logger {
public:
    /**
     * @brief 创建异步日志器。
     * @param name 日志器名称。
     * @param sinks 输出端列表。
     * @param overflow_policy 队列满时的处理策略。
     */
    async_logger(std::string name, std::vector<sinks::sink_ptr> sinks,
                 async_overflow_policy overflow_policy = async_overflow_policy::block)
        : logger(std::move(name), std::move(sinks)), overflow_policy_(overflow_policy) {}

    /// 析构前同步刷新仍由当前全局线程池持有的消息。
    ~async_logger() override {
        try {
            auto pool = live_pool_();
            if (pool && pool->active()) {
                pool->post_flush(this, true);
            }
        } catch (...) {
        }
    }

    /// 获取队列满时的处理策略。
    [[nodiscard]] async_overflow_policy overflow_policy() const { return overflow_policy_; }

    /// 等待此前消息完成并刷新后端；没有线程池时直接同步刷新。
    void flush() override {
        auto pool = live_pool_();
        if (!pool) {
            backend_flush_();
            return;
        }
        pool->post_flush(this, true);
    }

protected:
    /// 将日志投递到线程池；没有线程池时直接同步写入。
    void sink_it_(const details::log_msg& msg) override {
        auto pool = live_pool_();
        if (!pool) {
            backend_sink_it_(msg);
            return;
        }
        pool->post_log(this, msg, overflow_policy_);
    }

private:
    /// 获取当前线程的线程池保活快照，稳定配置下避免共享引用计数争用。
    [[nodiscard]] details::thread_pool* live_pool_() const { return thread_pool_manager::instance().pool(); }

    /// 队列满时的处理策略。
    async_overflow_policy overflow_policy_;
};

/**
 * @brief 创建并注册异步普通文件日志器。
 * @param logger_name 注册名称。
 * @param filename 日志文件路径。
 * @param truncate 是否在打开时清空文件。
 * @param overflow_policy 队列满时的处理策略。
 * @return 已注册的日志器。
 */
inline std::shared_ptr<logger> async_file_mt(const std::string& logger_name, const std::string& filename, bool truncate,
                                             async_overflow_policy overflow_policy = async_overflow_policy::block) {
    auto sink = std::make_shared<sinks::file_sink_mt>(filename, truncate);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

/**
 * @brief 创建并注册带批量缓冲的异步文件日志器。
 * @param logger_name 注册名称。
 * @param filename 日志文件路径。
 * @param truncate 是否在打开时清空文件。
 * @param overflow_policy 队列满时的处理策略。
 * @param cfg 批量缓冲配置。
 * @return 已注册的日志器。
 */
inline std::shared_ptr<logger>
async_buffered_file_mt(const std::string& logger_name, const std::string& filename, bool truncate,
                       async_overflow_policy overflow_policy = async_overflow_policy::block, batch_config cfg = {}) {
    auto sink = std::make_shared<sinks::buffered_file_sink_mt>(filename, truncate, cfg);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

/**
 * @brief 创建并注册异步 JSON 文件日志器。
 * @param logger_name 注册名称。
 * @param filename 日志文件路径。
 * @param truncate 是否在打开时清空文件。
 * @param overflow_policy 队列满时的处理策略。
 * @param cfg 批量缓冲配置。
 * @param fmt JSON 字段与编码配置。
 * @return 已注册的日志器。
 */
inline std::shared_ptr<logger> async_json_file_mt(const std::string& logger_name, const std::string& filename,
                                                  bool truncate,
                                                  async_overflow_policy overflow_policy = async_overflow_policy::block,
                                                  batch_config cfg = {}, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_file_sink_mt>(filename, truncate, cfg);
    sink->json() = std::move(fmt);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

/**
 * @brief 创建并注册异步 JSON 滚动文件日志器。
 * @param logger_name 注册名称。
 * @param filename 日志文件路径。
 * @param max_size 单个文件的最大字节数。
 * @param max_files 保留的滚动文件数量。
 * @param overflow_policy 队列满时的处理策略。
 * @param fmt JSON 字段与编码配置。
 * @return 已注册的日志器。
 */
inline std::shared_ptr<logger>
async_json_rotating_mt(const std::string& logger_name, const std::string& filename, std::size_t max_size,
                       std::size_t max_files, async_overflow_policy overflow_policy = async_overflow_policy::block,
                       json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_rotating_file_sink_mt>(filename, max_size, max_files);
    sink->json() = std::move(fmt);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

/**
 * @brief 创建并注册异步 UDP 文本日志器。
 * @param logger_name 注册名称。
 * @param host 目标主机名或地址。
 * @param port 目标端口。
 * @param overflow_policy 队列满时的处理策略。
 * @return 已注册的日志器。
 */
inline std::shared_ptr<logger> async_udp_mt(const std::string& logger_name, const std::string& host, std::uint16_t port,
                                            async_overflow_policy overflow_policy = async_overflow_policy::block) {
    auto sink = std::make_shared<sinks::udp_sink_mt>(host, port);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

/**
 * @brief 创建并注册异步 TCP 文本日志器。
 * @param logger_name 注册名称。
 * @param host 目标主机名或地址。
 * @param port 目标端口。
 * @param overflow_policy 队列满时的处理策略。
 * @return 已注册的日志器。
 */
inline std::shared_ptr<logger> async_tcp_mt(const std::string& logger_name, const std::string& host, std::uint16_t port,
                                            async_overflow_policy overflow_policy = async_overflow_policy::block) {
    auto sink = std::make_shared<sinks::tcp_sink_mt>(host, port);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

/**
 * @brief 创建并注册异步 UDP JSON 日志器。
 * @param logger_name 注册名称。
 * @param host 目标主机名或地址。
 * @param port 目标端口。
 * @param overflow_policy 队列满时的处理策略。
 * @param fmt JSON 字段与编码配置。
 * @return 已注册的日志器。
 */
inline std::shared_ptr<logger> async_json_udp_mt(const std::string& logger_name, const std::string& host,
                                                 std::uint16_t port,
                                                 async_overflow_policy overflow_policy = async_overflow_policy::block,
                                                 json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_udp_sink_mt>(host, port);
    sink->json() = std::move(fmt);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

/**
 * @brief 创建并注册异步 TCP JSON 日志器。
 * @param logger_name 注册名称。
 * @param host 目标主机名或地址。
 * @param port 目标端口。
 * @param overflow_policy 队列满时的处理策略。
 * @param fmt JSON 字段与编码配置。
 * @return 已注册的日志器。
 */
inline std::shared_ptr<logger> async_json_tcp_mt(const std::string& logger_name, const std::string& host,
                                                 std::uint16_t port,
                                                 async_overflow_policy overflow_policy = async_overflow_policy::block,
                                                 json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_tcp_sink_mt>(host, port);
    sink->json() = std::move(fmt);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

/**
 * @brief 创建并注册接收格式化文本的异步回调日志器。
 * @param logger_name 注册名称。
 * @param on_log 每条格式化日志的回调。
 * @param on_flush 刷新回调。
 * @param overflow_policy 队列满时的处理策略。
 * @return 已注册的日志器。
 */
inline std::shared_ptr<logger> async_callback_mt(const std::string& logger_name,
                                                 sinks::callback_sink_mt::formatted_callback_t on_log,
                                                 sinks::callback_sink_mt::flush_callback_t on_flush = {},
                                                 async_overflow_policy overflow_policy = async_overflow_policy::block) {
    auto sink = std::make_shared<sinks::callback_sink_mt>(std::move(on_log), std::move(on_flush));
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

/**
 * @brief 创建并注册接收结构化记录的异步回调日志器。
 * @param logger_name 注册名称。
 * @param on_log 每条结构化日志记录的回调。
 * @param on_flush 刷新回调。
 * @param overflow_policy 队列满时的处理策略。
 * @return 已注册的日志器。
 */
inline std::shared_ptr<logger> async_callback_mt(const std::string& logger_name,
                                                 sinks::callback_sink_mt::record_callback_t on_log,
                                                 sinks::callback_sink_mt::flush_callback_t on_flush = {},
                                                 async_overflow_policy overflow_policy = async_overflow_policy::block) {
    auto sink = std::make_shared<sinks::callback_sink_mt>(std::move(on_log), std::move(on_flush));
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks), overflow_policy);
    registry::instance().register_logger(new_logger);
    return new_logger;
}

} // minispdlog
