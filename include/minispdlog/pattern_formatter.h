#pragma once
#include "common.h"
#include "details/datetime.h"
#include "formatter.h"
#include "level.h"

#include <cstdint>
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
 *
 * Flags are compiled into a switch table (no per-flag virtual calls).
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

        private:
            enum class piece_kind : std::uint8_t {
                literal,
                year,
                month,
                day,
                hour,
                minute,
                second,
                millis,
                micros,
                level_short,
                level_full,
                name,
                payload,
                tid,
                pid,
                src_file,
                src_path,
                src_line,
                src_func,
                src_loc,
                color_start,
                color_stop
            };

            struct piece {
                piece_kind kind{piece_kind::literal};
                std::uint16_t lit{0};
            };

            void compile_pattern();
            void push_literal_(std::string& raw);
            void format_default_(const details::log_msg& msg, fmt::memory_buffer& dest);

            std::string pattern_;
            std::vector<piece> pieces_;
            std::vector<std::string> literals_;
            details::wall_clock_cache clock_{};
            bool needs_calendar_{false};
            bool default_layout_{false};
    };
}
