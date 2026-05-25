#include "minispdlog/pattern_formatter.h"
#include "minispdlog/details/utils.h"
#include <iomanip>
#include <sstream>
#include <cctype>
//flag_formatter 对各个占位符的处理
namespace minispdlog {
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
    }
    // 年份 %Y
    class year_formatter : public pattern_formatter :: flag_formatter {
        public:
            void format(const details::log_msg&, fmt::memory_buffer& dest) override {
                fmt::format_to(std::back_inserter(dest), "{:04d}", ctm_time.tm_year + 1900);
            }
            std::unique_ptr<flag_formatter> clone() const override {
                
            }
    }
}