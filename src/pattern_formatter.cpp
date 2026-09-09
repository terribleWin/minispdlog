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

}  // namespace

pattern_formatter::pattern_formatter(std::string pattern)
    : pattern_(std::move(pattern)) {
    compile_pattern();
}

void pattern_formatter::push_literal_(std::string& raw) {
    if (raw.empty()) {
        return;
    }
    pieces_.push_back(piece{piece_kind::literal, static_cast<std::uint16_t>(literals_.size())});
    literals_.push_back(std::move(raw));
    raw.clear();
}

void pattern_formatter::format(const details::log_msg& msg, fmt::memory_buffer& dest) {
    if (default_layout_) {
        format_default_(msg, dest);
        return;
    }
    msg.color_range_start = 0;
    msg.color_range_end = 0;
    if (needs_calendar_) {
        clock_.refresh(msg.time);
    }
    for (const auto& p : pieces_) {
        switch (p.kind) {
            case piece_kind::literal: {
                const auto& s = literals_[p.lit];
                dest.append(s.data(), s.data() + s.size());
                break;
            }
            case piece_kind::year:
                details::append_raw(dest, clock_.ymd_hms, 4);
                break;
            case piece_kind::month:
                details::append_raw(dest, clock_.ymd_hms + 5, 2);
                break;
            case piece_kind::day:
                details::append_raw(dest, clock_.ymd_hms + 8, 2);
                break;
            case piece_kind::hour:
                details::append_raw(dest, clock_.ymd_hms + 11, 2);
                break;
            case piece_kind::minute:
                details::append_raw(dest, clock_.ymd_hms + 14, 2);
                break;
            case piece_kind::second:
                details::append_raw(dest, clock_.ymd_hms + 17, 2);
                break;
            case piece_kind::millis:
                details::append_padded3(dest, details::millis_of_second(msg.time));
                break;
            case piece_kind::micros:
                details::append_padded6(dest, details::micros_of_second(msg.time));
                break;
            case piece_kind::level_short:
                dest.push_back(*level_to_short_string(msg.lvl));
                break;
            case piece_kind::level_full: {
                const auto name = level_to_string_view(msg.lvl);
                dest.append(name.data(), name.data() + name.size());
                break;
            }
            case piece_kind::name:
                dest.append(msg.logger_name.data(),
                            msg.logger_name.data() + msg.logger_name.size());
                break;
            case piece_kind::payload:
                dest.append(msg.payload.data(), msg.payload.data() + msg.payload.size());
                break;
            case piece_kind::tid:
                details::append_uint64(dest, static_cast<std::uint64_t>(msg.thread_id));
                break;
            case piece_kind::pid:
                details::append_uint64(dest, static_cast<std::uint64_t>(msg.process_id));
                break;
            case piece_kind::src_file:
                if (!msg.source.empty()) {
                    append_str(dest, basename(msg.source.filename));
                }
                break;
            case piece_kind::src_path:
                if (!msg.source.empty()) {
                    append_str(dest, msg.source.filename);
                }
                break;
            case piece_kind::src_line:
                if (!msg.source.empty()) {
                    details::append_int64(dest, static_cast<std::int64_t>(msg.source.line));
                }
                break;
            case piece_kind::src_func:
                if (!msg.source.empty()) {
                    append_str(dest, msg.source.funcname);
                }
                break;
            case piece_kind::src_loc:
                if (!msg.source.empty()) {
                    append_str(dest, basename(msg.source.filename));
                    dest.push_back(':');
                    details::append_int64(dest, static_cast<std::int64_t>(msg.source.line));
                }
                break;
            case piece_kind::color_start:
                msg.color_range_start = dest.size();
                break;
            case piece_kind::color_stop:
                msg.color_range_end = dest.size();
                break;
        }
    }
    dest.push_back('\n');
}

void pattern_formatter::format_default_(const details::log_msg& msg, fmt::memory_buffer& dest) {
    // "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] %v\n"
    clock_.refresh(msg.time);
    dest.push_back('[');
    details::append_raw(dest, clock_.ymd_hms, 19);
    dest.push_back('.');
    details::append_padded3(dest, details::millis_of_second(msg.time));
    dest.push_back(']');
    dest.push_back(' ');
    dest.push_back('[');
    dest.append(msg.logger_name.data(), msg.logger_name.data() + msg.logger_name.size());
    dest.push_back(']');
    dest.push_back(' ');
    dest.push_back('[');
    msg.color_range_start = dest.size();
    const auto lvl = level_to_string_view(msg.lvl);
    dest.append(lvl.data(), lvl.data() + lvl.size());
    msg.color_range_end = dest.size();
    dest.push_back(']');
    dest.push_back(' ');
    dest.append(msg.payload.data(), msg.payload.data() + msg.payload.size());
    dest.push_back('\n');
}

std::unique_ptr<formatter> pattern_formatter::clone() const {
    return std::make_unique<pattern_formatter>(pattern_);
}

void pattern_formatter::set_pattern(std::string pattern) {
    pattern_ = std::move(pattern);
    pieces_.clear();
    literals_.clear();
    needs_calendar_ = false;
    default_layout_ = false;
    compile_pattern();
}

void pattern_formatter::compile_pattern() {
    std::string raw_str;
    for (size_t i = 0; i < pattern_.size(); ++i) {
        if (pattern_[i] == '%' && i + 1 < pattern_.size()) {
            char flag = pattern_[++i];
            auto push_flag = [&](piece_kind kind, bool calendar = false) {
                push_literal_(raw_str);
                if (calendar) {
                    needs_calendar_ = true;
                }
                pieces_.push_back(piece{kind, 0});
            };
            switch (flag) {
                case 'Y':
                    push_flag(piece_kind::year, true);
                    break;
                case 'm':
                    push_flag(piece_kind::month, true);
                    break;
                case 'd':
                    push_flag(piece_kind::day, true);
                    break;
                case 'H':
                    push_flag(piece_kind::hour, true);
                    break;
                case 'M':
                    push_flag(piece_kind::minute, true);
                    break;
                case 'S':
                    push_flag(piece_kind::second, true);
                    break;
                case 'e':
                    push_flag(piece_kind::millis);
                    break;
                case 'f':
                    push_flag(piece_kind::micros);
                    break;
                case 'l':
                    push_flag(piece_kind::level_short);
                    break;
                case 'L':
                    push_flag(piece_kind::level_full);
                    break;
                case 'n':
                    push_flag(piece_kind::name);
                    break;
                case 'v':
                    push_flag(piece_kind::payload);
                    break;
                case 't':
                    push_flag(piece_kind::tid);
                    break;
                case 'P':
                    push_flag(piece_kind::pid);
                    break;
                case 's':
                    push_flag(piece_kind::src_file);
                    break;
                case 'g':
                    push_flag(piece_kind::src_path);
                    break;
                case '#':
                    push_flag(piece_kind::src_line);
                    break;
                case '!':
                    push_flag(piece_kind::src_func);
                    break;
                case '@':
                    push_flag(piece_kind::src_loc);
                    break;
                case '^':
                    push_flag(piece_kind::color_start);
                    break;
                case '$':
                    push_flag(piece_kind::color_stop);
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
    push_literal_(raw_str);
    default_layout_ = (pattern_ == default_pattern);
}

}  // namespace minispdlog
