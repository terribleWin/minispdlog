#pragma once

#include "base_sink.h"
#include <fstream>
#include <iostream>
#include <mutex>
#include <array>

namespace minispdlog {
    namespace sinks {
        namespace color {
    constexpr const char* reset   = "\033[0m";
    constexpr const char* bold    = "\033[1m";
    constexpr const char* white   = "\033[37m";
    constexpr const char* green   = "\033[32m";
    constexpr const char* yellow  = "\033[33m";
    constexpr const char* red     = "\033[31m";
    constexpr const char* magenta = "\033[35m";
    constexpr const char* cyan    = "\033[36m";
}
        
        template<typename ConsoleMutex>
        class color_console_sink : public base_sink<ConsoleMutex>{
        public:
            color_console_sink() {
        // 初始化颜色映射
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
        
        // 添加颜色前缀
            const std::string& prefix = colors_[static_cast<int>(msg.lvl)];
        
        // 输出: 颜色前缀 + 消息 + 颜色重置
            std::cout << prefix;
            std::cout.write(formatted.data(), formatted.size());
            std::cout << color::reset;
    }
            void flush_() override {
            std::cout << std::flush;
        }
        private:
            std::array<std::string, 7> colors_;
        };
        using color_console_sink_mt = color_console_sink<std::mutex>;
        using color_console_sink_st = color_console_sink<null_mutex>;
//stderr 版本
template<typename ConsoleMutex>
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
        
        const std::string& prefix = colors_[static_cast<int>(msg.lvl)];
        
        std::cerr << prefix;
        std::cerr.write(formatted.data(), formatted.size());
        std::cerr << color::reset;
    }
    
    void flush_() override {
        std::cerr << std::flush;
    }
    
private:
    std::array<std::string, 7> colors_;
};

using color_stderr_sink_mt = color_stderr_sink<std::mutex>;
using color_stderr_sink_st = color_stderr_sink<std::mutex>;
        
    }
}