#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace minispdlog {

/** @brief 当前 minispdlog 的语义版本。 */
inline constexpr const char* VERSION = "0.1.0";

#ifdef _WIN32
#define MINISPDLOG_WINDOWS
#elif defined(__linux__)
#define MINISPDLOG_LINUX
#elif defined(__APPLE__)
#define MINISPDLOG_MACOS
#endif

#if defined(_WIN32) && defined(MINISPDLOG_SHARED)
#ifdef MINISPDLOG_BUILD
#define MINISPDLOG_API __declspec(dllexport)
#else
#define MINISPDLOG_API __declspec(dllimport)
#endif
#else
#define MINISPDLOG_API
#endif

/** @name 编译期日志级别
 *  数值越小表示日志越详细，必须与 `minispdlog::level` 保持一致。
 *  @{ */
#define MINISPDLOG_LEVEL_TRACE 0
#define MINISPDLOG_LEVEL_DEBUG 1
#define MINISPDLOG_LEVEL_INFO 2
#define MINISPDLOG_LEVEL_WARN 3
#define MINISPDLOG_LEVEL_ERROR 4
#define MINISPDLOG_LEVEL_CRITICAL 5
#define MINISPDLOG_LEVEL_OFF 6
/** @} */

/** @brief 未显式配置时保留全部日志调用。 */
#ifndef MINISPDLOG_ACTIVE_LEVEL
#define MINISPDLOG_ACTIVE_LEVEL MINISPDLOG_LEVEL_TRACE
#endif

/** @brief 公共只读字符串视图类型。 */
using string_view_t = std::string_view;

/** @brief 日志时间戳使用的系统时钟。 */
using log_clock = std::chrono::system_clock;

} // minispdlog
