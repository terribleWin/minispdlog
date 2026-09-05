#pragma once
#include <string>
#include <string_view>
#include <memory>
#include <cstdint>
#include <chrono>
namespace minispdlog{
constexpr const char* VERSION ="0.1.0";
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

// ========== 编译期日志级别控制 ==========
// 数值越小越详细，与 level 枚举保持同步
#define MINISPDLOG_LEVEL_TRACE    0
#define MINISPDLOG_LEVEL_DEBUG    1
#define MINISPDLOG_LEVEL_INFO     2
#define MINISPDLOG_LEVEL_WARN     3
#define MINISPDLOG_LEVEL_ERROR    4
#define MINISPDLOG_LEVEL_CRITICAL 5
#define MINISPDLOG_LEVEL_OFF      6

// 默认：trace 及以上全量输出（等价于 Debug 构建）
#ifndef MINISPDLOG_ACTIVE_LEVEL
    #define MINISPDLOG_ACTIVE_LEVEL MINISPDLOG_LEVEL_TRACE
#endif

using string_view_t = std::string_view;
using log_clock = std::chrono::system_clock;
}
