#pragma once
#include "common.h"
#include "level.h"
#include "sinks/base_sink.h"
#include "details/log_msg.h"
#include <fmt/format.h>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace minispdlog {
namespace details {

// Converts a format string at the call site and captures file/line/function.
//
// This is a single user-defined conversion from a string literal, which is
// required for `lg.info("n={}", 7)`. Wrapping fmt::format_string would need a
// second conversion (rejected by MSVC/Clang). A consteval ctor from const char*
// is also not a constant expression on Clang 14 (Ubuntu 22.04 CI). Macros still
// pass fmt::format_string into log() and keep compile-time format checks.
struct sourced_fmt {
    const char* text{nullptr};
    source_loc loc;

    template <std::size_t N>
    sourced_fmt(const char (&s)[N],
                const char* file = __builtin_FILE(),
                int line = __builtin_LINE(),
                const char* func = __builtin_FUNCTION()) noexcept
        : text(s)
        , loc(file, line, func) {}

    sourced_fmt(const char* s,
                const char* file = __builtin_FILE(),
                int line = __builtin_LINE(),
                const char* func = __builtin_FUNCTION()) noexcept
        : text(s)
        , loc(file, line, func) {}
};

inline fmt::memory_buffer& thread_payload_buf() {
    thread_local fmt::memory_buffer buf;
    return buf;
}

}  // namespace details

    class MINISPDLOG_API logger : public std::enable_shared_from_this<logger> {
        public:
            using sink_list = std::vector<sinks::sink_ptr>;

            explicit logger(std::string name);
            logger(std::string name, sinks::sink_ptr single_sink);
            logger(std::string name, sink_list sinks);
            virtual ~logger() = default;

            logger(const logger&) = delete;
            logger& operator=(const logger&) = delete;

            template<typename... Args>
            void trace(details::sourced_fmt srcfmt, Args&&... args) {
                log(level::trace, srcfmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void debug(details::sourced_fmt srcfmt, Args&&... args) {
                log(level::debug, srcfmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void info(details::sourced_fmt srcfmt, Args&&... args) {
                log(level::info, srcfmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void warn(details::sourced_fmt srcfmt, Args&&... args) {
                log(level::warn, srcfmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void error(details::sourced_fmt srcfmt, Args&&... args) {
                log(level::error, srcfmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void critical(details::sourced_fmt srcfmt, Args&&... args) {
                log(level::critical, srcfmt, std::forward<Args>(args)...);
            }
            // Single write path: fills time, thread, pid, and the captured source_loc.
            template<typename... Args>
            void log(level lvl, details::source_loc loc, fmt::format_string<Args...> format,
                     Args&&... args) {
                if (!this->should_log(lvl)) return;
                auto& buf = details::thread_payload_buf();
                buf.clear();
                ::fmt::format_to(::fmt::appender(buf), format, std::forward<Args>(args)...);
                details::log_msg log_message(
                    loc,
                    name_,
                    lvl,
                    string_view_t(buf.data(), buf.size())
                );
                sink_it_(log_message);
            }
            template<typename... Args>
            void log(level lvl, details::sourced_fmt srcfmt, Args&&... args) {
                log(lvl, srcfmt.loc, ::fmt::runtime(srcfmt.text), std::forward<Args>(args)...);
            }

            // Concurrent-safe sink list mutation (copy-on-write).
            void add_sink(sinks::sink_ptr sink);
            void remove_sink(sinks::sink_ptr sink);
            // Snapshot copy of shared_ptrs; never a live reference to the internal vector.
            [[nodiscard]] sink_list sinks() const;

            void set_level(level log_level);
            [[nodiscard]] level get_level() const;
            [[nodiscard]] bool should_log(level msg_level) const;
            // Compile a pattern string onto every current sink (each sink owns one
            // pattern_formatter). Does not clone a formatter object.
            void set_pattern(std::string pattern);
            // Install a custom formatter on every current sink (cloned per sink).
            void set_formatter(std::unique_ptr<formatter> new_formatter);
            // Public API: async_logger waits until queued records are written.
            virtual void flush();
            void flush_on(level log_level);
            [[nodiscard]] const std::string& name() const;

            // Hot-path / sync entry (async_logger overrides to enqueue).
            virtual void sink_it_(const details::log_msg& msg);
            // Worker-side write: always drains to sinks (never re-enqueues).
            void backend_sink_it_(const details::log_msg& msg);
            // Worker-side flush: never re-posts onto the async queue.
            void backend_flush_();

        protected:
            std::string name_;

        private:
            [[nodiscard]] std::shared_ptr<const sink_list> load_sinks_() const;
            void store_sinks_(std::shared_ptr<sink_list> next);
            void flush_sinks_(const sink_list& sinks) const;

            // Writers serialize on this mutex and publish a new vector.
            // Readers (log/flush) only atomic_load the shared_ptr: no mutex, no
            // iterator invalidation, and the logger lock is never held during sink I/O.
            mutable std::mutex sinks_mutex_;
            std::shared_ptr<sink_list> sinks_;

            std::atomic<level> level_{level::trace};
            std::atomic<level> flush_level_{level::off};
    };
}
