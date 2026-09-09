#pragma once

#include "base_sink.h"

#include <cstdio>
#include <mutex>

namespace minispdlog {
namespace sinks {

template <typename ConsoleMutex>
class console_sink : public base_sink<ConsoleMutex> {
public:
    console_sink() = default;
    ~console_sink() override = default;

protected:
    void sink_it_(const details::log_msg& msg) override {
        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);
        std::fwrite(formatted.data(), 1, formatted.size(), stdout);
    }
    void flush_() override {
        std::fflush(stdout);
    }
};
using console_sink_mt = console_sink<std::mutex>;
using console_sink_st = console_sink<null_mutex>;

template <typename ConsoleMutex>
class stderr_sink : public base_sink<ConsoleMutex> {
public:
    stderr_sink() = default;
    ~stderr_sink() override = default;

protected:
    void sink_it_(const details::log_msg& msg) override {
        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);
        std::fwrite(formatted.data(), 1, formatted.size(), stderr);
    }
    void flush_() override {
        std::fflush(stderr);
    }
};
using stderr_sink_mt = stderr_sink<std::mutex>;
using stderr_sink_st = stderr_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog
