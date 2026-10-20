#pragma once
#include "../common.h"

#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace minispdlog {
namespace details {

    /** @brief 按 `strftime` 风格格式化指定时间点。 */
    MINISPDLOG_API std::string format_time(const log_clock::time_point& tp, const char* format = "%Y-%m-%d %H:%M:%S");

    /** @brief 返回当前线程的进程内稳定标识，并在线程局部缓存结果。 */
    MINISPDLOG_API std::size_t get_thread_id();

    /** @brief 返回当前进程标识；POSIX `fork()` 后会正确反映子进程。 */
    MINISPDLOG_API std::size_t get_pid();

    /** @brief 返回当前主机名；查询失败时返回 `unknown`。 */
    MINISPDLOG_API const std::string& get_hostname();

    /** @brief 返回当前 Unix 时间戳，单位毫秒。 */
    MINISPDLOG_API std::int64_t get_timestamp_ms();

    /** @brief 原地移除字符串左侧空白并返回同一对象。 */
    MINISPDLOG_API std::string& ltrim(std::string& s);

    /** @brief 原地移除字符串右侧空白并返回同一对象。 */
    MINISPDLOG_API std::string& rtrim(std::string& s);

    /** @brief 原地移除字符串两侧空白并返回同一对象。 */
    MINISPDLOG_API std::string& trim(std::string& s);

} // details
} // minispdlog
