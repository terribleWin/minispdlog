#pragma once
#include "common.h"
#include "details/datetime.h"
#include "formatter.h"
#include "level.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace minispdlog {
/**
 * @brief 将预编译模式串渲染为文本日志。
 *
 * 常用占位符：日期 `%Y-%m-%d`、本地时间 `%H:%M:%S.%e`、UTC 时间 `%u`、
 * Unix 毫秒时间戳 `%E`、源码位置 `%s:%#` 或 `%@`、原始正文 `%v`，以及
 * 适合单行日志的安全正文 `%q`。`%^...%$` 只标记需要着色的字节区间，
 * 由彩色 Sink 决定如何输出。模式在设置时编译为紧凑分派表，单条日志
 * 格式化时不再解析字符串。
 *
 * `%q` 将回车、换行、制表符和反斜杠分别编码为 `\\r`、`\\n`、`\\t`
 * 和 `\\\\`，其余 C0 控制字符编码为 `\\xHH`；UTF-8 等普通正文保持原样。
 *
 * @par 线程安全性
 * 格式化器含时间缓存，应由一个 Sink 在其锁保护下使用；多个 Sink 通过
 * `clone()` 持有独立实例。
 */
class pattern_formatter : public formatter {
public:
    /** @brief 默认的人类可读模式。 */
    static constexpr const char* default_pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] %v";

    /** @brief 构造并预编译模式串。 */
    explicit pattern_formatter(std::string pattern = default_pattern);
    ~pattern_formatter() override = default;

    /** @copydoc formatter::format */
    void format(const details::log_msg& msg, fmt::memory_buffer& dest) override;
    /** @copydoc formatter::clone */
    std::unique_ptr<formatter> clone() const override;
    /** @brief 替换并重新编译模式串。 */
    void set_pattern(std::string pattern);

private:
    enum class piece_kind : std::uint8_t {
        literal,
        year,
        month,
        day,
        hour,
        minute,
        second,
        millis,
        micros,
        level_short,
        level_full,
        name,
        payload,
        payload_single_line,
        utc_rfc3339_millis,
        epoch_millis,
        tid,
        pid,
        src_file,
        src_path,
        src_line,
        src_func,
        src_loc,
        color_start,
        color_stop
    };

    struct piece {
        piece_kind kind{piece_kind::literal};
        std::uint16_t lit{0};
    };

    void compile_pattern();
    void push_literal_(std::string& raw);
    void format_default_(const details::log_msg& msg, fmt::memory_buffer& dest);

    std::string pattern_;
    std::vector<piece> pieces_;
    std::vector<std::string> literals_;
    details::wall_clock_cache clock_{};
    details::utc_iso_cache utc_clock_{};
    bool needs_calendar_{false};
    bool needs_utc_{false};
    bool default_layout_{false};
};

} // minispdlog
