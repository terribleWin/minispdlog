#include "minispdlog/pattern_formatter.h"
#include "minispdlog/details/utils.h"

#include <chrono>
#include <cstring>

// flag_formatter 对各个占位符的处理
namespace minispdlog {
namespace {

void append_str(fmt::memory_buffer& dest, const char* s) {
    if (s == nullptr || *s == '\0') {
        return;
    }
    dest.append(s, s + std::strlen(s));
}

const char* basename(const char* path) {
    if (path == nullptr || *path == '\0') {
        return "";
    }
    const char* base = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

template <typename Duration>
int subsec_of_current_second(const log_clock::time_point& tp) {
    const auto duration = tp.time_since_epoch();
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(duration);
    return static_cast<int>(std::chrono::duration_cast<Duration>(duration - secs).count());
}

class raw_string_formatter : public pattern_formatter::flag_formatter {
public:
    explicit raw_string_formatter(std::string str)
        : str_(std::move(str)) {}
    void format(const details::log_msg& /*msg*/, const std::tm& /*ctm_time*/,
                fmt::memory_buffer& dest) override {
        dest.append(str_.data(), str_.data() + str_.size());
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<raw_string_formatter>(str_);
    }

private:
    std::string str_;
};

class year_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const std::tm& ctm_time, fmt::memory_buffer& dest) override {
        fmt::format_to(std::back_inserter(dest), "{:04d}", ctm_time.tm_year + 1900);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<year_formatter>();
    }
};

class month_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const std::tm& ctm_time, fmt::memory_buffer& dest) override {
        fmt::format_to(std::back_inserter(dest), "{:02d}", ctm_time.tm_mon + 1);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<month_formatter>();
    }
};

class day_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const std::tm& ctm_time, fmt::memory_buffer& dest) override {
        fmt::format_to(std::back_inserter(dest), "{:02d}", ctm_time.tm_mday);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<day_formatter>();
    }
};

class hour_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const std::tm& ctm_time, fmt::memory_buffer& dest) override {
        fmt::format_to(std::back_inserter(dest), "{:02d}", ctm_time.tm_hour);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<hour_formatter>();
    }
};

class minute_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const std::tm& ctm_time, fmt::memory_buffer& dest) override {
        fmt::format_to(std::back_inserter(dest), "{:02d}", ctm_time.tm_min);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<minute_formatter>();
    }
};

class second_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const std::tm& ctm_time, fmt::memory_buffer& dest) override {
        fmt::format_to(std::back_inserter(dest), "{:02d}", ctm_time.tm_sec);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<second_formatter>();
    }
};

// %e 毫秒 000-999
class millis_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        fmt::format_to(std::back_inserter(dest), "{:03d}",
                       subsec_of_current_second<std::chrono::milliseconds>(msg.time));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<millis_formatter>();
    }
};

// %f 微秒 000000-999999
class micros_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        fmt::format_to(std::back_inserter(dest), "{:06d}",
                       subsec_of_current_second<std::chrono::microseconds>(msg.time));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<micros_formatter>();
    }
};

// %l 短级别名
class level_short_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        append_str(dest, level_to_short_string(msg.lvl));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<level_short_formatter>();
    }
};

// %L 全级别名
class level_full_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        append_str(dest, level_to_string(msg.lvl));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<level_full_formatter>();
    }
};

class logger_name_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        dest.append(msg.logger_name.data(), msg.logger_name.data() + msg.logger_name.size());
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<logger_name_formatter>();
    }
};

class pay_load : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        dest.append(msg.payload.data(), msg.payload.data() + msg.payload.size());
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<pay_load>();
    }
};

class thread_id_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        fmt::format_to(std::back_inserter(dest), "{}", msg.thread_id);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<thread_id_formatter>();
    }
};

// %P 进程号
class pid_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        fmt::format_to(std::back_inserter(dest), "{}", msg.process_id);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<pid_formatter>();
    }
};

// %s 源文件名（basename）
class source_filename_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        if (msg.source.empty()) {
            return;
        }
        append_str(dest, basename(msg.source.filename));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<source_filename_formatter>();
    }
};

// %g 源文件路径
class source_path_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        if (msg.source.empty()) {
            return;
        }
        append_str(dest, msg.source.filename);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<source_path_formatter>();
    }
};

// %# 源码行号
class source_line_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        if (msg.source.empty()) {
            return;
        }
        fmt::format_to(std::back_inserter(dest), "{}", msg.source.line);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<source_line_formatter>();
    }
};

// %! 源码函数名
class source_func_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        if (msg.source.empty()) {
            return;
        }
        append_str(dest, msg.source.funcname);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<source_func_formatter>();
    }
};

// %@ basename:line
class source_location_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        if (msg.source.empty()) {
            return;
        }
        fmt::format_to(std::back_inserter(dest), "{}:{}", basename(msg.source.filename),
                       msg.source.line);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<source_location_formatter>();
    }
};

// %^ start of the color span (no output)
class color_start_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        msg.color_range_start = dest.size();
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<color_start_formatter>();
    }
};

// %$ end of the color span (no output)
class color_stop_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const std::tm&, fmt::memory_buffer& dest) override {
        msg.color_range_end = dest.size();
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<color_stop_formatter>();
    }
};

}  // namespace

pattern_formatter::pattern_formatter(std::string pattern)
    : pattern_(std::move(pattern)) {
    compile_pattern();
}

void pattern_formatter::format(const details::log_msg& msg, fmt::memory_buffer& dest) {
    msg.color_range_start = 0;
    msg.color_range_end = 0;
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(msg.time.time_since_epoch());
    if (secs != last_log_secs_) {
        cached_tm_ = get_time(msg);
        last_log_secs_ = secs;
    }
    for (const auto& formatter : formatters_) {
        formatter->format(msg, cached_tm_, dest);
    }
    dest.push_back('\n');
}

std::unique_ptr<formatter> pattern_formatter::clone() const {
    return std::make_unique<pattern_formatter>(pattern_);
}

void pattern_formatter::set_pattern(std::string pattern) {
    pattern_ = std::move(pattern);
    formatters_.clear();
    compile_pattern();
}

void pattern_formatter::compile_pattern() {
    std::string raw_str;
    for (size_t i = 0; i < pattern_.size(); ++i) {
        if (pattern_[i] == '%' && i + 1 < pattern_.size()) {
            if (!raw_str.empty()) {
                formatters_.push_back(std::make_unique<raw_string_formatter>(raw_str));
                raw_str.clear();
            }
            char flag = pattern_[++i];
            switch (flag) {
                case 'Y':
                    formatters_.push_back(std::make_unique<year_formatter>());
                    break;
                case 'm':
                    formatters_.push_back(std::make_unique<month_formatter>());
                    break;
                case 'd':
                    formatters_.push_back(std::make_unique<day_formatter>());
                    break;
                case 'H':
                    formatters_.push_back(std::make_unique<hour_formatter>());
                    break;
                case 'M':
                    formatters_.push_back(std::make_unique<minute_formatter>());
                    break;
                case 'S':
                    formatters_.push_back(std::make_unique<second_formatter>());
                    break;
                case 'e':
                    formatters_.push_back(std::make_unique<millis_formatter>());
                    break;
                case 'f':
                    formatters_.push_back(std::make_unique<micros_formatter>());
                    break;
                case 'l':
                    formatters_.push_back(std::make_unique<level_short_formatter>());
                    break;
                case 'L':
                    formatters_.push_back(std::make_unique<level_full_formatter>());
                    break;
                case 'n':
                    formatters_.push_back(std::make_unique<logger_name_formatter>());
                    break;
                case 'v':
                    formatters_.push_back(std::make_unique<pay_load>());
                    break;
                case 't':
                    formatters_.push_back(std::make_unique<thread_id_formatter>());
                    break;
                case 'P':
                    formatters_.push_back(std::make_unique<pid_formatter>());
                    break;
                case 's':
                    formatters_.push_back(std::make_unique<source_filename_formatter>());
                    break;
                case 'g':
                    formatters_.push_back(std::make_unique<source_path_formatter>());
                    break;
                case '#':
                    formatters_.push_back(std::make_unique<source_line_formatter>());
                    break;
                case '!':
                    formatters_.push_back(std::make_unique<source_func_formatter>());
                    break;
                case '@':
                    formatters_.push_back(std::make_unique<source_location_formatter>());
                    break;
                case '^':
                    formatters_.push_back(std::make_unique<color_start_formatter>());
                    break;
                case '$':
                    formatters_.push_back(std::make_unique<color_stop_formatter>());
                    break;
                case '%':
                    raw_str += '%';
                    break;
                default:
                    raw_str += '%';
                    raw_str += flag;
                    break;
            }
        } else {
            raw_str += pattern_[i];
        }
    }
    if (!raw_str.empty()) {
        formatters_.push_back(std::make_unique<raw_string_formatter>(raw_str));
    }
}

std::tm pattern_formatter::get_time(const details::log_msg& msg) {
    auto time_t = std::chrono::system_clock::to_time_t(msg.time);
    std::tm tm_time{};
#ifdef _WIN32
    localtime_s(&tm_time, &time_t);
#else
    localtime_r(&time_t, &tm_time);
#endif
    return tm_time;
}

}  // namespace minispdlog
