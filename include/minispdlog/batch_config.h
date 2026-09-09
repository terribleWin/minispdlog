#pragma once

#include "common.h"

#include <chrono>
#include <cstddef>

namespace minispdlog {

/// How far a committed batch is pushed toward stable storage.
enum class durability {
    /// fwrite only (libc may still buffer). Fastest; process crash can lose the last batch.
    none,
    /// fflush after each committed batch. Survives process crash once the kernel has the bytes.
    fflush,
    /// fflush + fsync / FlushFileBuffers. Survives OS crash; slowest.
    fsync
};

/// Batch + recovery knobs for `buffered_file_sink` / `json_file_sink`.
/// A threshold of 0 disables that trigger. Data also leaves the front buffer on
/// `flush()`, sink destruction, and `dump_buffered_logs()`.
struct batch_config {
    std::size_t max_bytes = 64 * 1024;
    std::size_t max_records = 256;
    std::chrono::milliseconds max_age{50};
    durability commit = durability::fflush;
    bool recover = true;

    /// Hold formatted lines in the front buffer until `flush()` / destructor.
    static batch_config until_flush() {
        batch_config cfg;
        cfg.max_bytes = 0;
        cfg.max_records = 0;
        cfg.max_age = std::chrono::milliseconds{0};
        return cfg;
    }
};

} // namespace minispdlog
