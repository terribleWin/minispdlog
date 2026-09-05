#pragma once

#include "base_sink.h"
#include <array>
#include <iostream>
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

// Paint [color_range_start, color_range_end) when the formatter set a span;
// otherwise color the whole formatted line.
inline void write_colored(std::ostream& out, const details::log_msg& msg,
                          const fmt::memory_buffer& formatted, const std::string& color_code) {
    const char* data = formatted.data();
    const auto size = formatted.size();
    auto start = msg.color_range_start;
    auto end = msg.color_range_end;
    if (start > size) {
        start = size;
    }
    if (end > size) {
        end = size;
    }

    auto write_range = [&](size_t from, size_t to) {
        if (to > from) {
            out.write(data + from, static_cast<std::streamsize>(to - from));
        }
    };

    if (end > start) {
        write_range(0, start);
        out << color_code;
        write_range(start, end);
        out << color::reset;
        write_range(end, size);
    } else {
        out << color_code;
        write_range(0, size);
        out << color::reset;
    }
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
        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);
        write_colored(std::cout, msg, formatted, colors_[static_cast<int>(msg.lvl)]);
    }
    void flush_() override { std::cout << std::flush; }

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
        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);
        write_colored(std::cerr, msg, formatted, colors_[static_cast<int>(msg.lvl)]);
    }
    void flush_() override { std::cerr << std::flush; }

private:
    std::array<std::string, 7> colors_;
};

using color_stderr_sink_mt = color_stderr_sink<std::mutex>;
using color_stderr_sink_st = color_stderr_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog
