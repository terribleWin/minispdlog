//异步API统一入口
#pragma once

#include <memory>
#include "sinks/file_sink.h"
#include "details/async_msg.h"
#include "details/circular_q.h"
#include "details/mpmc_blocking_q.h"
#include "details/thread_pool.h"

namespace minispdlog {

// 溢出策略
enum class async_overflow_policy {
    block,           // 阻塞等待，直到队列有空位（不丢消息）
    overrun_oldest   // 丢弃最旧的消息，新消息覆盖入队
};

// ========== 全局线程池管理器（单例） ==========

class thread_pool_manager {
public:
    static thread_pool_manager& instance() {
        static thread_pool_manager mgr;
        return mgr;
    }

    void init(size_t queue_size, size_t threads) {
        pool_ = std::make_unique<details::thread_pool>(queue_size, threads);
    }

    details::thread_pool* pool() { return pool_.get(); }

private:
    thread_pool_manager() = default;
    std::unique_ptr<details::thread_pool> pool_;
};

// 初始化全局线程池（程序启动时调用一次）
inline void init_thread_pool(size_t queue_size, size_t threads) {
    thread_pool_manager::instance().init(queue_size, threads);
}

// 获取全局线程池指针
inline details::thread_pool* get_thread_pool() {
    return thread_pool_manager::instance().pool();
}

// ========== async_logger：异步日志核心类 ==========
// 继承自 logger，重写 sink_it_ 将消息投递到全局线程池
// 后台线程从队列取出后，调用基类的 sink_it_ 完成实际写入

class async_logger : public logger {
public:
    async_logger(std::string name, std::vector<sinks::sink_ptr> sinks)
        : logger(std::move(name), std::move(sinks)) {}

protected:
    void sink_it_(const details::log_msg& msg) override {
        auto pool = get_thread_pool();
        if (!pool) return;
        pool->post_log(shared_from_this(), msg);
    }
};

// ========== 异步文件 Logger 工厂 ==========
// 创建异步文件 logger，所有消息通过全局线程池投递
inline std::shared_ptr<logger> async_file_mt(
    const std::string& logger_name,
    const std::string& filename,
    bool truncate,
    async_overflow_policy overflow_policy = async_overflow_policy::block
) {
    auto sink = std::make_shared<sinks::file_sink_mt>(filename, truncate);
    std::vector<sinks::sink_ptr> sinks = {std::move(sink)};
    auto new_logger = std::make_shared<async_logger>(logger_name, std::move(sinks));
    register_logger(new_logger);
    return new_logger;
}

} // namespace minispdlog