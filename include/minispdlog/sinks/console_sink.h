#pragma once

#include "base_sink.h"

#include <cstdio>
#include <mutex>

namespace minispdlog {
namespace sinks {

    /** @brief 将格式化日志写入标准输出。 */
    template <typename ConsoleMutex>
    class console_sink : public base_sink<ConsoleMutex> {
    public:
        console_sink() = default;
        ~console_sink() override = default;

    protected:
        void sink_it_(const details::log_msg& msg) override {
            auto& formatted = this->format_message(msg);
            std::fwrite(formatted.data(), 1, formatted.size(), stdout);
        }
        void flush_() override { std::fflush(stdout); }
    };
    using console_sink_mt = console_sink<std::mutex>;
    using console_sink_st = console_sink<null_mutex>;

    /** @brief 将格式化日志写入标准错误。 */
    template <typename ConsoleMutex>
    class stderr_sink : public base_sink<ConsoleMutex> {
    public:
        stderr_sink() = default;
        ~stderr_sink() override = default;

    protected:
        void sink_it_(const details::log_msg& msg) override {
            auto& formatted = this->format_message(msg);
            std::fwrite(formatted.data(), 1, formatted.size(), stderr);
        }
        void flush_() override { std::fflush(stderr); }
    };
    using stderr_sink_mt = stderr_sink<std::mutex>;
    using stderr_sink_st = stderr_sink<null_mutex>;

} ///< 结束 sinks 命名空间。
} ///< 结束 minispdlog 命名空间。
