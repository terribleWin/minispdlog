#pragma once
/*用户接口层
*/
#include "logger.h"
#include "registry.h"
#include "level.h"
#include "sinks/console_sink.h"
#include "sinks/color_console_sink.h"
#include "batch_config.h"
#include "sinks/file_sink.h"
#include "sinks/null_sink.h"
#include "sinks/buffered_file_sink.h"
#include "sinks/rotating_file_sink.h"
#include "sinks/daily_file_sink.h"
#include "sinks/json_sink.h"
#include "sinks/callback_sink.h"
#ifdef MINISPDLOG_WITH_QT
#include "sinks/qt_sink.h"
#endif
#include <fmt/format.h>
#include <memory>
#include <string>
namespace minispdlog{

    //reistry 相关接口

    //获取logger
    inline std::shared_ptr<logger> get(const std::string& name) {
    return registry::instance().get(name);
}
    //注册logger
    inline void register_logger(std::shared_ptr<logger> logger) {
    registry::instance().register_logger(std::move(logger));
}
    //删除logger
    inline void drop(const std::string& name) {
    registry::instance().drop(name);
}
    //删除所有logger
    inline void drop_all() {
    registry::instance().drop_all();
}
    //获取默认logger
    inline std::shared_ptr<logger> default_logger() {
        auto def = registry::instance().default_logger();
        if (!def) {
            //如果默认 logger 不存在,创建一个新的默认 logger
            auto console_sink = std::make_shared<sinks::color_console_sink_mt>();
            def = std::make_shared<logger>("", console_sink);
            def->set_level(level::info);
            registry::instance().set_default_logger(def);
        }
    return def;
}
    //设置默认logger
    inline void set_default_logger(std::shared_ptr<logger> new_default_logger) {
        registry::instance().set_default_logger(std::move(new_default_logger));
}
    //设置所有 logger 的级别
    inline void set_level(level log_level) {
        registry::instance().set_level(log_level);
    }
    //刷新所有 logger
    inline void flush_all() {
        registry::instance().flush_all();
    }

    // Drain async queues, join the global thread pool, drop registered loggers.
    // Call before main() returns if you used async logging.
    MINISPDLOG_API void shutdown();

    /// Write every buffered file's front buffer to disk (tests + crash handler).
    MINISPDLOG_API void dump_buffered_logs() noexcept;

    /// Dump buffered logs on SIGSEGV/SIGABRT (and Windows unhandled SEH). Idempotent.
    MINISPDLOG_API void install_crash_flush();

    //快速创建并注册 logger
    //创建一个多线程安全的控制台 logger
      inline std::shared_ptr<logger> stdout_color_mt(const std::string& logger_name) {
          auto sink = std::make_shared<sinks::color_console_sink_mt>();
          auto new_logger = std::make_shared<logger>(logger_name, sink);
          register_logger(new_logger);
        return new_logger;
    }
    //创建彩色控制台 logger
    inline std::shared_ptr<logger> stderr_color_mt(const std::string& logger_name) {
        auto sink = std::make_shared<sinks::color_stderr_sink_mt>();
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    //创建普通控制台
    inline std::shared_ptr<logger> stdout_mt(const std::string& logger_name) {
        auto sink = std::make_shared<sinks::console_sink_mt>();
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    //创建文件logger
    inline std::shared_ptr<logger> basic_logger_mt(
    const std::string& logger_name,
    const std::string& filename,
    bool truncate = false
) {
    auto sink = std::make_shared<sinks::file_sink_mt>(filename, truncate);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}
    //创建单线程文件 logger（无锁，性能更高）
    inline std::shared_ptr<logger> basic_logger_st(
    const std::string& logger_name,
    const std::string& filename,
    bool truncate = false
) {
    auto sink = std::make_shared<sinks::file_sink_st>(filename, truncate);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}
    inline std::shared_ptr<logger> buffered_logger_mt(
        const std::string& logger_name,
        const std::string& filename,
        bool truncate = false,
        batch_config cfg = {}) {
        auto sink = std::make_shared<sinks::buffered_file_sink_mt>(filename, truncate, cfg);
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> buffered_logger_st(
        const std::string& logger_name,
        const std::string& filename,
        bool truncate = false,
        batch_config cfg = {}) {
        auto sink = std::make_shared<sinks::buffered_file_sink_st>(filename, truncate, cfg);
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    //创建滚动文件 logger
    inline std::shared_ptr<logger> rotating_logger_mt(
    const std::string& logger_name,
    const std::string& filename,
    size_t max_size,
    size_t max_files
) {
    auto sink = std::make_shared<sinks::rotating_file_sink_mt>(filename, max_size, max_files);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}
    // 按天切分文件 logger（默认 00:00 翻日，max_files=0 表示不删旧文件）
    inline std::shared_ptr<logger> daily_logger_mt(
    const std::string& logger_name,
    const std::string& filename,
    int rotation_hour = 0,
    int rotation_minute = 0,
    bool truncate = false,
    size_t max_files = 0
) {
    auto sink = std::make_shared<sinks::daily_file_sink_mt>(
        filename, rotation_hour, rotation_minute, truncate, max_files);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}
    inline std::shared_ptr<logger> daily_logger_st(
    const std::string& logger_name,
    const std::string& filename,
    int rotation_hour = 0,
    int rotation_minute = 0,
    bool truncate = false,
    size_t max_files = 0
) {
    auto sink = std::make_shared<sinks::daily_file_sink_st>(
        filename, rotation_hour, rotation_minute, truncate, max_files);
    auto new_logger = std::make_shared<logger>(logger_name, sink);
    register_logger(new_logger);
    return new_logger;
}
    // JSON Lines 文件 logger（每行一个对象，给 ELK/Loki 用）
    inline std::shared_ptr<logger> json_logger_mt(
        const std::string& logger_name,
        const std::string& filename,
        bool truncate = false,
        batch_config cfg = {}) {
        auto sink = std::make_shared<sinks::json_file_sink_mt>(filename, truncate, cfg);
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> json_logger_st(
        const std::string& logger_name,
        const std::string& filename,
        bool truncate = false,
        batch_config cfg = {}) {
        auto sink = std::make_shared<sinks::json_file_sink_st>(filename, truncate, cfg);
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> stdout_json_mt(const std::string& logger_name) {
        auto sink = std::make_shared<sinks::json_console_sink_mt>();
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> stdout_json_st(const std::string& logger_name) {
        auto sink = std::make_shared<sinks::json_console_sink_st>();
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> stderr_json_mt(const std::string& logger_name) {
        auto sink = std::make_shared<sinks::json_stderr_sink_mt>();
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> stderr_json_st(const std::string& logger_name) {
        auto sink = std::make_shared<sinks::json_stderr_sink_st>();
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> rotating_json_logger_mt(
        const std::string& logger_name,
        const std::string& filename,
        std::size_t max_size,
        std::size_t max_files) {
        auto sink = std::make_shared<sinks::json_rotating_file_sink_mt>(filename, max_size, max_files);
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> rotating_json_logger_st(
        const std::string& logger_name,
        const std::string& filename,
        std::size_t max_size,
        std::size_t max_files) {
        auto sink = std::make_shared<sinks::json_rotating_file_sink_st>(filename, max_size, max_files);
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> daily_json_logger_mt(
        const std::string& logger_name,
        const std::string& filename,
        int rotation_hour = 0,
        int rotation_minute = 0,
        bool truncate = false,
        std::size_t max_files = 0) {
        auto sink = std::make_shared<sinks::json_daily_file_sink_mt>(
            filename, rotation_hour, rotation_minute, truncate, max_files);
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> daily_json_logger_st(
        const std::string& logger_name,
        const std::string& filename,
        int rotation_hour = 0,
        int rotation_minute = 0,
        bool truncate = false,
        std::size_t max_files = 0) {
        auto sink = std::make_shared<sinks::json_daily_file_sink_st>(
            filename, rotation_hour, rotation_minute, truncate, max_files);
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    // 回调 logger：不写新 Sink 类，把行交给业务（告警、计数、GUI 模拟）
    inline std::shared_ptr<logger> callback_logger_mt(
        const std::string& logger_name,
        sinks::callback_sink_mt::formatted_callback_t on_log,
        sinks::callback_sink_mt::flush_callback_t on_flush = {}) {
        auto sink = std::make_shared<sinks::callback_sink_mt>(std::move(on_log), std::move(on_flush));
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> callback_logger_mt(
        const std::string& logger_name,
        sinks::callback_sink_mt::record_callback_t on_log,
        sinks::callback_sink_mt::flush_callback_t on_flush = {}) {
        auto sink = std::make_shared<sinks::callback_sink_mt>(std::move(on_log), std::move(on_flush));
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> callback_logger_st(
        const std::string& logger_name,
        sinks::callback_sink_st::formatted_callback_t on_log,
        sinks::callback_sink_st::flush_callback_t on_flush = {}) {
        auto sink = std::make_shared<sinks::callback_sink_st>(std::move(on_log), std::move(on_flush));
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }
    inline std::shared_ptr<logger> callback_logger_st(
        const std::string& logger_name,
        sinks::callback_sink_st::record_callback_t on_log,
        sinks::callback_sink_st::flush_callback_t on_flush = {}) {
        auto sink = std::make_shared<sinks::callback_sink_st>(std::move(on_log), std::move(on_flush));
        auto new_logger = std::make_shared<logger>(logger_name, sink);
        register_logger(new_logger);
        return new_logger;
    }

    //全局日志接口（sourced_fmt 在调用点捕获源码位置，再交给 logger::log）
    template<typename... Args>
    inline void trace(details::sourced_fmt<std::type_identity_t<Args>...> fmt, Args&&... args) {
        default_logger()->log(level::trace, fmt.loc, fmt.value, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void debug(details::sourced_fmt<std::type_identity_t<Args>...> fmt, Args&&... args) {
        default_logger()->log(level::debug, fmt.loc, fmt.value, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void info(details::sourced_fmt<std::type_identity_t<Args>...> fmt, Args&&... args) {
        default_logger()->log(level::info, fmt.loc, fmt.value, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void warn(details::sourced_fmt<std::type_identity_t<Args>...> fmt, Args&&... args) {
        default_logger()->log(level::warn, fmt.loc, fmt.value, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void error(details::sourced_fmt<std::type_identity_t<Args>...> fmt, Args&&... args) {
        default_logger()->log(level::error, fmt.loc, fmt.value, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void critical(details::sourced_fmt<std::type_identity_t<Args>...> fmt, Args&&... args) {
        default_logger()->log(level::critical, fmt.loc, fmt.value, std::forward<Args>(args)...);
    }

// ========== 编译期日志宏（Release 下零开销，并在调用点填充源码位置） ==========
#define MINISPDLOG_LOGGER_CALL(logger, lvl, ...) \
    (logger)->log((lvl), MINISPDLOG_LOC, __VA_ARGS__)

#define MINISPDLOG_TRACE(...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_TRACE >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::trace, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_DEBUG(...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_DEBUG >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::debug, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_INFO(...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_INFO >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::info, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_WARN(...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_WARN >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::warn, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_ERROR(...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_ERROR >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::error, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_CRITICAL(...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_CRITICAL >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(minispdlog::default_logger(), minispdlog::level::critical, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_LOGGER_TRACE(logger, ...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_TRACE >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::trace, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_LOGGER_DEBUG(logger, ...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_DEBUG >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::debug, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_LOGGER_INFO(logger, ...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_INFO >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::info, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_LOGGER_WARN(logger, ...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_WARN >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::warn, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_LOGGER_ERROR(logger, ...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_ERROR >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::error, __VA_ARGS__); \
        } \
    } while(0)

#define MINISPDLOG_LOGGER_CRITICAL(logger, ...) \
    do { \
        if constexpr (MINISPDLOG_LEVEL_CRITICAL >= MINISPDLOG_ACTIVE_LEVEL) { \
            MINISPDLOG_LOGGER_CALL(logger, minispdlog::level::critical, __VA_ARGS__); \
        } \
    } while(0)

}
