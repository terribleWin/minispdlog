#pragma once

#include "base_sink.h"

#include <atomic>
#include <cstddef>
#include <mutex>

namespace minispdlog {
namespace sinks {

// Formats every accepted record into a buffer and counts bytes so the work
// cannot be optimized out. No console or disk I/O — use this to measure the
// formatter, not the sink write path.
template <typename Mutex>
class null_sink : public base_sink<Mutex> {
public:
    [[nodiscard]] std::size_t bytes_written() const noexcept {
        return bytes_.load(std::memory_order_relaxed);
    }

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

} // namespace sinks
} // namespace minispdlog
