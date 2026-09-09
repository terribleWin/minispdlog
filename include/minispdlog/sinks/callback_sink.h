#pragma once

#include "base_sink.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace minispdlog {
namespace sinks {

/**
 * Callback sink — custom output without writing a new sink class.
 *
 * Two log hooks (construct with either):
 *   formatted: void(const std::string& line)
 *   record:    void(const log_msg& msg, const std::string& line)
 * Optional flush hook: void().
 *
 * The formatted line is an owning string (safe to store). log_msg::payload /
 * logger_name are views valid only during the callback — copy them if you keep
 * the record.
 *
 * Invoked under the sink mutex. Do not log or flush the same sink from a
 * callback (deadlock on _mt). qt_sink is the GUI specialization; use this
 * sink for metrics, alerts, tests, or shipping without Qt.
 */
template <typename Mutex>
class callback_sink : public base_sink<Mutex> {
public:
    using formatted_callback_t = std::function<void(const std::string& formatted)>;
    using record_callback_t =
        std::function<void(const details::log_msg& msg, const std::string& formatted)>;
    using flush_callback_t = std::function<void()>;
    using callback_t = formatted_callback_t;

    explicit callback_sink(formatted_callback_t on_log, flush_callback_t on_flush = {})
        : callback_sink(wrap_formatted(std::move(on_log)), std::move(on_flush), 0) {}

    explicit callback_sink(record_callback_t on_log, flush_callback_t on_flush = {})
        : callback_sink(std::move(on_log), std::move(on_flush), 0) {}

    void set_flush_callback(flush_callback_t on_flush) {
        std::lock_guard<Mutex> lock(this->mutex_);
        flush_callback_ = std::move(on_flush);
    }

protected:
    void sink_it_(const details::log_msg& msg) override {
        auto& formatted = this->format_message(msg);
        callback_(msg, std::string(formatted.data(), formatted.size()));
    }

    void flush_() override {
        if (flush_callback_) {
            flush_callback_();
        }
    }

private:
    static record_callback_t wrap_formatted(formatted_callback_t on_log) {
        if (!on_log) {
            throw std::invalid_argument("callback_sink: callback must not be empty");
        }
        return [cb = std::move(on_log)](const details::log_msg&, const std::string& formatted) {
            cb(formatted);
        };
    }

    callback_sink(record_callback_t on_log, flush_callback_t on_flush, int)
        : callback_(std::move(on_log))
        , flush_callback_(std::move(on_flush)) {
        if (!callback_) {
            throw std::invalid_argument("callback_sink: callback must not be empty");
        }
    }

    record_callback_t callback_;
    flush_callback_t flush_callback_;
};

using callback_sink_mt = callback_sink<std::mutex>;
using callback_sink_st = callback_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog
