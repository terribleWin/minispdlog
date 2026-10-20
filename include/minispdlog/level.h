#pragma once
#include "common.h"

#include <string>

namespace minispdlog {

/** @brief 日志严重级别；数值越大表示越严重。 */
enum class level {
    trace = 0,    ///< 最细粒度的执行轨迹。
    debug = 1,    ///< 面向开发调试的信息。
    info = 2,     ///< 正常运行状态。
    warn = 3,     ///< 可恢复但需要关注的异常状态。
    error = 4,    ///< 当前操作失败。
    critical = 5, ///< 可能导致服务不可用的严重错误。
    off = 6       ///< 关闭日志输出。
};

/** @brief 返回级别的完整小写名称。 */
MINISPDLOG_API const char* level_to_string(level lvl) noexcept;

/** @brief 返回级别的单字符缩写。 */
MINISPDLOG_API const char* level_to_short_string(level lvl) noexcept;

/** @brief 以无分配字符串视图返回级别名称。 */
MINISPDLOG_API std::string_view level_to_string_view(level lvl) noexcept;

/**
 * @brief 将不区分大小写的名称转换为日志级别。
 * @return 对应级别；未知名称回退为 `level::info`。
 */
MINISPDLOG_API level string_to_level(const std::string& str);

/** @brief 判断消息级别是否达到 logger 的过滤阈值。 */
inline bool should_log(level logger_level, level msg_level) noexcept {
    return msg_level >= logger_level;
}

} // minispdlog
