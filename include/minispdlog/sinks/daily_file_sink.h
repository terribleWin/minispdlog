#pragma once

#include "base_sink.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

namespace minispdlog {
namespace sinks {

    // daily_file_sink: rotate once per local calendar day (not hourly).
    //
    // Filename convention (same-scene '-' as %Y-%m-%d):
    //   logs/app.log  + 2026-09-05  ->  logs/app.2026-09-05.log
    //
    // Rotation uses log_msg::time so tests can inject a date. The optional
    // hour:minute is the daily cut-over (default 00:00).
    // max_files == 0 keeps every dated file; otherwise files older than
    // (current date - max_files days) are removed.
    template <typename Mutex>
    class daily_file_sink : public base_sink<Mutex> {
    public:
        daily_file_sink(std::string base_filename, int rotation_hour = 0, int rotation_minute = 0,
                        bool truncate = false, std::size_t max_files = 0);

        ~daily_file_sink() override;

        daily_file_sink(const daily_file_sink&) = delete;
        daily_file_sink& operator=(const daily_file_sink&) = delete;

        [[nodiscard]] std::string filename() const;

        // logs/app.log + {2026,9,5} -> logs/app.2026-09-05.log
        static std::string calc_filename(const std::string& base_filename, const std::tm& date);
        static std::string calc_filename(const std::string& base_filename, int year, int month, int day);

    protected:
        void sink_it_(const details::log_msg& msg) override;
        void flush_() override;

    private:
        struct file_closer {
            void operator()(std::FILE* file) const noexcept {
                if (file != nullptr) {
                    std::fclose(file);
                }
            }
        };

        void open_file_(const std::string& filename, bool truncate);
        void delete_old_(const std::tm& current_tm);
        [[nodiscard]] log_clock::time_point next_rotation_tp_(log_clock::time_point from) const;

        std::string base_filename_;
        int rotation_hour_;
        int rotation_minute_;
        bool truncate_;
        std::size_t max_files_;
        log_clock::time_point rotation_tp_{};
        std::string current_filename_;
        std::unique_ptr<std::FILE, file_closer> file_;
    };

    using daily_file_sink_mt = daily_file_sink<std::mutex>;
    using daily_file_sink_st = daily_file_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog
