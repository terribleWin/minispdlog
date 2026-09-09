#pragma once

#include "details/datetime.h"
#include "formatter.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace minispdlog {

// One JSON object per log line (JSON Lines / NDJSON) for collectors such as
// Filebeat, Loki, and Elasticsearch. RFC 8259 strings, UTC ISO-8601 `time`
// (`YYYY-MM-DDTHH:MM:SS.mmmZ`), Unix epoch milliseconds `ts`, and numeric
// `level_num`. U+2028 / U+2029 are escaped so a line remains one JSON value.
// Attach with set_formatter, or use json_* sinks which lock this formatter.
//
// Core keys: time, ts, level, level_num, logger, msg, tid, pid;
// source when source_loc is set. Extra resource fields via add() / with_host().
class MINISPDLOG_API json_formatter : public formatter {
public:
    json_formatter() = default;
    json_formatter(const json_formatter&) = default;
    json_formatter& operator=(const json_formatter&) = default;

    json_formatter& add(std::string key, std::string value);
    json_formatter& add_int(std::string key, std::int64_t value);
    json_formatter& add_bool(std::string key, bool value);
    json_formatter& add_null(std::string key);
    json_formatter& with_host();
    void clear_fields();

    void format(const details::log_msg& msg, fmt::memory_buffer& dest) override;
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

} // namespace minispdlog
