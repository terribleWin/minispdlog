#include "minispdlog/sinks/daily_file_sink.h"

#include <fmt/format.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace minispdlog {
namespace sinks {
    namespace {

        std::tm local_tm(log_clock::time_point tp) {
            const auto time = log_clock::to_time_t(tp);
            std::tm out{};
#ifdef _WIN32
            localtime_s(&out, &time);
#else
            localtime_r(&time, &out);
#endif
            return out;
        }

        void split_basename_ext(const std::string& path, std::string& basename, std::string& ext) {
            const auto dot_pos = path.find_last_of('.');
            const auto slash_pos = path.find_last_of("/\\");
            if (dot_pos != std::string::npos && (slash_pos == std::string::npos || dot_pos > slash_pos)) {
                basename = path.substr(0, dot_pos);
                ext = path.substr(dot_pos);
            } else {
                basename = path;
                ext.clear();
            }
        }

        std::chrono::year_month_day ymd_from_tm(const std::tm& date) {
            return std::chrono::year_month_day{std::chrono::year{date.tm_year + 1900},
                                               std::chrono::month{static_cast<unsigned>(date.tm_mon + 1)},
                                               std::chrono::day{static_cast<unsigned>(date.tm_mday)}};
        }

    } // namespace

    template <typename Mutex>
    daily_file_sink<Mutex>::daily_file_sink(std::string base_filename, int rotation_hour, int rotation_minute,
                                            bool truncate, std::size_t max_files)
        : base_filename_(std::move(base_filename)), rotation_hour_(rotation_hour), rotation_minute_(rotation_minute),
          truncate_(truncate), max_files_(max_files) {
        if (rotation_hour_ < 0 || rotation_hour_ > 23) {
            throw std::invalid_argument("daily_file_sink: rotation_hour must be 0-23");
        }
        if (rotation_minute_ < 0 || rotation_minute_ > 59) {
            throw std::invalid_argument("daily_file_sink: rotation_minute must be 0-59");
        }
        if (base_filename_.empty()) {
            throw std::invalid_argument("daily_file_sink: base_filename must not be empty");
        }

        const auto now = log_clock::now();
        const auto now_tm = local_tm(now);
        open_file_(calc_filename(base_filename_, now_tm), truncate_);
        rotation_tp_ = next_rotation_tp_(now);
        delete_old_(now_tm);
    }

    template <typename Mutex>
    daily_file_sink<Mutex>::~daily_file_sink() = default;

    template <typename Mutex>
    std::string daily_file_sink<Mutex>::filename() const {
        return current_filename_;
    }

    template <typename Mutex>
    std::string daily_file_sink<Mutex>::calc_filename(const std::string& base_filename, const std::tm& date) {
        return calc_filename(base_filename, date.tm_year + 1900, date.tm_mon + 1, date.tm_mday);
    }

    template <typename Mutex>
    std::string daily_file_sink<Mutex>::calc_filename(const std::string& base_filename, int year, int month, int day) {
        std::string basename;
        std::string ext;
        split_basename_ext(base_filename, basename, ext);
        return fmt::format("{}.{:04d}-{:02d}-{:02d}{}", basename, year, month, day, ext);
    }

    template <typename Mutex>
    void daily_file_sink<Mutex>::sink_it_(const details::log_msg& msg) {
        if (msg.time >= rotation_tp_) {
            const auto date = local_tm(msg.time);
            open_file_(calc_filename(base_filename_, date), truncate_);
            rotation_tp_ = next_rotation_tp_(msg.time);
            delete_old_(date);
        }

        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);
        if (file_) {
            std::fwrite(formatted.data(), 1, formatted.size(), file_.get());
        }
    }

    template <typename Mutex>
    void daily_file_sink<Mutex>::flush_() {
        if (file_) {
            std::fflush(file_.get());
        }
    }

    template <typename Mutex>
    void daily_file_sink<Mutex>::open_file_(const std::string& filename, bool truncate) {
        file_.reset();
        const char* mode = truncate ? "wb" : "ab";
        std::FILE* opened = std::fopen(filename.c_str(), mode);
        if (opened == nullptr) {
            throw std::runtime_error("daily_file_sink: Failed to open file: " + filename);
        }
        file_.reset(opened);
        current_filename_ = filename;
    }

    template <typename Mutex>
    void daily_file_sink<Mutex>::delete_old_(const std::tm& current_tm) {
        if (max_files_ == 0) {
            return;
        }

        std::string stem;
        std::string ext;
        split_basename_ext(base_filename_, stem, ext);

        const std::filesystem::path stem_path(stem);
        std::filesystem::path dir = stem_path.parent_path();
        if (dir.empty()) {
            dir = ".";
        }
        const std::string name_prefix = stem_path.filename().string() + ".";

        const auto cutoff =
            std::chrono::sys_days{ymd_from_tm(current_tm)} - std::chrono::days{static_cast<int>(max_files_)};

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec || !entry.is_regular_file(ec)) {
                continue;
            }
            const auto fname = entry.path().filename().string();
            const auto date_offset = name_prefix.size();
            if (fname.size() < date_offset + 10 + ext.size()) {
                continue;
            }
            if (fname.compare(0, date_offset, name_prefix) != 0) {
                continue;
            }
            if (!ext.empty() && fname.compare(fname.size() - ext.size(), ext.size(), ext) != 0) {
                continue;
            }

            int year = 0;
            int month = 0;
            int day = 0;
            const auto date_part = fname.substr(date_offset, 10);
            if (std::sscanf(date_part.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
                continue;
            }
            const auto file_ymd =
                std::chrono::year_month_day{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                                            std::chrono::day{static_cast<unsigned>(day)}};
            if (!file_ymd.ok()) {
                continue;
            }
            if (std::chrono::sys_days{file_ymd} <= cutoff) {
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }

    template <typename Mutex>
    log_clock::time_point daily_file_sink<Mutex>::next_rotation_tp_(log_clock::time_point from) const {
        std::tm date = local_tm(from);
        date.tm_hour = rotation_hour_;
        date.tm_min = rotation_minute_;
        date.tm_sec = 0;
        date.tm_isdst = -1;
        const auto rotation = log_clock::from_time_t(std::mktime(&date));
        if (rotation > from) {
            return rotation;
        }
        return rotation + std::chrono::hours(24);
    }

    template class daily_file_sink<std::mutex>;
    template class daily_file_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog
