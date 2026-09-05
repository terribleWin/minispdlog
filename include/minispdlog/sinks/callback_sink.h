#pragma once

#include "base_sink.h"
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace minispdlog {
namespace sinks {

/**
 * @brief 回调 Sink —— GUI / 自定义输出的可测试基石
 *
 * 将格式化后的日志交给用户提供的回调（例如投递到 UI 线程、写入内存缓冲）。
 * qt_sink 在真实 Qt 环境中用 QMetaObject::invokeMethod 完成同类工作；
 * 单测可用本 Sink 验证「格式化 + 投递」而不依赖 GUI。
 */
template<typename Mutex>
class callback_sink : public base_sink<Mutex> {
public:
    using callback_t = std::function<void(const std::string& formatted)>;

    explicit callback_sink(callback_t callback)
        : callback_(std::move(callback)) {
        if (!callback_) {
            throw std::invalid_argument("callback_sink: callback must not be empty");
        }
    }

protected:
    void sink_it_(const details::log_msg& msg) override {
        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);
        callback_(std::string(formatted.data(), formatted.size()));
    }

    void flush_() override {}

private:
    callback_t callback_;
};

using callback_sink_mt = callback_sink<std::mutex>;
using callback_sink_st = callback_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog
