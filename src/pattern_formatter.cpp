#include "minispdlog/pattern_formatter.h"
#include "minispdlog/details/utils.h"
#include <iomanip>
#include <sstream>
#include <cctype>
//flag_formatter 对各个占位符的处理
namespace minispdlog {
    namespace {
    //普通文本
    class raw_string_formatter : public pattern_formatter::flag_formatter {
        public:
            explicit raw_string_formatter(std::string str) :str_(std::move(str)) {}
            void format(const details::log_msg& msg, fmt::memory_buffer& dest) override {
                dest.append(str_.data(),str_.data() + str_.size());
            }
            std::unique_ptr<flag_formatter> clone() const override {
                return std::make_unique<raw_string_formatter>(str_);
            }
        private:
            std::string str_; //存储普通文本
    };
    // 年份 %Y
    class year_formatter : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg&,const std::tm& ctm_time, fmt::memory_buffer& dest) override {
                fmt::format_to(std::back_inserter(dest), "{:04d}", ctm_time.tm_year + 1900);
            }
            std::unique_ptr<flag_formatter> clone() const override {
                return std::make_unique<year_formatter> ();
            }
    };
    //月份 %m
    class month_formatter : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg&,const std::tm& ctm_time, fmt::memory_buffer& dest) override{
                fmt::format_to(std::back_inserter(dest), "{:02d}", ctm_time.tm_mon + 1);
            }
            std::unique_ptr<flag_formatter> clone() const override {
                return std::make_unique<month_formatter> ();
            }
    };
    //日期 %d
    class day_formatter : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg&,const std::tm& ctm_time, fmt::memory_buffer& dest) override {
                fmt::format_to(std::back_inserter(dest), "{:02d}", ctm_time.tm_mday);
            }
            std::unique_ptr<flag_formatter> clone() const override {
                return std::make_unique<day_formatter> ();
            }
    };
    //小时 %H
    class hour_formatter : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg&,const std::tm& ctm_time, fmt::memory_buffer& dest) override {
                fmt::format_to(std::back_inserter(dest), "{:02d}", ctm_time.tm_hour);
            }
            std::unique_ptr<flag_formatter> clone() const override {
                return std::make_unique<hour_formatter> ();
            }
    };
    //分钟 %M
    class minute_formatter : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg&,const std::tm& ctm_time, fmt::memory_buffer& dest) override {
                fmt::format_to(std::back_inserter(dest), "{:02d}", ctm_time.tm_min);
            }
            std::unique_ptr<flag_formatter> clone() const override {
                return std::make_unique<minute_formatter> ();
            }
    };
    //秒钟 %S
    class second_formatter : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg&,const std::tm& ctm_time, fmt::memory_buffer& dest) override {
                fmt::format_to(std::back_inserter(dest), "{:02d}", ctm_time.tm_sec);
            }
            std::unique_ptr<flag_formatter> clone() const override {
                return std::make_unique<second_formatter> ();
            }
    };
    //日志级别 %l
    class level_full_formatter : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg& msg,const std::tm&, fmt::memory_buffer& dest) override {
                const char* level_str = details::level_to_string(msg.level);
                dest.append(level_str, level_str + std::strlen(level_str));
            }
            std::unique_ptr<flag_formatter> clone() const override {
                return std::make_unique<level_full_formatter> ();
            }
    };
    //logger名称 %n
    class logger_name_formatter : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg& msg,const std::tm&, fmt::memory_buffer& dest) override {
                dest.append(msg.logger_name.data(), msg.logger_name.data() + msg.logger_name.size());
            }
            std::unique_ptr<flag_formatter> clone() const override {    
                return std::make_unique<logger_name_formatter> ();
            }
    };
    //日志内容 %v
    class pay_load : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg& msg,const std::tm&, fmt::memory_buffer& dest) override {
                dest.append(msg.payload.data(), msg.payload.data() + msg.payload.size());
            }
            std::unique_ptr<flag_formatter> clone() const override {
                return std::make_unique<pay_load> ();
            }
    };
    //%t 线程id
    class thread_id_formatter : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg& msg,const std::tm&, fmt::memory_buffer& dest) override {
                fmt::format_to(std::back_inserter(dest), "{}", msg.thread_id);
            }
            std::unique_ptr<flag_formatter> clone() const override {
                return std::make_unique<thread_id_formatter> ();
            }
    };
}//anonymous namespace
    pattern_formatter::pattern_formatter(std::string pattern) 
        : pattern_(std::move(pattern)) {
        compile_pattern();
    }
    /*pattern: [%Y-%m-%d %H:%M:%S] [%l] %v
    output: [2026-05-01 12:00:00] [info] Hello, world!
    */
    void pattern_formatter::format(const details::log_msg& msg, fmt::memory_buffer& dest) {
        //缓存
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(
            msg.time.time_since_epoch()
        );
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
                    case 'Y': formatters_.push_back(std::make_unique<year_formatter>()); break;
                    case 'm': formatters_.push_back(std::make_unique<month_formatter>()); break;
                    case 'd': formatters_.push_back(std::make_unique<day_formatter>()); break;
                    case 'H': formatters_.push_back(std::make_unique<hour_formatter>()); break;
                    case 'M': formatters_.push_back(std::make_unique<minute_formatter>()); break;
                    case 'S': formatters_.push_back(std::make_unique<second_formatter>()); break;
                    case 'L': formatters_.push_back(std::make_unique<level_full_formatter>()); break;
                    case 'l': formatters_.push_back(std::make_unique<level_formatter>()); break;
                    case 'n': formatters_.push_back(std::make_unique<logger_name_formatter>()); break;
                    case 'v': formatters_.push_back(std::make_unique<pay_load>()); break;
                    case 't': formatters_.push_back(std::make_unique<thread_id_formatter>()); break;
                    case '%': raw_str += '%'; break; //转义 %%
                    default: 
                        raw_str += '%';
                        raw_str += flag; //未知占位符原样输出
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
}
