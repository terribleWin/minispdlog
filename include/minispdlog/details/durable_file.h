#pragma once

#include "../batch_config.h"
#include "../common.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace minispdlog {
namespace details {

/// Sidecar next to the log used as a write-ahead copy of the frozen batch.
MINISPDLOG_API std::string wal_path_for(const std::string& log_path);

/// Write a WAL file with the on-disk layout (for recovery tests that simulate a crash).
MINISPDLOG_API void write_wal_sidecar(const std::string& log_path, std::uint64_t target_offset,
                                      std::string_view payload);

/// Double-buffer + batch commit + WAL replay + torn-line salvage around one `FILE*`.
///
/// Front buffer collects formatted records. When a threshold hits (or `flush()`),
/// the buffers swap; the frozen side is written as one `fwrite`. The writer is not
/// thread-safe; the sink serializes `append` / `begin_commit` / `end_commit`.
/// `write_commit` may run without the sink mutex so producers can fill the new front.
class MINISPDLOG_API durable_file {
public:
    durable_file(std::string path, bool truncate, batch_config cfg);
    ~durable_file();

    durable_file(const durable_file&) = delete;
    durable_file& operator=(const durable_file&) = delete;

    void append(const char* data, std::size_t size);
    bool should_commit() const;
    bool committing() const noexcept { return committing_; }

    /// Swap front to frozen. `data`/`size` view the frozen bytes until `end_commit()`.
    bool begin_commit(const char*& data, std::size_t& size);
    void write_commit(const char* data, std::size_t size);
    void end_commit() noexcept;

    /// Commit the front buffer if `should_commit()` (single-threaded / already locked).
    void commit_if_needed();
    /// Commit any remaining front bytes and apply `durability` to the main file.
    void flush();

    /// Best-effort write of the front buffer (tests + crash handler). Not signal-safe.
    void emergency_dump() noexcept;

    const std::string& path() const noexcept { return path_; }
    std::size_t front_size() const noexcept { return buf_[static_cast<std::size_t>(front_)].size(); }

private:
    void open_file_(bool truncate);
    void recover_on_open_();
    void salvage_torn_tail_();
    void apply_durability_(std::FILE* file) const;
    void write_wal_(std::uint64_t target_offset, const char* data, std::size_t size) const;
    void replay_wal_();
    void close_file_() noexcept;
    void prepare_front_clock_();

    std::string path_;
    batch_config cfg_{};
    std::FILE* file_{nullptr};
    std::vector<char> buf_[2]{};
    int front_{0};
    int frozen_{-1};
    std::size_t records_{0};
    bool has_first_{false};
    std::chrono::steady_clock::time_point first_append_{};
    bool committing_{false};
};

MINISPDLOG_API void register_durable_file(durable_file* file);
MINISPDLOG_API void unregister_durable_file(durable_file* file);
MINISPDLOG_API void dump_durable_files() noexcept;

} // namespace details

/// Best-effort: write every registered file's front buffer to disk.
MINISPDLOG_API void dump_buffered_logs() noexcept;

/// Install a process-wide handler that calls `dump_buffered_logs()` on fatal faults
/// (Windows unhandled SEH, POSIX SIGSEGV/SIGABRT/SIGILL/SIGFPE/SIGBUS). Idempotent.
MINISPDLOG_API void install_crash_flush();

} // namespace minispdlog
