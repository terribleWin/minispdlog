#pragma once

#include "../json_formatter.h"
#include "buffered_file_sink.h"
#include "console_sink.h"
#include "daily_file_sink.h"
#include "rotating_file_sink.h"

#include <memory>
#include <string>
#include <utility>

namespace minispdlog {
namespace sinks {

template <typename Mutex>
void set_json_formatter(base_sink<Mutex>& sink) {
    sink.base_sink<Mutex>::set_formatter(std::make_unique<json_formatter>());
}

// 辅助宏或模板基类，减少重复代码
template <typename BaseSink, typename Mutex>
class json_sink_wrapper : public BaseSink {
private:
    bool force_json_ = true;

public:
    template <typename... Args>
    explicit json_sink_wrapper(Args&&... args) 
        : BaseSink(std::forward<Args>(args)...) {
        set_json_formatter(*this);
    }

    void set_pattern(std::string pattern) override {
        if (force_json_) {
            set_json_formatter(*this);
        } else {
            BaseSink::set_pattern(std::move(pattern));
        }
    }

    void set_formatter(std::unique_ptr<formatter> fmt) override {
        if (force_json_) {
            set_json_formatter(*this);
        } else {
            BaseSink::set_formatter(std::move(fmt));
        }
    }

    void allow_custom_formatting(bool allow = true) {
        force_json_ = !allow;
    }
};

// 使用 wrapper 简化定义
template <typename Mutex>
using json_file_sink = json_sink_wrapper<buffered_file_sink<Mutex>, Mutex>;

template <typename Mutex>
using json_console_sink = json_sink_wrapper<console_sink<Mutex>, Mutex>;

template <typename Mutex>
using json_stderr_sink = json_sink_wrapper<stderr_sink<Mutex>, Mutex>;

template <typename Mutex>
using json_rotating_file_sink = json_sink_wrapper<rotating_file_sink<Mutex>, Mutex>;

template <typename Mutex>
using json_daily_file_sink = json_sink_wrapper<daily_file_sink<Mutex>, Mutex>;

// 类型别名
using json_file_sink_mt = json_file_sink<std::mutex>;
using json_file_sink_st = json_file_sink<null_mutex>;

using json_console_sink_mt = json_console_sink<std::mutex>;
using json_console_sink_st = json_console_sink<null_mutex>;

using json_stderr_sink_mt = json_stderr_sink<std::mutex>;
using json_stderr_sink_st = json_stderr_sink<null_mutex>;

using json_rotating_file_sink_mt = json_rotating_file_sink<std::mutex>;
using json_rotating_file_sink_st = json_rotating_file_sink<null_mutex>;

using json_daily_file_sink_mt = json_daily_file_sink<std::mutex>;
using json_daily_file_sink_st = json_daily_file_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog