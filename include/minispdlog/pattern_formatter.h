#pragma once
#include "common.h"
#include "level.h"
#include "formatter.h"
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <ctime>

namespace minispdlog {
/**
 * @brief 基于 pattern 字符串的日志格式化器
 * 
 * 继承自 formatter 抽象接口，将 pattern 字符串编译为 flag_formatter 序列，
 * 格式化时逐个调用，支持时间缓存优化。
 * 
 * @see formatter, flag_formatter
 */
    class pattern_formatter : public formatter {
        public:
            explicit pattern_formatter(std::string pattern = "[%Y-%m-%d %H:%M:%S] [%l] %v");
            ~pattern_formatter() override = default;
            void format(const details::log_msg& msg, fmt::memory_buffer& dest) override;
            std::unique_ptr<formatter> clone() const override;
            void set_pattern(const std::string& pattern);
        
        public:
            //抽象基类 处理单个占位符
            class flag_formatter {
                public:
                    virtual ~flag_formatter() = default;
                    virtual void format(const details::log_msg& msg,
                                       fmt::memory_buffer& dest) = 0;
                    virtual std::unique_ptr<flag_formatter> clone() const = 0;
            };
        
        private:
            void compile_pattern();
            std::tm get_time(const details::log_msg& msg);
            std::string pattern_;
            std::vector<std::unique_ptr<flag_formatter>> formatters_;
            std::chrono::seconds last_log_secs_{0};
            std::tm cached_tm_{};
    };
}