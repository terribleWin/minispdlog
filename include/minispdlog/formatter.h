#pragma once

#include "common.h"
#include "details/log_msg.h"

#include <fmt/format.h>

#include <memory>

namespace minispdlog {

/**
 * @brief 日志格式化器抽象接口。
 *
 * 每个 Sink 独占一个格式化器实例；`clone()` 用于为多个 Sink 复制独立状态。
 * 实现可以复用内部缓存，但不得保存 `log_msg` 中字符串视图的地址。
 */
class formatter {
public:
    virtual ~formatter() = default;

    /**
     * @brief 将一条日志追加到目标缓冲区。
     * @param msg 仅在本次调用期间有效的日志记录。
     * @param dest 接收格式化字节的缓冲区；函数不会自动清空它。
     */
    virtual void format(const details::log_msg& msg, fmt::memory_buffer& dest) = 0;

    /** @brief 克隆一个具有相同配置、但状态彼此独立的格式化器。 */
    virtual std::unique_ptr<formatter> clone() const = 0;
};

} // minispdlog
