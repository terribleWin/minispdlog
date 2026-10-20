#pragma once

#include "../json_formatter.h"
#include "buffered_file_sink.h"
#include "console_sink.h"
#include "daily_file_sink.h"
#include "rotating_file_sink.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace minispdlog {
namespace sinks {

    /** @internal @brief 为基础 Sink 安装默认 JSON Lines 格式化器。 */
    template <typename Mutex>
    void set_json_formatter(base_sink<Mutex>& sink) {
        sink.base_sink<Mutex>::set_formatter(std::make_unique<json_formatter>());
    }

    /**
 * @brief 将任意文本 Sink 约束为 JSON Lines 输出的适配器。
 * @tparam BaseSink 被包装的具体 Sink。
 * @tparam Mutex 基础 Sink 使用的互斥类型。
 *
 * 默认拒绝 `set_pattern()` 和非 JSON 格式化器，避免采集文件混入普通文本。
 * 如确需自定义格式，可先调用 `allow_custom_formatting(true)` 明确解除约束。
 */
    template <typename BaseSink, typename Mutex>
    class json_sink_wrapper : public BaseSink {
    private:
        bool force_json_ = true;

        json_formatter* live_json_() { return dynamic_cast<json_formatter*>(this->formatter_.get()); }

        void keep_json_formatter_() {
            json_formatter proto;
            if (auto* cur = live_json_()) {
                proto = *cur;
            }
            this->base_sink<Mutex>::set_formatter(std::make_unique<json_formatter>(std::move(proto)));
        }

    public:
        /** @brief 构造基础 Sink，并立即安装 JSON 格式化器。 */
        template <typename... Args>
        explicit json_sink_wrapper(Args&&... args) : BaseSink(std::forward<Args>(args)...) {
            set_json_formatter(*this);
        }

        /**
     * @brief 返回当前 JSON 格式化器，以配置资源字段。
     * @warning 必须在开始并发记录日志前完成配置。
     * @throws std::logic_error 已允许并安装自定义格式化器时抛出。
     */
        json_formatter& json() {
            if (auto* fmt = live_json_()) {
                return *fmt;
            }
            if (!force_json_) {
                throw std::logic_error("json() requires a json_formatter (custom formatting is enabled)");
            }
            set_json_formatter(*this);
            return *live_json_();
        }

        /** @brief 尝试设置文本模式；强制 JSON 时只恢复 JSON 格式化器。 */
        void set_pattern(std::string pattern) override {
            if (force_json_) {
                keep_json_formatter_();
            } else {
                BaseSink::set_pattern(std::move(pattern));
            }
        }

        /** @brief 尝试安装格式化器；强制 JSON 时保留 JSON 格式化器及资源字段。 */
        void set_formatter(std::unique_ptr<formatter> fmt) override {
            if (force_json_) {
                keep_json_formatter_();
            } else {
                BaseSink::set_formatter(std::move(fmt));
            }
        }

        /**
     * @brief 允许或禁止调用方改为非 JSON 格式。
     * @param allow 为 true 时解除 JSON 约束；默认恢复强制 JSON。
     */
        void allow_custom_formatting(bool allow = true) { force_json_ = !allow; }
    };

    /** @brief 带批量恢复能力的 JSON 文件 Sink。 */
    template <typename Mutex>
    using json_file_sink = json_sink_wrapper<buffered_file_sink<Mutex>, Mutex>;

    /** @brief 标准输出 JSON Sink。 */
    template <typename Mutex>
    using json_console_sink = json_sink_wrapper<console_sink<Mutex>, Mutex>;

    /** @brief 标准错误 JSON Sink。 */
    template <typename Mutex>
    using json_stderr_sink = json_sink_wrapper<stderr_sink<Mutex>, Mutex>;

    /** @brief 按大小滚动的 JSON 文件 Sink。 */
    template <typename Mutex>
    using json_rotating_file_sink = json_sink_wrapper<rotating_file_sink<Mutex>, Mutex>;

    /** @brief 按日期切分的 JSON 文件 Sink。 */
    template <typename Mutex>
    using json_daily_file_sink = json_sink_wrapper<daily_file_sink<Mutex>, Mutex>;

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

} // sinks
} // minispdlog
