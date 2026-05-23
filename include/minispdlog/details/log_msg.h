#pragma once

#include "../common.h"
#include "../level.h"
#include "utils.h"
#include <string>
#include <cstddef>

namespace minispdlog {
namespace details {

// 源代码位置信息(用于调试)
#if defined(__cpp_lib_source_location) && __cpp_lib_source_location >= 201907L
    #include <source_location>
    using source_loc = std::source_location;
#else
struct source_loc {
    constexpr source_loc() = default;
    constexpr source_loc(const char* filename, int line, const char* funcname)
        : filename(filename), line(line), funcname(funcname) {}
    
    constexpr bool empty() const noexcept { return line == 0; }
    
    const char* filename{nullptr};
    int line{0};
    const char* funcname{nullptr};
};
#endif

// 日志消息结构体
// 这是日志系统的核心数据结构,包含了一条日志的所有信息
struct log_msg {
    log_msg() = default;
    
    // 构造函数:创建日志消息
    log_msg(log_clock::time_point log_time, 
            source_loc loc,
            string_view_t logger_name,
            minispdlog::level lvl,
            string_view_t msg)
        : logger_name(logger_name)
        , lvl(lvl)
        , time(log_time)
        , thread_id(get_thread_id())
        , source(loc)
        , payload(msg)
    {}
    
    // 简化构造函数(自动获取当前时间)
    log_msg(source_loc loc,
            string_view_t logger_name,
            minispdlog::level lvl,
            string_view_t msg)
        : log_msg(log_clock::now(), loc, logger_name, lvl, msg)
    {}
    
    // 最简化构造(无源码位置信息)
    log_msg(string_view_t logger_name,
            minispdlog::level lvl,
            string_view_t msg)
        : log_msg(source_loc(), logger_name, lvl, msg)
    {}
    
    log_msg(const log_msg&) = default;
    log_msg& operator=(const log_msg&) = default;
    
    // 核心字段
    string_view_t logger_name;           // Logger 名称
    //level level{level::off};              // 日志级别
    minispdlog::level lvl{minispdlog::level::off};  // 使用完整路径
    log_clock::time_point time;           // 时间戳(直接使用标准库类型)
    size_t thread_id{0};                  // 线程 ID
    source_loc source;                    // 源码位置
    string_view_t payload;                // 实际日志内容
    
    // 颜色范围(用于格式化时着色,由 formatter 设置)
    mutable size_t color_range_start{0};
    mutable size_t color_range_end{0};
};

} // namespace details
} // namespace minispdlog