#pragma once

#include "base_sink.h"
#include <functional>
#include <string>

namespace minispdlog {
namespace sinks {

/**
 * @brief Qt 兼容 Sink（回调模式）
 *
 * 不直接依赖 Qt，通过用户提供的回调函数将日志消息投递到 GUI 线程。
 * 用户需在回调中使用 QMetaObject::invokeMethod(..., Qt::QueuedConnection)
 * 安全地更新控件。
 *
 * 使用示例：
 * @code
 * auto sink = std::make_shared<qt_sink>(
 *     [](const std::string& msg, level lvl) {
 *         // 用户自行确保线程安全
 *         std::cout << msg;
 *     }
 * );
 * @endcode
 */
class qt_sink : public base_sink<std::mutex> {
public:
    /// 回调签名：接收格式化后的日志字符串和级别
    using log_callback = std::function<void(const std::string& msg, level lvl)>;

    explicit qt_sink(log_callback cb)
        : callback_(std::move(cb)) {}

protected:
    void sink_it_(const details::log_msg& msg) override {
        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);
        if (callback_) {
            callback_(std::string(formatted.data(), formatted.size()), msg.lvl);
        }
    }

    void flush_() override {
        // Qt sink 不需要显式 flush，由 Qt 事件循环管理
    }

private:
    log_callback callback_;
};

} // namespace sinks
} // namespace minispdlog
