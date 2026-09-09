#pragma once

#include "base_sink.h"

#include <array>
#include <cstdio>
#include <mutex>
#include <ostream>
#include <string>

namespace minispdlog {
namespace sinks {
namespace color {
    constexpr const char* reset = "\033[0m";
    constexpr const char* bold = "\033[1m";
    constexpr const char* white = "\033[37m";
    constexpr const char* green = "\033[32m";
    constexpr const char* yellow = "\033[33m";
    constexpr const char* red = "\033[31m";
    constexpr const char* magenta = "\033[35m";
    constexpr const char* cyan = "\033[36m";
}

template <typename WriteBytes, typename WriteCstr>
inline void write_colored_range(const char* data, size_t size, size_t start, size_t end,
                                const std::string& color_code, WriteBytes&& write_bytes,
                                WriteCstr&& write_cstr) {
    if (start > size) {
        start = size;
    }
    if (end > size) {
        end = size;
    }

    auto write_span = [&](size_t from, size_t to) {
        if (to > from) {
            write_bytes(data + from, to - from);
        }
    };

    if (end > start) {
        write_span(0, start);
        write_cstr(color_code.c_str());
        write_span(start, end);
        write_cstr(color::reset);
        write_span(end, size);
    } else {
        write_cstr(color_code.c_str());
        write_span(0, size);
        write_cstr(color::reset);
    }
}

// Paint [color_range_start, color_range_end) when the formatter set a span;
// otherwise color the whole formatted line.
inline void write_colored(std::ostream& out, const details::log_msg& msg,
                          const fmt::memory_buffer& formatted, const std::string& color_code) {
    write_colored_range(
        formatted.data(), formatted.size(), msg.color_range_start, msg.color_range_end, color_code,
        [&](const char* ptr, size_t n) { out.write(ptr, static_cast<std::streamsize>(n)); },
        [&](const char* text) { out << text; });
}

inline void write_colored(std::FILE* out, const details::log_msg& msg,
                          const fmt::memory_buffer& formatted, const std::string& color_code) {
    write_colored_range(
        formatted.data(), formatted.size(), msg.color_range_start, msg.color_range_end, color_code,
        [&](const char* ptr, size_t n) { std::fwrite(ptr, 1, n, out); },
        [&](const char* text) { std::fputs(text, out); });
}

template <typename ConsoleMutex>
class color_console_sink : public base_sink<ConsoleMutex> {
public:
    color_console_sink() {
        colors_[static_cast<int>(level::trace)] = color::white;
        colors_[static_cast<int>(level::debug)] = color::cyan;
        colors_[static_cast<int>(level::info)] = color::green;
        colors_[static_cast<int>(level::warn)] = color::yellow;
        colors_[static_cast<int>(level::error)] = color::red;
        colors_[static_cast<int>(level::critical)] = std::string(color::bold) + color::red;
    }
    ~color_console_sink() override = default;

protected:
    void sink_it_(const details::log_msg& msg) override {
        auto& formatted = this->format_message(msg);
        write_colored(stdout, msg, formatted, colors_[static_cast<int>(msg.lvl)]);
    }
    void flush_() override { std::fflush(stdout); }

private:
    std::array<std::string, 7> colors_;
};
using color_console_sink_mt = color_console_sink<std::mutex>;
using color_console_sink_st = color_console_sink<null_mutex>;

template <typename ConsoleMutex>
class color_stderr_sink : public base_sink<ConsoleMutex> {
public:
    color_stderr_sink() {
        colors_[static_cast<int>(level::trace)] = color::white;
        colors_[static_cast<int>(level::debug)] = color::cyan;
        colors_[static_cast<int>(level::info)] = color::green;
        colors_[static_cast<int>(level::warn)] = color::yellow;
        colors_[static_cast<int>(level::error)] = color::red;
        colors_[static_cast<int>(level::critical)] = std::string(color::bold) + color::red;
    }
    ~color_stderr_sink() override = default;

protected:
    void sink_it_(const details::log_msg& msg) override {
        auto& formatted = this->format_message(msg);
        write_colored(stderr, msg, formatted, colors_[static_cast<int>(msg.lvl)]);
    }
    void flush_() override { std::fflush(stderr); }

private:
    std::array<std::string, 7> colors_;
};

using color_stderr_sink_mt = color_stderr_sink<std::mutex>;
using color_stderr_sink_st = color_stderr_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog
