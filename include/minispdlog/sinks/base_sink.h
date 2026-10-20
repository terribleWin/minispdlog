#pragma once

#include "../common.h"
#include "../details/log_msg.h"
#include "../formatter.h"
#include "../pattern_formatter.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace minispdlog {
namespace sinks {

    /**
 * @brief 所有日志输出目标的统一接口。
 *
 * logger 先按 Sink 级别过滤，再调用 `log()`。实现必须明确自己的线程安全策略；
 * 常用模板实现 `base_sink` 通过互斥类型统一提供串行化。
 */
    class sink {
    public:
        virtual ~sink() = default;

        /** @brief 消费一条日志记录。 */
        virtual void log(const details::log_msg& msg) = 0;
        /** @brief 刷新该输出目标。 */
        virtual void flush() = 0;
        /** @brief 设置该 Sink 的过滤阈值。 */
        virtual void set_level(level log_level) = 0;
        /** @brief 返回当前过滤阈值。 */
        virtual level get_level() const = 0;
        /** @brief 判断消息级别是否达到该 Sink 的阈值。 */
        virtual bool should_log(level msg_level) const = 0;
        /** @brief 编译模式串并安装新的 `pattern_formatter`。 */
        virtual void set_pattern(std::string pattern) = 0;
        /** @brief 安装任意格式化器；传入空指针时保持原配置。 */
        virtual void set_formatter(std::unique_ptr<formatter> sink_formatter) = 0;
    };

    /** @brief Sink 的共享所有权类型。 */
    using sink_ptr = std::shared_ptr<sink>;

    /**
 * @brief 为具体 Sink 提供锁、级别过滤与格式化器管理的模板基类。
 * @tparam Mutex `std::mutex` 表示多线程安全，`null_mutex` 表示调用方保证串行。
 */
    template <typename Mutex>
    class base_sink : public sink {
    public:
        base_sink() : formatter_(std::make_unique<pattern_formatter>()) {}
        base_sink(const base_sink&) = delete;
        base_sink& operator=(const base_sink&) = delete;

        void log(const details::log_msg& msg) override {
            std::lock_guard<Mutex> lock(mutex_);
            sink_it_(msg);
        }

        level get_level() const override { return level_.load(std::memory_order_relaxed); }

        void set_level(level log_level) override { level_.store(log_level, std::memory_order_relaxed); }

        bool should_log(level msg_level) const override { return msg_level >= get_level(); }

        void flush() override {
            std::lock_guard<Mutex> lock(mutex_);
            flush_();
        }

        void set_pattern(std::string pattern) override {
            auto compiled = std::make_unique<pattern_formatter>(std::move(pattern));
            std::lock_guard<Mutex> lock(mutex_);
            formatter_ = std::move(compiled);
        }

        void set_formatter(std::unique_ptr<formatter> sink_formatter) override {
            if (!sink_formatter) {
                return;
            }
            std::lock_guard<Mutex> lock(mutex_);
            formatter_ = std::move(sink_formatter);
        }

    protected:
        /** @brief 在已持有 Sink 锁时输出记录。 */
        virtual void sink_it_(const details::log_msg& msg) = 0;
        /** @brief 在已持有 Sink 锁时刷新输出。 */
        virtual void flush_() = 0;

        /** @brief 将记录追加到调用方提供的缓冲区。 */
        void format_message(const details::log_msg& msg, fmt::memory_buffer& dest) { formatter_->format(msg, dest); }

        /**
     * @brief 使用 Sink 私有缓冲区格式化记录并返回引用。
     * @warning 返回内容只在该 Sink 下一次格式化前有效；需要长期保存时必须复制。
     */
        fmt::memory_buffer& format_message(const details::log_msg& msg) {
            format_buf_.clear();
            formatter_->format(msg, format_buf_);
            return format_buf_;
        }

        mutable Mutex mutex_;
        std::atomic<level> level_{level::trace};
        std::unique_ptr<formatter> formatter_;
        fmt::memory_buffer format_buf_{};
    };

    /** @brief 单线程 Sink 使用的零开销互斥类型。 */
    struct null_mutex {
        void lock() {}
        void unlock() {}
    };

} ///< 结束 sinks 命名空间。
} ///< 结束 minispdlog 命名空间。
