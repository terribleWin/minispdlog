#pragma once

/**
 * @file minispdlog.h
 * @brief 同步 logger、常用 Sink 工厂与全局日志函数的统一入口。
 *
 * 异步队列与异步工厂位于 `async.h`。本文件保留同步直写路径，不会隐式创建
 * 工作线程。
 */

#include "batch_config.h"
#include "level.h"
#include "logger.h"
#include "registry.h"
#include "sinks/buffered_file_sink.h"
#include "sinks/callback_sink.h"
#include "sinks/color_console_sink.h"
#include "sinks/console_sink.h"
#include "sinks/daily_file_sink.h"
#include "sinks/file_sink.h"
#include "sinks/json_sink.h"
#include "sinks/network_sink.h"
#include "sinks/null_sink.h"
#include "sinks/rotating_file_sink.h"
#ifdef MINISPDLOG_WITH_QT
#include "sinks/qt_sink.h"
#endif

#include <fmt/format.h>

#include <cstdint>
#include <memory>
#include <string>

namespace minispdlog {

/** @brief 按名称获取已注册 logger；不存在时返回空指针。 */
inline std::shared_ptr<logger> get(const std::string& name) {
    return registry::instance().get(name);
}

/** @brief 将 logger 注册到全局注册表。 */
inline void register_logger(std::shared_ptr<logger> logger) {
    registry::instance().register_logger(std::move(logger));
}

/** @brief 从全局注册表移除指定名称的 logger。 */
inline void drop(const std::string& name) {
    registry::instance().drop(name);
}

/** @brief 移除全部已注册 logger 与默认 logger。 */
inline void drop_all() {
    registry::instance().drop_all();
}

/**
 * @brief 获取默认 logger；缺失时惰性创建彩色标准输出 logger。
 * @note 正常读取使用原子共享指针快照，不经过注册表映射锁。
 */
inline std::shared_ptr<logger> default_logger() {
    auto def = registry::instance().default_logger();
    if (!def) {
        auto console_sink = std::make_shared<sinks::color_console_sink_mt>();
        def = std::make_shared<logger>("", console_sink);
        def->set_level(level::info);
        def = registry::instance().set_default_logger_if_empty(std::move(def));
    }
    return def;
}

/** @brief 原子替换默认 logger；允许传入空指针清除。 */
inline void set_default_logger(std::shared_ptr<logger> new_default_logger) {
    registry::instance().set_default_logger(std::move(new_default_logger));
}

/** @brief 设置默认 logger 与全部已注册 logger 的过滤级别。 */
inline void set_level(level log_level) {
    registry::instance().set_level(log_level);
}

/** @brief 刷新默认 logger 与全部已注册 logger。 */
inline void flush_all() {
    registry::instance().flush_all();
}

/**
 * @brief 排空异步队列、停止全局线程池并移除已注册 logger。
 * @note 使用异步日志时应在 `main()` 返回前调用。
 */
MINISPDLOG_API void shutdown();

/** @brief 尽力写出所有批量文件 Sink 的前台缓冲区。 */
MINISPDLOG_API void dump_buffered_logs() noexcept;

/** @brief 安装致命故障时的批量日志尽力写出处理器；可重复调用。 */
MINISPDLOG_API void install_crash_flush();

/** @brief 创建并注册多线程安全的彩色标准输出 logger。 */
inline std::shared_ptr<logger> stdout_color_mt(const std::string& logger_name) {
    auto sink = std::make_shared<sinks::color_console_sink_mt>();
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的彩色标准错误 logger。 */
inline std::shared_ptr<logger> stderr_color_mt(const std::string& logger_name) {
    auto sink = std::make_shared<sinks::color_stderr_sink_mt>();
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的普通标准输出 logger。 */
inline std::shared_ptr<logger> stdout_mt(const std::string& logger_name) {
    auto sink = std::make_shared<sinks::console_sink_mt>();
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册每条记录直接写入文件的多线程 logger。 */
inline std::shared_ptr<logger> basic_logger_mt(const std::string& logger_name, const std::string& filename,
                                               bool truncate = false) {
    auto sink = std::make_shared<sinks::file_sink_mt>(filename, truncate);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册由调用方保证串行访问的普通文件 logger。 */
inline std::shared_ptr<logger> basic_logger_st(const std::string& logger_name, const std::string& filename,
                                               bool truncate = false) {
    auto sink = std::make_shared<sinks::file_sink_st>(filename, truncate);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的批量耐久文件 logger。 */
inline std::shared_ptr<logger> buffered_logger_mt(const std::string& logger_name, const std::string& filename,
                                                  bool truncate = false, batch_config cfg = {}) {
    auto sink = std::make_shared<sinks::buffered_file_sink_mt>(filename, truncate, cfg);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程批量耐久文件 logger。 */
inline std::shared_ptr<logger> buffered_logger_st(const std::string& logger_name, const std::string& filename,
                                                  bool truncate = false, batch_config cfg = {}) {
    auto sink = std::make_shared<sinks::buffered_file_sink_st>(filename, truncate, cfg);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}
/** @brief 创建并注册多线程安全的按大小滚动文件 logger。 */
inline std::shared_ptr<logger> rotating_logger_mt(const std::string& logger_name, const std::string& filename,
                                                  std::size_t max_size, std::size_t max_files) {
    auto sink = std::make_shared<sinks::rotating_file_sink_mt>(filename, max_size, max_files);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/**
 * @brief 创建并注册多线程安全的按日期切分文件 logger。
 * @note 默认本地 00:00 切分，`max_files == 0` 表示保留全部日期文件。
 */
inline std::shared_ptr<logger> daily_logger_mt(const std::string& logger_name, const std::string& filename,
                                               int rotation_hour = 0, int rotation_minute = 0, bool truncate = false,
                                               std::size_t max_files = 0) {
    auto sink =
        std::make_shared<sinks::daily_file_sink_mt>(filename, rotation_hour, rotation_minute, truncate, max_files);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程按日期切分文件 logger。 */
inline std::shared_ptr<logger> daily_logger_st(const std::string& logger_name, const std::string& filename,
                                               int rotation_hour = 0, int rotation_minute = 0, bool truncate = false,
                                               std::size_t max_files = 0) {
    auto sink =
        std::make_shared<sinks::daily_file_sink_st>(filename, rotation_hour, rotation_minute, truncate, max_files);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的批量 JSON Lines 文件 logger。 */
inline std::shared_ptr<logger> json_logger_mt(const std::string& logger_name, const std::string& filename,
                                              bool truncate = false, batch_config cfg = {}, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_file_sink_mt>(filename, truncate, cfg);
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程批量 JSON Lines 文件 logger。 */
inline std::shared_ptr<logger> json_logger_st(const std::string& logger_name, const std::string& filename,
                                              bool truncate = false, batch_config cfg = {}, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_file_sink_st>(filename, truncate, cfg);
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的标准输出 JSON Lines logger。 */
inline std::shared_ptr<logger> stdout_json_mt(const std::string& logger_name, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_console_sink_mt>();
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程标准输出 JSON Lines logger。 */
inline std::shared_ptr<logger> stdout_json_st(const std::string& logger_name, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_console_sink_st>();
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的标准错误 JSON Lines logger。 */
inline std::shared_ptr<logger> stderr_json_mt(const std::string& logger_name, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_stderr_sink_mt>();
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程标准错误 JSON Lines logger。 */
inline std::shared_ptr<logger> stderr_json_st(const std::string& logger_name, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_stderr_sink_st>();
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的按大小滚动 JSON Lines logger。 */
inline std::shared_ptr<logger> rotating_json_logger_mt(const std::string& logger_name, const std::string& filename,
                                                       std::size_t max_size, std::size_t max_files,
                                                       json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_rotating_file_sink_mt>(filename, max_size, max_files);
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程按大小滚动 JSON Lines logger。 */
inline std::shared_ptr<logger> rotating_json_logger_st(const std::string& logger_name, const std::string& filename,
                                                       std::size_t max_size, std::size_t max_files,
                                                       json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_rotating_file_sink_st>(filename, max_size, max_files);
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的按日期切分 JSON Lines logger。 */
inline std::shared_ptr<logger> daily_json_logger_mt(const std::string& logger_name, const std::string& filename,
                                                    int rotation_hour = 0, int rotation_minute = 0,
                                                    bool truncate = false, std::size_t max_files = 0,
                                                    json_formatter fmt = {}) {
    auto sink =
        std::make_shared<sinks::json_daily_file_sink_mt>(filename, rotation_hour, rotation_minute, truncate, max_files);
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程按日期切分 JSON Lines logger。 */
inline std::shared_ptr<logger> daily_json_logger_st(const std::string& logger_name, const std::string& filename,
                                                    int rotation_hour = 0, int rotation_minute = 0,
                                                    bool truncate = false, std::size_t max_files = 0,
                                                    json_formatter fmt = {}) {
    auto sink =
        std::make_shared<sinks::json_daily_file_sink_st>(filename, rotation_hour, rotation_minute, truncate, max_files);
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}
/** @brief 创建并注册多线程安全的 UDP 文本 logger。 */
inline std::shared_ptr<logger> udp_logger_mt(const std::string& logger_name, const std::string& host,
                                             std::uint16_t port) {
    auto sink = std::make_shared<sinks::udp_sink_mt>(host, port);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程 UDP 文本 logger。 */
inline std::shared_ptr<logger> udp_logger_st(const std::string& logger_name, const std::string& host,
                                             std::uint16_t port) {
    auto sink = std::make_shared<sinks::udp_sink_st>(host, port);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的 TCP 文本 logger。 */
inline std::shared_ptr<logger> tcp_logger_mt(const std::string& logger_name, const std::string& host,
                                             std::uint16_t port) {
    auto sink = std::make_shared<sinks::tcp_sink_mt>(host, port);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程 TCP 文本 logger。 */
inline std::shared_ptr<logger> tcp_logger_st(const std::string& logger_name, const std::string& host,
                                             std::uint16_t port) {
    auto sink = std::make_shared<sinks::tcp_sink_st>(host, port);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 使用完整网络配置创建并注册多线程安全 logger。 */
inline std::shared_ptr<logger> network_logger_mt(const std::string& logger_name, network_config cfg) {
    auto sink = std::make_shared<sinks::network_sink_mt>(std::move(cfg));
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的 UDP JSON Lines logger。 */
inline std::shared_ptr<logger> json_udp_logger_mt(const std::string& logger_name, const std::string& host,
                                                  std::uint16_t port, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_udp_sink_mt>(host, port);
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程 UDP JSON Lines logger。 */
inline std::shared_ptr<logger> json_udp_logger_st(const std::string& logger_name, const std::string& host,
                                                  std::uint16_t port, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_udp_sink_st>(host, port);
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册多线程安全的 TCP JSON Lines logger。 */
inline std::shared_ptr<logger> json_tcp_logger_mt(const std::string& logger_name, const std::string& host,
                                                  std::uint16_t port, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_tcp_sink_mt>(host, port);
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 创建并注册单线程 TCP JSON Lines logger。 */
inline std::shared_ptr<logger> json_tcp_logger_st(const std::string& logger_name, const std::string& host,
                                                  std::uint16_t port, json_formatter fmt = {}) {
    auto sink = std::make_shared<sinks::json_tcp_sink_st>(host, port);
    sink->json() = std::move(fmt);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 使用格式化文本回调创建并注册多线程安全 logger。 */
inline std::shared_ptr<logger> callback_logger_mt(const std::string& logger_name,
                                                  sinks::callback_sink_mt::formatted_callback_t on_log,
                                                  sinks::callback_sink_mt::flush_callback_t on_flush = {}) {
    auto sink = std::make_shared<sinks::callback_sink_mt>(std::move(on_log), std::move(on_flush));
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 使用记录视图回调创建并注册多线程安全 logger。 */
inline std::shared_ptr<logger> callback_logger_mt(const std::string& logger_name,
                                                  sinks::callback_sink_mt::record_callback_t on_log,
                                                  sinks::callback_sink_mt::flush_callback_t on_flush = {}) {
    auto sink = std::make_shared<sinks::callback_sink_mt>(std::move(on_log), std::move(on_flush));
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 使用格式化文本回调创建并注册单线程 logger。 */
inline std::shared_ptr<logger> callback_logger_st(const std::string& logger_name,
                                                  sinks::callback_sink_st::formatted_callback_t on_log,
                                                  sinks::callback_sink_st::flush_callback_t on_flush = {}) {
    auto sink = std::make_shared<sinks::callback_sink_st>(std::move(on_log), std::move(on_flush));
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 使用记录视图回调创建并注册单线程 logger。 */
inline std::shared_ptr<logger> callback_logger_st(const std::string& logger_name,
                                                  sinks::callback_sink_st::record_callback_t on_log,
                                                  sinks::callback_sink_st::flush_callback_t on_flush = {}) {
    auto sink = std::make_shared<sinks::callback_sink_st>(std::move(on_log), std::move(on_flush));
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}

/** @brief 使用默认 logger 记录 trace 日志，并捕获调用点。 */
template <typename... Args>
inline void trace(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
    default_logger()->log(level::trace, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
}

/** @brief 使用默认 logger 记录 debug 日志，并捕获调用点。 */
template <typename... Args>
inline void debug(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
    default_logger()->log(level::debug, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
}

/** @brief 使用默认 logger 记录 info 日志，并捕获调用点。 */
template <typename... Args>
inline void info(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
    default_logger()->log(level::info, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
}

/** @brief 使用默认 logger 记录 warn 日志，并捕获调用点。 */
template <typename... Args>
inline void warn(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
    default_logger()->log(level::warn, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
}

/** @brief 使用默认 logger 记录 error 日志，并捕获调用点。 */
template <typename... Args>
inline void error(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
    default_logger()->log(level::error, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
}

/** @brief 使用默认 logger 记录 critical 日志，并捕获调用点。 */
template <typename... Args>
inline void critical(details::sourced_fmt<std::type_identity_t<Args>...> srcfmt, Args&&... args) {
    default_logger()->log(level::critical, srcfmt.loc, srcfmt.fmt, std::forward<Args>(args)...);
}

/** @name 编译期日志宏
 *  低于 `MINISPDLOG_ACTIVE_LEVEL` 的调用在编译期移除，并在调用点捕获源码位置。
 *  @{ */
#define MINISPDLOG_LOGGER_CALL(logger, lvl, ...) (logger)->log((lvl), MINISPDLOG_LOC, __VA_ARGS__)

#define MINISPDLOG_TRACE(...)                                                                                          \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_TRACE >= MINISPDLOG_ACTIVE_LEVEL) {                                             \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::trace, __VA_ARGS__);               \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_DEBUG(...)                                                                                          \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_DEBUG >= MINISPDLOG_ACTIVE_LEVEL) {                                             \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::debug, __VA_ARGS__);               \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_INFO(...)                                                                                           \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_INFO >= MINISPDLOG_ACTIVE_LEVEL) {                                              \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::info, __VA_ARGS__);                \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_WARN(...)                                                                                           \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_WARN >= MINISPDLOG_ACTIVE_LEVEL) {                                              \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::warn, __VA_ARGS__);                \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_ERROR(...)                                                                                          \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_ERROR >= MINISPDLOG_ACTIVE_LEVEL) {                                             \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::error, __VA_ARGS__);               \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_CRITICAL(...)                                                                                       \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_CRITICAL >= MINISPDLOG_ACTIVE_LEVEL) {                                          \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::critical, __VA_ARGS__);            \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_LOGGER_TRACE(logger, ...)                                                                           \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_TRACE >= MINISPDLOG_ACTIVE_LEVEL) {                                             \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::trace, __VA_ARGS__);                                     \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_LOGGER_DEBUG(logger, ...)                                                                           \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_DEBUG >= MINISPDLOG_ACTIVE_LEVEL) {                                             \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::debug, __VA_ARGS__);                                     \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_LOGGER_INFO(logger, ...)                                                                            \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_INFO >= MINISPDLOG_ACTIVE_LEVEL) {                                              \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::info, __VA_ARGS__);                                      \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_LOGGER_WARN(logger, ...)                                                                            \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_WARN >= MINISPDLOG_ACTIVE_LEVEL) {                                              \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::warn, __VA_ARGS__);                                      \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_LOGGER_ERROR(logger, ...)                                                                           \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_ERROR >= MINISPDLOG_ACTIVE_LEVEL) {                                             \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::error, __VA_ARGS__);                                     \
        }                                                                                                              \
    } while (0)

#define MINISPDLOG_LOGGER_CRITICAL(logger, ...)                                                                        \
    do {                                                                                                               \
        if constexpr (MINISPDLOG_LEVEL_CRITICAL >= MINISPDLOG_ACTIVE_LEVEL) {                                          \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::critical, __VA_ARGS__);                                  \
        }                                                                                                              \
    } while (0)

/** @} */

} // minispdlog
