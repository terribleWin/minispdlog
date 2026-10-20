#pragma once

#include "../common.h"
#include "../level.h"
#include "utils.h"

#include <cstddef>
#if __has_include(<source_location>)
#include <source_location>
#endif
#if defined(__cpp_lib_source_location) && __cpp_lib_source_location >= 201907L
#define MINISPDLOG_HAS_SOURCE_LOCATION 1
#else
#define MINISPDLOG_HAS_SOURCE_LOCATION 0
#endif

namespace minispdlog {
namespace details {

    /**
 * @brief 与日志记录一起传递的轻量源码位置。
 * @note 字符串均为非拥有视图，调用方必须保证其生命周期覆盖日志处理过程。
 */
    struct source_loc {
        constexpr source_loc() = default;
        constexpr source_loc(const char* filename_in, int line_in, const char* funcname_in) noexcept
            : filename(filename_in), line(line_in), funcname(funcname_in) {}

#if MINISPDLOG_HAS_SOURCE_LOCATION
        constexpr source_loc(const std::source_location& loc) noexcept
            : filename(loc.file_name()), line(static_cast<int>(loc.line())), funcname(loc.function_name()) {}
#endif

        /** @brief 判断当前对象是否未携带源码位置。 */
        [[nodiscard]] constexpr bool empty() const noexcept { return line == 0; }

        const char* filename{nullptr}; ///< 源文件路径，非拥有指针。
        int line{0};                   ///< 从 1 开始的源代码行号；零表示无位置。
        const char* funcname{nullptr}; ///< 函数名，非拥有指针。
    };

    /**
 * @brief 在 logger、formatter 与 Sink 之间传递的日志记录视图。
 *
 * 时间、线程号、进程号在构造时填充；源码位置由调用方传入。
 * `logger_name` 与 `payload` 不拥有内存，同步路径只在调用栈内使用；异步路径会
 * 先复制到 `log_msg_buffer`，因此调用者无需延长格式化参数的生命周期。
 */
    struct log_msg {
        log_msg() = default;

        log_msg(log_clock::time_point log_time, source_loc loc, string_view_t logger_name, minispdlog::level lvl,
                string_view_t msg)
            : logger_name(logger_name), lvl(lvl), time(log_time), thread_id(get_thread_id()), process_id(get_pid()),
              source(loc), payload(msg) {}

        log_msg(source_loc loc, string_view_t logger_name, minispdlog::level lvl, string_view_t msg)
            : log_msg(log_clock::now(), loc, logger_name, lvl, msg) {}

        log_msg(string_view_t logger_name, minispdlog::level lvl, string_view_t msg)
            : log_msg(source_loc(), logger_name, lvl, msg) {}

        log_msg(const log_msg&) = default;
        log_msg& operator=(const log_msg&) = default;
        log_msg(log_msg&&) = default;
        log_msg& operator=(log_msg&&) = default;

        string_view_t logger_name;                     ///< logger 名称视图。
        minispdlog::level lvl{minispdlog::level::off}; ///< 严重级别。
        log_clock::time_point time;                    ///< 生产线程记录时间。
        std::size_t thread_id{0};                      ///< 生产线程标识。
        std::size_t process_id{0};                     ///< 生产进程标识。
        source_loc source;                             ///< 调用点源码位置。
        string_view_t payload;                         ///< 已完成 fmt 插值的正文视图。

        /** @brief `%^` 标记的着色区间起始字节偏移。 */
        mutable std::size_t color_range_start{0};
        /** @brief `%$` 标记的着色区间结束字节偏移；两端均为零表示整行着色。 */
        mutable std::size_t color_range_end{0};
    };

} // details
} // minispdlog

#if MINISPDLOG_HAS_SOURCE_LOCATION
/** @brief 在 C++20 环境中捕获当前调用点。 */
#define MINISPDLOG_LOC ::minispdlog::details::source_loc(std::source_location::current())
#else
/** @brief 在缺少 `std::source_location` 时通过预定义宏捕获调用点。 */
#define MINISPDLOG_LOC ::minispdlog::details::source_loc(__FILE__, __LINE__, __func__)
#endif
