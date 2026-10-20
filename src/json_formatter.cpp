#include "minispdlog/json_formatter.h"
#include "minispdlog/details/datetime.h"
#include "minispdlog/details/utils.h"
#include "minispdlog/level.h"

#include <array>
#include <chrono>
#include <cstdint>

namespace minispdlog {
namespace {

    constexpr char kHex[] = "0123456789abcdef";
    constexpr char kReplacementCharacter[] = "\xEF\xBF\xBD";

    constexpr auto kNeedEscape = []() {
        std::array<unsigned char, 256> table{};
        for (int c = 0; c < 0x20; ++c) {
            table[static_cast<unsigned char>(c)] = 1;
        }
        table[static_cast<unsigned char>('"')] = 1;
        table[static_cast<unsigned char>('\\')] = 1;
        return table;
    }();

    constexpr std::array<string_view_t, 9> kReservedKeys{"time", "ts",  "level", "level_num", "logger",
                                                         "msg",  "tid", "pid",   "source"};

    bool is_reserved_key(string_view_t key) {
        for (auto reserved : kReservedKeys) {
            if (key == reserved) {
                return true;
            }
        }
        return false;
    }

    template <std::size_t N>
    void append_lit(fmt::memory_buffer& dest, const char (&text)[N]) {
        details::append_raw(dest, text, N - 1);
    }

    bool is_utf8_continuation(unsigned char c) {
        return (c & 0xC0U) == 0x80U;
    }

    std::size_t valid_utf8_sequence_length(const unsigned char* data, std::size_t remaining) {
        const auto c0 = data[0];
        if (c0 < 0x80U) {
            return 1;
        }
        if (c0 >= 0xC2U && c0 <= 0xDFU) {
            return remaining >= 2 && is_utf8_continuation(data[1]) ? 2 : 0;
        }
        if (remaining < 3) {
            return 0;
        }
        const auto c1 = data[1];
        const auto c2 = data[2];
        if (c0 == 0xE0U) {
            return c1 >= 0xA0U && c1 <= 0xBFU && is_utf8_continuation(c2) ? 3 : 0;
        }
        if ((c0 >= 0xE1U && c0 <= 0xECU) || (c0 >= 0xEEU && c0 <= 0xEFU)) {
            return is_utf8_continuation(c1) && is_utf8_continuation(c2) ? 3 : 0;
        }
        if (c0 == 0xEDU) {
            return c1 >= 0x80U && c1 <= 0x9FU && is_utf8_continuation(c2) ? 3 : 0;
        }
        if (remaining < 4) {
            return 0;
        }
        const auto c3 = data[3];
        if (c0 == 0xF0U) {
            return c1 >= 0x90U && c1 <= 0xBFU && is_utf8_continuation(c2) && is_utf8_continuation(c3) ? 4 : 0;
        }
        if (c0 >= 0xF1U && c0 <= 0xF3U) {
            return is_utf8_continuation(c1) && is_utf8_continuation(c2) && is_utf8_continuation(c3) ? 4 : 0;
        }
        if (c0 == 0xF4U) {
            return c1 >= 0x80U && c1 <= 0x8FU && is_utf8_continuation(c2) && is_utf8_continuation(c3) ? 4 : 0;
        }
        return 0;
    }

    void append_escaped(fmt::memory_buffer& dest, string_view_t text) {
        dest.push_back('"');
        const char* data = text.data();
        const std::size_t n = text.size();
        std::size_t i = 0;
        std::size_t run = 0;
        while (i < n) {
            const auto c = static_cast<unsigned char>(data[i]);
            if (kNeedEscape[c] != 0) {
                if (i > run) {
                    details::append_raw(dest, data + run, i - run);
                }
                switch (c) {
                case '"':
                    append_lit(dest, "\\\"");
                    break;
                case '\\':
                    append_lit(dest, "\\\\");
                    break;
                case '\b':
                    append_lit(dest, "\\b");
                    break;
                case '\f':
                    append_lit(dest, "\\f");
                    break;
                case '\n':
                    append_lit(dest, "\\n");
                    break;
                case '\r':
                    append_lit(dest, "\\r");
                    break;
                case '\t':
                    append_lit(dest, "\\t");
                    break;
                default:
                    append_lit(dest, "\\u00");
                    dest.push_back(kHex[c >> 4]);
                    dest.push_back(kHex[c & 0x0f]);
                    break;
                }
                ++i;
                run = i;
                continue;
            }
            if (c >= 0x80U) {
                const auto* bytes = reinterpret_cast<const unsigned char*>(data + i);
                const auto sequence_length = valid_utf8_sequence_length(bytes, n - i);
                if (sequence_length == 0) {
                    if (i > run) {
                        details::append_raw(dest, data + run, i - run);
                    }
                    append_lit(dest, kReplacementCharacter);
                    ++i;
                    run = i;
                    continue;
                }
                const auto c1 = sequence_length >= 2 ? bytes[1] : 0;
                const auto c2 = sequence_length >= 3 ? bytes[2] : 0;
                if (c == 0xE2U && c1 == 0x80U && (c2 == 0xA8U || c2 == 0xA9U)) {
                    if (i > run) {
                        details::append_raw(dest, data + run, i - run);
                    }
                    if (c2 == 0xA8) {
                        append_lit(dest, "\\u2028");
                    } else {
                        append_lit(dest, "\\u2029");
                    }
                    i += 3;
                    run = i;
                    continue;
                }
                i += sequence_length;
                continue;
            }
            ++i;
        }
        if (n > run) {
            details::append_raw(dest, data + run, n - run);
        }
        dest.push_back('"');
    }

    template <std::size_t N>
    void append_key(fmt::memory_buffer& dest, const char (&key)[N], bool& first) {
        if (!first) {
            dest.push_back(',');
        }
        first = false;
        dest.push_back('"');
        details::append_raw(dest, key, N - 1);
        dest.push_back('"');
        dest.push_back(':');
    }

    void append_field_key(fmt::memory_buffer& dest, string_view_t key, bool& first) {
        if (!first) {
            dest.push_back(',');
        }
        first = false;
        append_escaped(dest, key);
        dest.push_back(':');
    }

    string_view_t safe_string_view(const char* str) {
        return str != nullptr ? string_view_t(str) : string_view_t{};
    }

} // namespace

json_formatter::field* json_formatter::upsert_(std::string key) {
    for (auto& item : fields_) {
        if (item.key == key) {
            return &item;
        }
    }
    fields_.push_back({});
    fields_.back().key = std::move(key);
    return &fields_.back();
}

json_formatter& json_formatter::add(std::string key, std::string value) {
    if (key.empty() || is_reserved_key(key)) {
        return *this;
    }
    auto* item = upsert_(std::move(key));
    item->type = field::kind::string;
    item->str = std::move(value);
    item->i64 = 0;
    item->boolean = false;
    return *this;
}

json_formatter& json_formatter::add_int(std::string key, std::int64_t value) {
    if (key.empty() || is_reserved_key(key)) {
        return *this;
    }
    auto* item = upsert_(std::move(key));
    item->type = field::kind::integer;
    item->str.clear();
    item->i64 = value;
    item->boolean = false;
    return *this;
}

json_formatter& json_formatter::add_bool(std::string key, bool value) {
    if (key.empty() || is_reserved_key(key)) {
        return *this;
    }
    auto* item = upsert_(std::move(key));
    item->type = field::kind::boolean;
    item->str.clear();
    item->i64 = 0;
    item->boolean = value;
    return *this;
}

json_formatter& json_formatter::add_null(std::string key) {
    if (key.empty() || is_reserved_key(key)) {
        return *this;
    }
    auto* item = upsert_(std::move(key));
    item->type = field::kind::null;
    item->str.clear();
    item->i64 = 0;
    item->boolean = false;
    return *this;
}

json_formatter& json_formatter::with_host() {
    return add("host", details::get_hostname());
}

void json_formatter::clear_fields() {
    fields_.clear();
}

void json_formatter::format(const details::log_msg& msg, fmt::memory_buffer& dest) {
    dest.push_back('{');
    bool first = true;

    clock_.refresh(msg.time);
    append_key(dest, "time", first);
    dest.push_back('"');
    details::append_raw(dest, clock_.ymd_hms, 19);
    dest.push_back('.');
    details::append_padded3(dest, details::millis_of_second(msg.time));
    dest.push_back('Z');
    dest.push_back('"');

    append_key(dest, "ts", first);
    const auto ts = std::chrono::floor<std::chrono::milliseconds>(msg.time.time_since_epoch()).count();
    details::append_int64(dest, static_cast<std::int64_t>(ts));

    append_key(dest, "level", first);
    append_escaped(dest, level_to_string_view(msg.lvl));

    append_key(dest, "level_num", first);
    details::append_int64(dest, static_cast<std::int64_t>(msg.lvl));

    append_key(dest, "logger", first);
    append_escaped(dest, msg.logger_name);

    append_key(dest, "msg", first);
    append_escaped(dest, msg.payload);

    append_key(dest, "tid", first);
    details::append_uint64(dest, static_cast<std::uint64_t>(msg.thread_id));

    append_key(dest, "pid", first);
    details::append_uint64(dest, static_cast<std::uint64_t>(msg.process_id));

    for (const auto& item : fields_) {
        append_field_key(dest, item.key, first);
        switch (item.type) {
        case field::kind::string:
            append_escaped(dest, item.str);
            break;
        case field::kind::integer:
            details::append_int64(dest, item.i64);
            break;
        case field::kind::boolean:
            if (item.boolean) {
                append_lit(dest, "true");
            } else {
                append_lit(dest, "false");
            }
            break;
        case field::kind::null:
            append_lit(dest, "null");
            break;
        }
    }

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

} // minispdlog
