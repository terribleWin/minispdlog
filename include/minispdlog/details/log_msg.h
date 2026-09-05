#pragma once

#include "../common.h"
#include "../level.h"
#include "utils.h"

#include <cstddef>
#if __has_include(<source_location>) && __cplusplus >= 202002L
    #include <source_location>
    #define MINISPDLOG_HAS_SOURCE_LOCATION 1
#else
    #define MINISPDLOG_HAS_SOURCE_LOCATION 0
#endif

namespace minispdlog {
namespace details {

// spdlog-style source location. Convertible from std::source_location.
struct source_loc {
    constexpr source_loc() = default;
    constexpr source_loc(const char* filename_in, int line_in, const char* funcname_in) noexcept
        : filename(filename_in)
        , line(line_in)
        , funcname(funcname_in) {}

#if MINISPDLOG_HAS_SOURCE_LOCATION
    constexpr source_loc(const std::source_location& loc) noexcept
        : filename(loc.file_name())
        , line(static_cast<int>(loc.line()))
        , funcname(loc.function_name()) {}
#endif

    [[nodiscard]] constexpr bool empty() const noexcept { return line == 0; }

    const char* filename{nullptr};
    int line{0};
    const char* funcname{nullptr};
};

/**
 * @brief 日志消息结构体，包含一条日志的全部信息
 *
 * 作为日志系统内部的数据载体，在 logger → formatter → sink 之间传递。
 * 时间、线程号、进程号在构造时填充；源码位置由调用方传入。
 */
struct log_msg {
    log_msg() = default;

    log_msg(log_clock::time_point log_time,
            source_loc loc,
            string_view_t logger_name,
            minispdlog::level lvl,
            string_view_t msg)
        : logger_name(logger_name)
        , lvl(lvl)
        , time(log_time)
        , thread_id(get_thread_id())
        , process_id(get_pid())
        , source(loc)
        , payload(msg) {}

    log_msg(source_loc loc,
            string_view_t logger_name,
            minispdlog::level lvl,
            string_view_t msg)
        : log_msg(log_clock::now(), loc, logger_name, lvl, msg) {}

    log_msg(string_view_t logger_name,
            minispdlog::level lvl,
            string_view_t msg)
        : log_msg(source_loc(), logger_name, lvl, msg) {}

    log_msg(const log_msg&) = default;
    log_msg& operator=(const log_msg&) = default;
    log_msg(log_msg&&) = default;
    log_msg& operator=(log_msg&&) = default;

    string_view_t logger_name;
    minispdlog::level lvl{minispdlog::level::off};
    log_clock::time_point time;
    size_t thread_id{0};
    size_t process_id{0};
    source_loc source;
    string_view_t payload;

    // Byte offsets into the formatted line. pattern_formatter sets them from
    // %^ / %$; color sinks paint [start, end). Both 0 means "color the whole line".
    mutable size_t color_range_start{0};
    mutable size_t color_range_end{0};
};

}  // namespace details
}  // namespace minispdlog

#if MINISPDLOG_HAS_SOURCE_LOCATION
    // C++20 版本：使用 std::source_location，更安全
    #define MINISPDLOG_LOC \
        ::minispdlog::details::source_loc(std::source_location::current())
#else
    // 旧标准版本：使用宏
    #define MINISPDLOG_LOC \
        ::minispdlog::details::source_loc(__FILE__, __LINE__, __func__)
#endif