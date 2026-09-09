#pragma once

#include "details/datetime.h"
#include "formatter.h"

#include <memory>

namespace minispdlog {

// One JSON object per log line (JSON Lines / NDJSON). This is a real formatter,
// not a test double: it writes RFC 8259 strings, local wall time, and Unix epoch
// milliseconds. Attach to any sink with set_formatter, or use json_* sinks which
// lock this formatter (set_pattern / set_formatter cannot silently switch to text).
//
// Keys: time, ts, level, logger, msg, tid, pid; source when source_loc is set.
class MINISPDLOG_API json_formatter : public formatter {
public:
    json_formatter() = default;
    json_formatter(const json_formatter&) = default;
    json_formatter& operator=(const json_formatter&) = default;

    void format(const details::log_msg& msg, fmt::memory_buffer& dest) override;
    std::unique_ptr<formatter> clone() const override;

private:
    details::wall_clock_cache clock_{};
};

} // namespace minispdlog
