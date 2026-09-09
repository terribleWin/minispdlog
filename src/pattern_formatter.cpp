#include "minispdlog/pattern_formatter.h"
#include "minispdlog/details/datetime.h"

#include <cstring>

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

class raw_string_formatter : public pattern_formatter::flag_formatter {
public:
    explicit raw_string_formatter(std::string str)
        : str_(std::move(str)) {}
    void format(const details::log_msg&, const details::wall_clock_cache&,
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
    void format(const details::log_msg&, const details::wall_clock_cache& clock,
                fmt::memory_buffer& dest) override {
        details::append_raw(dest, clock.ymd_hms, 4);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<year_formatter>();
    }
};

class month_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const details::wall_clock_cache& clock,
                fmt::memory_buffer& dest) override {
        details::append_raw(dest, clock.ymd_hms + 5, 2);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<month_formatter>();
    }
};

class day_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const details::wall_clock_cache& clock,
                fmt::memory_buffer& dest) override {
        details::append_raw(dest, clock.ymd_hms + 8, 2);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<day_formatter>();
    }
};

class hour_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const details::wall_clock_cache& clock,
                fmt::memory_buffer& dest) override {
        details::append_raw(dest, clock.ymd_hms + 11, 2);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<hour_formatter>();
    }
};

class minute_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const details::wall_clock_cache& clock,
                fmt::memory_buffer& dest) override {
        details::append_raw(dest, clock.ymd_hms + 14, 2);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<minute_formatter>();
    }
};

class second_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg&, const details::wall_clock_cache& clock,
                fmt::memory_buffer& dest) override {
        details::append_raw(dest, clock.ymd_hms + 17, 2);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<second_formatter>();
    }
};

class millis_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        details::append_padded3(dest, details::millis_of_second(msg.time));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<millis_formatter>();
    }
};

class micros_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        details::append_padded6(dest, details::micros_of_second(msg.time));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<micros_formatter>();
    }
};

class level_short_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        append_str(dest, level_to_short_string(msg.lvl));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<level_short_formatter>();
    }
};

class level_full_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        append_str(dest, level_to_string(msg.lvl));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<level_full_formatter>();
    }
};

class logger_name_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        dest.append(msg.logger_name.data(), msg.logger_name.data() + msg.logger_name.size());
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<logger_name_formatter>();
    }
};

class pay_load : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        dest.append(msg.payload.data(), msg.payload.data() + msg.payload.size());
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<pay_load>();
    }
};

class thread_id_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        details::append_uint64(dest, static_cast<std::uint64_t>(msg.thread_id));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<thread_id_formatter>();
    }
};

class pid_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        details::append_uint64(dest, static_cast<std::uint64_t>(msg.process_id));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<pid_formatter>();
    }
};

class source_filename_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        if (msg.source.empty()) {
            return;
        }
        append_str(dest, basename(msg.source.filename));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<source_filename_formatter>();
    }
};

class source_path_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        if (msg.source.empty()) {
            return;
        }
        append_str(dest, msg.source.filename);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<source_path_formatter>();
    }
};

class source_line_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        if (msg.source.empty()) {
            return;
        }
        details::append_int64(dest, static_cast<std::int64_t>(msg.source.line));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<source_line_formatter>();
    }
};

class source_func_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        if (msg.source.empty()) {
            return;
        }
        append_str(dest, msg.source.funcname);
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<source_func_formatter>();
    }
};

class source_location_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        if (msg.source.empty()) {
            return;
        }
        append_str(dest, basename(msg.source.filename));
        dest.push_back(':');
        details::append_int64(dest, static_cast<std::int64_t>(msg.source.line));
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<source_location_formatter>();
    }
};

class color_start_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        msg.color_range_start = dest.size();
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<color_start_formatter>();
    }
};

class color_stop_formatter : public pattern_formatter::flag_formatter {
public:
    void format(const details::log_msg& msg, const details::wall_clock_cache&,
                fmt::memory_buffer& dest) override {
        msg.color_range_end = dest.size();
    }
    std::unique_ptr<flag_formatter> clone() const override {
        return std::make_unique<color_stop_formatter>();
    }
};

} // namespace

pattern_formatter::pattern_formatter(std::string pattern)
    : pattern_(std::move(pattern)) {
    compile_pattern();
}

void pattern_formatter::format(const details::log_msg& msg, fmt::memory_buffer& dest) {
    msg.color_range_start = 0;
    msg.color_range_end = 0;
    if (needs_calendar_) {
        clock_.refresh(msg.time);
    }
    for (const auto& formatter : formatters_) {
        formatter->format(msg, clock_, dest);
    }
    dest.push_back('\n');
}

std::unique_ptr<formatter> pattern_formatter::clone() const {
    return std::make_unique<pattern_formatter>(pattern_);
}

void pattern_formatter::set_pattern(std::string pattern) {
    pattern_ = std::move(pattern);
    formatters_.clear();
    needs_calendar_ = false;
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
                    needs_calendar_ = true;
                    formatters_.push_back(std::make_unique<year_formatter>());
                    break;
                case 'm':
                    needs_calendar_ = true;
                    formatters_.push_back(std::make_unique<month_formatter>());
                    break;
                case 'd':
                    needs_calendar_ = true;
                    formatters_.push_back(std::make_unique<day_formatter>());
                    break;
                case 'H':
                    needs_calendar_ = true;
                    formatters_.push_back(std::make_unique<hour_formatter>());
                    break;
                case 'M':
                    needs_calendar_ = true;
                    formatters_.push_back(std::make_unique<minute_formatter>());
                    break;
                case 'S':
                    needs_calendar_ = true;
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

} // namespace minispdlog
