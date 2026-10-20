#pragma once

#include "details/datetime.h"
#include "formatter.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace minispdlog {

/**
 * @brief 将日志记录编码为单行 JSON（JSON Lines/NDJSON）。
 *
 * 字符串遵循 RFC 8259，并额外转义 U+2028/U+2029，保证一条记录始终占一行。
 * 时间字段同时提供 UTC ISO-8601 `time` 与 Unix 毫秒 `ts`。固定字段包括
 * `time`、`ts`、`level`、`level_num`、`logger`、`msg`、`tid`、`pid`，
 * 有源码位置时还会输出 `source`。调用 `add*()` 可附加静态资源字段。
 *
 * @note 配置资源字段应在开始并发记录日志之前完成。
 */
class MINISPDLOG_API json_formatter : public formatter {
public:
    json_formatter() = default;
    json_formatter(const json_formatter&) = default;
    json_formatter& operator=(const json_formatter&) = default;

    /** @brief 新增或替换字符串资源字段。 */
    json_formatter& add(std::string key, std::string value);
    /** @brief 新增或替换整数资源字段。 */
    json_formatter& add_int(std::string key, std::int64_t value);
    /** @brief 新增或替换布尔资源字段。 */
    json_formatter& add_bool(std::string key, bool value);
    /** @brief 新增或替换空值资源字段。 */
    json_formatter& add_null(std::string key);
    /** @brief 将本机主机名写入 `host` 资源字段。 */
    json_formatter& with_host();
    /** @brief 清空全部自定义资源字段。 */
    void clear_fields();

    /** @copydoc formatter::format */
    void format(const details::log_msg& msg, fmt::memory_buffer& dest) override;
    /** @copydoc formatter::clone */
    std::unique_ptr<formatter> clone() const override;

private:
    struct field {
        enum class kind : unsigned char { string, integer, boolean, null };
        std::string key;
        kind type{kind::string};
        std::string str;
        std::int64_t i64{0};
        bool boolean{false};
    };

    field* upsert_(std::string key);

    std::vector<field> fields_{};
    details::utc_iso_cache clock_{};
};

} ///< 结束 minispdlog 命名空间。
