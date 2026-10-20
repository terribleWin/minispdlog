#pragma once

#include "base_sink.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace minispdlog {
namespace sinks {

    /**
 * @brief 通过业务回调消费日志，无需编写新的 Sink 子类。
 *
 * 可选择只接收拥有内存的格式化字符串，或同时接收 `log_msg` 记录视图；还可提供
 * 可选的刷新回调。格式化字符串可以安全保存，而 `log_msg::payload` 与
 * `logger_name` 只在回调执行期间有效，需要跨调用保存时必须复制。
 *
 * @warning 回调在 Sink 锁内执行；回调中不得再次记录或刷新同一 Sink，否则 `_mt`
 * 版本会死锁。GUI 场景优先使用 `qt_sink`。
 */
    template <typename Mutex>
    class callback_sink : public base_sink<Mutex> {
    public:
        /** @brief 只接收格式化文本的回调类型。 */
        using formatted_callback_t = std::function<void(const std::string& formatted)>;
        /** @brief 同时接收记录视图与格式化文本的回调类型。 */
        using record_callback_t = std::function<void(const details::log_msg& msg, const std::string& formatted)>;
        /** @brief 刷新事件回调类型。 */
        using flush_callback_t = std::function<void()>;
        /** @brief 向后兼容的格式化回调别名。 */
        using callback_t = formatted_callback_t;

        /** @brief 使用格式化文本回调构造 Sink。 */
        explicit callback_sink(formatted_callback_t on_log, flush_callback_t on_flush = {})
            : callback_sink(wrap_formatted(std::move(on_log)), std::move(on_flush), 0) {}

        /** @brief 使用记录回调构造 Sink。 */
        explicit callback_sink(record_callback_t on_log, flush_callback_t on_flush = {})
            : callback_sink(std::move(on_log), std::move(on_flush), 0) {}

        /** @brief 在线程安全保护下替换刷新回调。 */
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
            return [cb = std::move(on_log)](const details::log_msg&, const std::string& formatted) { cb(formatted); };
        }

        callback_sink(record_callback_t on_log, flush_callback_t on_flush, int)
            : callback_(std::move(on_log)), flush_callback_(std::move(on_flush)) {
            if (!callback_) {
                throw std::invalid_argument("callback_sink: callback must not be empty");
            }
        }

        record_callback_t callback_;
        flush_callback_t flush_callback_;
    };

    using callback_sink_mt = callback_sink<std::mutex>;
    using callback_sink_st = callback_sink<null_mutex>;

} // sinks
} // minispdlog
