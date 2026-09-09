#pragma once
#include "common.h"
#include "details/datetime.h"
#include "formatter.h"
#include "level.h"

#include <memory>
#include <string>
#include <vector>

namespace minispdlog {
/**
 * Pattern string formatter.
 *
 * Separator convention (same scene, same delimiter):
 *   date   %Y-%m-%d
 *   time   %H:%M:%S.%e   (use %f instead of %e when you need microseconds)
 *   source %s:%#  or %@
 *   fields [..] [..] separated by a space
 *   color  %^ ... %$  (marks log_msg::color_range_* for color sinks)
 */
    class pattern_formatter : public formatter {
        public:
            static constexpr const char* default_pattern =
                "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] %v";

            explicit pattern_formatter(std::string pattern = default_pattern);
            ~pattern_formatter() override = default;
            void format(const details::log_msg& msg, fmt::memory_buffer& dest) override;
            std::unique_ptr<formatter> clone() const override;
            void set_pattern(std::string pattern);
        
        public:
            //抽象基类 处理单个占位符
            class flag_formatter {
                public:
                    virtual ~flag_formatter() = default;
                    virtual void format(const details::log_msg& msg,
                                        const details::wall_clock_cache& clock,
                                       fmt::memory_buffer& dest) = 0;
                    virtual std::unique_ptr<flag_formatter> clone() const = 0;
            };
        
        private:
            void compile_pattern();
            std::string pattern_;
            std::vector<std::unique_ptr<flag_formatter>> formatters_;
            details::wall_clock_cache clock_{};
            bool needs_calendar_{false};
    };
}
