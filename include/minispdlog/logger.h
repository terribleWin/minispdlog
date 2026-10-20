#pragma once
#include "common.h"
#include "details/log_msg.h"
#include "level.h"
#include "sinks/base_sink.h"
#include <atomic>
#include <cstddef>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace minispdlog {
namespace details {

/**
 * @brief 跨标准库版本的原子共享指针。
 *
 * 支持 C++20 原子共享指针特化的标准库直接使用 `std::atomic<std::shared_ptr<T>>`；
 * 旧版 libstdc++ 仅在缺少该特化时回退到共享指针原子自由函数，以保持 ABI 与
 * 并发生命周期语义一致。
 */
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    template <typename T>
    using atomic_shared_ptr = std::atomic<std::shared_ptr<T>>;
#else
    template <typename T>
    class atomic_shared_ptr {
    public:
        atomic_shared_ptr() noexcept = default;
        atomic_shared_ptr(std::nullptr_t) noexcept {}
        explicit atomic_shared_ptr(std::shared_ptr<T> desired) noexcept : value_(std::move(desired)) {}

        atomic_shared_ptr(const atomic_shared_ptr&) = delete;
        atomic_shared_ptr& operator=(const atomic_shared_ptr&) = delete;

        /** @brief 原子读取共享指针。 */
        [[nodiscard]] std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
            return std::atomic_load_explicit(&value_, order);
        }

        /** @brief 原子发布共享指针。 */
        void store(std::shared_ptr<T> desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
            std::atomic_store_explicit(&value_, std::move(desired), order);
        }

        /** @brief 原子替换共享指针并返回旧值。 */
        std::shared_ptr<T> exchange(std::shared_ptr<T> desired,
                                    std::memory_order order = std::memory_order_seq_cst) noexcept {
            return std::atomic_exchange_explicit(&value_, std::move(desired), order);
        }

        /** @brief 以强比较交换方式发布共享指针。 */
        bool compare_exchange_strong(std::shared_ptr<T>& expected, std::shared_ptr<T> desired,
                                     std::memory_order success, std::memory_order failure) noexcept {
            return std::atomic_compare_exchange_strong_explicit(&value_, &expected, std::move(desired), success,
                                                                failure);
        }

        /** @brief 以弱比较交换方式发布共享指针。 */
        bool compare_exchange_weak(std::shared_ptr<T>& expected, std::shared_ptr<T> desired, std::memory_order success,
                                   std::memory_order failure) noexcept {
            return std::atomic_compare_exchange_weak_explicit(&value_, &expected, std::move(desired), success, failure);
        }

        /** @brief 判断底层原子共享指针实现是否无锁。 */
        [[nodiscard]] bool is_lock_free() const noexcept { return std::atomic_is_lock_free(&value_); }

    private:
        /// 仅由共享指针原子自由函数访问的兼容存储。
        mutable std::shared_ptr<T> value_;
    };
#endif

    /**
 * @brief 保存编译期格式串及其调用点信息。
 *
 * 该类型通过一次用户定义转换支持 `logger.info("n={}", 7)` 形式的调用。
 * 构造函数直接接收字符串字面量，避免旧版 Clang 将运行期字符指针误用于
 * `fmt::format_string` 的编译期检查。
 */
    template <typename... Args>
    struct sourced_fmt {
        /// 经过编译期校验的格式串。
        fmt::format_string<Args...> fmt;
        /// 格式串所在的源码位置。
        source_loc loc;

        /**
     * @brief 从字符串字面量构造格式串并捕获调用点。
     * @tparam N 字符串字面量的数组长度。
     * @param s 字符串字面量。
     * @param file 调用点文件名。
     * @param line 调用点行号。
     * @param func 调用点函数名。
     */
        template <std::size_t N>
        consteval sourced_fmt(const char (&s)[N], const char* file = __builtin_FILE(), int line = __builtin_LINE(),
                              const char* func = __builtin_FUNCTION())
            : fmt(s), loc(file, line, func) {}
    };

    /**
 * @brief 获取当前线程复用的格式化缓冲区。
 * @return 仅供当前线程使用的缓冲区引用。
 * @note MinGW 的线程退出阶段可能早于非平凡 TLS 析构，调用端在该平台改用栈缓冲。
 */
    inline fmt::memory_buffer& thread_payload_buf() {
        thread_local fmt::memory_buffer buf;
        return buf;
    }

} ///< 结束 details 命名空间。

/**
 * @brief 同步日志器基类。
 *
 * 日志器采用写时复制维护输出端列表，日志热路径只读取不可变快照；异步日志器
 * 通过覆写 sink_it_ 将记录投递到线程池，同时复用本类的后端输出逻辑。
 */
class MINISPDLOG_API logger : public std::enable_shared_from_this<logger> {
public:
    /// 输出端列表类型。
    using sink_list = std::vector<sinks::sink_ptr>;

    /** @brief 创建不包含输出端的日志器。 @param name 日志器名称。 */
    explicit logger(std::string name);
    /**
     * @brief 创建包含单个输出端的日志器。
     * @param name 日志器名称。
     * @param single_sink 输出端；空指针会被忽略。
     */
    logger(std::string name, sinks::sink_ptr single_sink);
    /**
     * @brief 创建包含多个输出端的日志器。
     * @param name 日志器名称。
     * @param sinks 初始输出端列表。
     */
    logger(std::string name, sink_list sinks);
    virtual ~logger() = default;

    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;

    /// 按 trace 级别写入日志。
    template <typename... Args>
    void trace(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
        log(level::trace, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
    }

    /// 按 debug 级别写入日志。
    template <typename... Args>
    void debug(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
        log(level::debug, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
    }

    /// 按 info 级别写入日志。
    template <typename... Args>
    void info(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
        log(level::info, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
    }

    /// 按 warn 级别写入日志。
    template <typename... Args>
    void warn(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
        log(level::warn, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
    }

    /// 按 error 级别写入日志。
    template <typename... Args>
    void error(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
        log(level::error, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
    }

    /// 按 critical 级别写入日志。
    template <typename... Args>
    void critical(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
        log(level::critical, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief 格式化并写入一条带源码位置的日志。
     * @param lvl 日志级别。
     * @param loc 调用点位置。
     * @param format 编译期校验的格式串。
     * @param args 格式化参数。
     */
    template <typename... Args>
    void log(level lvl, details::source_loc loc, fmt::format_string<Args...> format, Args&&... args) {
        if (!should_log(lvl)) {
            return;
        }
#if defined(__MINGW32__)
        /// 避免 MinGW 在线程退出时销毁 fmt TLS 缓冲区产生的运行库堆损坏。
        fmt::memory_buffer local_buf;
        auto& buf = local_buf;
#else
        auto& buf = details::thread_payload_buf();
#endif
        buf.clear();
        ::fmt::format_to(::fmt::appender(buf), format, std::forward<Args>(args)...);
        details::log_msg log_message(loc, name_, lvl, string_view_t(buf.data(), buf.size()));
        sink_it_(log_message);
    }

    /**
     * @brief 使用已捕获调用点的格式串写入日志。
     * @param lvl 日志级别。
     * @param srcfmt 格式串及调用点。
     * @param args 格式化参数。
     */
    template <typename... Args>
    void log(level lvl, details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
        log(lvl, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
    }

    /** @brief 以写时复制方式添加输出端。 @param sink 待添加的输出端。 */
    void add_sink(sinks::sink_ptr sink);
    /** @brief 以写时复制方式移除输出端。 @param sink 待移除的输出端。 */
    void remove_sink(sinks::sink_ptr sink);
    /** @brief 获取当前输出端快照。 @return 与内部容器相互独立的列表副本。 */
    [[nodiscard]] sink_list sinks() const;

    /** @brief 设置最低日志级别。 @param log_level 最低可写入级别。 */
    void set_level(level log_level);
    /** @brief 获取最低日志级别。 */
    [[nodiscard]] level get_level() const;
    /** @brief 判断指定级别是否允许写入。 */
    [[nodiscard]] bool should_log(level msg_level) const;
    /**
     * @brief 为当前所有输出端设置格式模板。
     * @param pattern 格式模板；每个输出端独立编译并持有格式器。
     */
    void set_pattern(std::string pattern);
    /**
     * @brief 为当前所有输出端设置自定义格式器。
     * @param new_formatter 格式器；除最后一个输出端外均使用克隆副本。
     */
    void set_formatter(std::unique_ptr<formatter> new_formatter);
    /// 刷新全部输出端；异步日志器会等待已排队记录完成。
    virtual void flush();
    /** @brief 设置触发自动刷新的最低级别。 */
    void flush_on(level log_level);
    /** @brief 获取日志器名称。 */
    [[nodiscard]] const std::string& name() const;

    /** @brief 同步写入入口；异步日志器覆写后将记录入队。 */
    virtual void sink_it_(const details::log_msg& msg);
    /** @brief 后端直接写入输出端，保证不会再次进入异步队列。 */
    void backend_sink_it_(const details::log_msg& msg);
    /** @brief 后端直接刷新输出端，保证不会再次进入异步队列。 */
    void backend_flush_();

protected:
    /// 日志器名称；构造后保持稳定，异步消息可安全引用。
    std::string name_;

private:
    /// 原子读取不可变输出端快照。
    [[nodiscard]] std::shared_ptr<const sink_list> load_sinks_() const;
    /// 原子发布新的输出端快照。
    void store_sinks_(std::shared_ptr<sink_list> next);
    /// 刷新给定快照中的全部有效输出端。
    void flush_sinks_(const sink_list& sinks) const;

    /// 串行化写时复制，锁不会覆盖任何输出端 I/O。
    mutable std::mutex sinks_mutex_;
    /// C++20 原子共享指针；读线程持有快照即可保证容器生命周期。
    details::atomic_shared_ptr<sink_list> sinks_;

    /// 最低可写入级别。
    std::atomic<level> level_{level::trace};
    /// 自动刷新级别。
    std::atomic<level> flush_level_{level::off};
};
} ///< 结束 minispdlog 命名空间。
