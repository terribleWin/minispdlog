#include "minispdlog/json_formatter.h"
#include "minispdlog/details/datetime.h"
#include "minispdlog/level.h"

#include <chrono>
#include <cstdint>
#include <cstring>

namespace minispdlog {
namespace {

constexpr char kHex[] = "0123456789abcdef";

void append_cstr(fmt::memory_buffer& dest, const char* text) {
    if (text == nullptr) {
        return;
    }
    details::append_raw(dest, text, std::strlen(text));
}

void append_escaped(fmt::memory_buffer& dest, string_view_t text) {
    dest.push_back('"');
    for (unsigned char c : text) {
        switch (c) {
            case '"':
                append_cstr(dest, "\\\"");
                break;
            case '\\':
                append_cstr(dest, "\\\\");
                break;
            case '\b':
                append_cstr(dest, "\\b");
                break;
            case '\f':
                append_cstr(dest, "\\f");
                break;
            case '\n':
                append_cstr(dest, "\\n");
                break;
            case '\r':
                append_cstr(dest, "\\r");
                break;
            case '\t':
                append_cstr(dest, "\\t");
                break;
            default:
                if (c < 0x20) {
                    append_cstr(dest, "\\u00");
                    dest.push_back(kHex[c >> 4]);
                    dest.push_back(kHex[c & 0x0f]);
                } else {
                    dest.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    dest.push_back('"');
}

void append_key(fmt::memory_buffer& dest, const char* key, bool& first) {
    if (!first) {
        dest.push_back(',');
    }
    first = false;
    dest.push_back('"');
    append_cstr(dest, key);
    dest.push_back('"');
    dest.push_back(':');
}

string_view_t safe_string_view(const char* str) {
    return str != nullptr ? string_view_t(str) : string_view_t{};
}

} // namespace

void json_formatter::format(const details::log_msg& msg, fmt::memory_buffer& dest) {
    dest.push_back('{');
    bool first = true;

    clock_.refresh(msg.time);
    append_key(dest, "time", first);
    dest.push_back('"');
    details::append_raw(dest, clock_.ymd_hms, 19);
    dest.push_back('.');
    details::append_padded3(dest, details::millis_of_second(msg.time));
    dest.push_back('"');

    append_key(dest, "ts", first);
    const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(msg.time.time_since_epoch())
                        .count();
    details::append_int64(dest, static_cast<std::int64_t>(ts));

    append_key(dest, "level", first);
    append_escaped(dest, level_to_string(msg.lvl));

    append_key(dest, "logger", first);
    append_escaped(dest, msg.logger_name);

    append_key(dest, "msg", first);
    append_escaped(dest, msg.payload);

    append_key(dest, "tid", first);
    details::append_uint64(dest, static_cast<std::uint64_t>(msg.thread_id));

    append_key(dest, "pid", first);
    details::append_uint64(dest, static_cast<std::uint64_t>(msg.process_id));

    if (!msg.source.empty()) {
        append_key(dest, "source", first);
        dest.push_back('{');
        bool src_first = true;
        append_key(dest, "file", src_first);
        append_escaped(dest, safe_string_view(msg.source.filename));
        append_key(dest, "line", src_first);
        details::append_int64(dest, static_cast<std::int64_t>(msg.source.line));
        append_key(dest, "func", src_first);
        append_escaped(dest, safe_string_view(msg.source.funcname));
        dest.push_back('}');
    }

    dest.push_back('}');
    dest.push_back('\n');
}

std::unique_ptr<formatter> json_formatter::clone() const {
    return std::make_unique<json_formatter>(*this);
}

} // namespace minispdlog
