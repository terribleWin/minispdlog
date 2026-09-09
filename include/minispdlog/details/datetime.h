#pragma once

#include "../common.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <fmt/format.h>

namespace minispdlog {
namespace details {

inline void write2(char* dest, int value) {
    dest[0] = static_cast<char>('0' + (value / 10) % 10);
    dest[1] = static_cast<char>('0' + value % 10);
}

inline void write3(char* dest, int value) {
    dest[0] = static_cast<char>('0' + (value / 100) % 10);
    dest[1] = static_cast<char>('0' + (value / 10) % 10);
    dest[2] = static_cast<char>('0' + value % 10);
}

inline void write4(char* dest, int value) {
    dest[0] = static_cast<char>('0' + (value / 1000) % 10);
    dest[1] = static_cast<char>('0' + (value / 100) % 10);
    dest[2] = static_cast<char>('0' + (value / 10) % 10);
    dest[3] = static_cast<char>('0' + value % 10);
}

inline void write6(char* dest, int value) {
    dest[0] = static_cast<char>('0' + (value / 100000) % 10);
    dest[1] = static_cast<char>('0' + (value / 10000) % 10);
    dest[2] = static_cast<char>('0' + (value / 1000) % 10);
    dest[3] = static_cast<char>('0' + (value / 100) % 10);
    dest[4] = static_cast<char>('0' + (value / 10) % 10);
    dest[5] = static_cast<char>('0' + value % 10);
}

inline void append_raw(fmt::memory_buffer& dest, const char* text, std::size_t n) {
    dest.append(text, text + n);
}

inline void append_padded2(fmt::memory_buffer& dest, int value) {
    char buf[2];
    write2(buf, value);
    append_raw(dest, buf, 2);
}

inline void append_padded3(fmt::memory_buffer& dest, int value) {
    char buf[3];
    write3(buf, value);
    append_raw(dest, buf, 3);
}

inline void append_padded6(fmt::memory_buffer& dest, int value) {
    char buf[6];
    write6(buf, value);
    append_raw(dest, buf, 6);
}

inline void append_uint64(fmt::memory_buffer& dest, std::uint64_t value) {
    char digits[20];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    while (n > 0) {
        dest.push_back(digits[--n]);
    }
}

inline void append_int64(fmt::memory_buffer& dest, std::int64_t value) {
    if (value < 0) {
        dest.push_back('-');
        const auto u = static_cast<std::uint64_t>(-(value + 1)) + 1;
        append_uint64(dest, u);
        return;
    }
    append_uint64(dest, static_cast<std::uint64_t>(value));
}

inline int subsec_count(const log_clock::time_point& tp, std::int64_t units_per_second) {
    const auto duration = tp.time_since_epoch();
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(duration);
    const auto rest = duration - secs;
    if (units_per_second == 1000) {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(rest).count());
    }
    return static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(rest).count());
}

inline int millis_of_second(const log_clock::time_point& tp) {
    return subsec_count(tp, 1000);
}

inline int micros_of_second(const log_clock::time_point& tp) {
    return subsec_count(tp, 1000000);
}

// "YYYY-MM-DD HH:MM:SS" rebuilt at most once per local second. tm_ is kept for
// callers that still need calendar fields; Y/m/d/H/M/S should memcpy from ymd_hms.
struct wall_clock_cache {
    std::chrono::seconds secs{std::chrono::seconds{-1}};
    std::tm tm{};
    char ymd_hms[20]{};

    void refresh(const log_clock::time_point& tp) {
        const auto next = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
        if (next == secs && ymd_hms[0] != '\0') {
            return;
        }
        const auto time_t_val = log_clock::to_time_t(tp);
#ifdef _WIN32
        localtime_s(&tm, &time_t_val);
#else
        localtime_r(&time_t_val, &tm);
#endif
        write4(ymd_hms, tm.tm_year + 1900);
        ymd_hms[4] = '-';
        write2(ymd_hms + 5, tm.tm_mon + 1);
        ymd_hms[7] = '-';
        write2(ymd_hms + 8, tm.tm_mday);
        ymd_hms[10] = ' ';
        write2(ymd_hms + 11, tm.tm_hour);
        ymd_hms[13] = ':';
        write2(ymd_hms + 14, tm.tm_min);
        ymd_hms[16] = ':';
        write2(ymd_hms + 17, tm.tm_sec);
        ymd_hms[19] = '\0';
        secs = next;
    }
};

} // namespace details
} // namespace minispdlog
