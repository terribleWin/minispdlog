#pragma once

#include "base_sink.h"

#include <atomic>
#include <cstddef>
#include <mutex>

namespace minispdlog {
namespace sinks {

    /**
 * @brief 只执行格式化并统计字节数、不产生外部 I/O 的 Sink。
 *
 * 字节计数阻止编译器消除格式化工作，适合测量 logger 与 formatter 开销；
 * 它不代表控制台、磁盘或网络 Sink 的性能。
 */
    template <typename Mutex>
    class null_sink : public base_sink<Mutex> {
    public:
        /** @brief 返回累计格式化字节数。 */
        [[nodiscard]] std::size_t bytes_written() const noexcept { return bytes_.load(std::memory_order_relaxed); }

    protected:
        void sink_it_(const details::log_msg& msg) override {
            auto& formatted = this->format_message(msg);
            bytes_.fetch_add(formatted.size(), std::memory_order_relaxed);
        }

        void flush_() override {}

    private:
        std::atomic<std::size_t> bytes_{0};
    };

    using null_sink_mt = null_sink<std::mutex>;
    using null_sink_st = null_sink<null_mutex>;

} ///< 结束 sinks 命名空间。
} ///< 结束 minispdlog 命名空间。
