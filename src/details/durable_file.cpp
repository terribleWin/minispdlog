#include "minispdlog/details/durable_file.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <csignal>
#include <cstdio>

namespace minispdlog {
namespace details {
namespace {

constexpr char kWalMagic[4] = {'M', 'S', 'L', 'G'};
constexpr std::uint16_t kWalVersion = 1;
constexpr std::size_t kWalHeaderSize = 24;
constexpr std::size_t kSalvageWindow = 256 * 1024;
constexpr int kMaxCrashFiles = 64;

std::atomic<durable_file*> g_crash_files[kMaxCrashFiles]{};

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

std::FILE* fopen_raw(const char* path, const char* mode) {
    return std::fopen(path, mode);
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

void write_le16(char* dest, std::uint16_t value) {
    dest[0] = static_cast<char>(value & 0xff);
    dest[1] = static_cast<char>((value >> 8) & 0xff);
}

void write_le64(char* dest, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        dest[i] = static_cast<char>((value >> (8 * i)) & 0xff);
    }
}

std::uint16_t read_le16(const char* src) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(src[0]) |
                                      (static_cast<unsigned char>(src[1]) << 8));
}

std::uint64_t read_le64(const char* src) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(src[i])) << (8 * i);
    }
    return value;
}

void pack_wal_header(char out[kWalHeaderSize], std::uint64_t target_offset, std::uint64_t payload_size) {
    std::memcpy(out, kWalMagic, 4);
    write_le16(out + 4, kWalVersion);
    write_le16(out + 6, 0);
    write_le64(out + 8, target_offset);
    write_le64(out + 16, payload_size);
}

int seek_abs(std::FILE* file, std::uint64_t offset) {
#ifdef _WIN32
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET);
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET);
#endif
}

int seek_end(std::FILE* file) {
#ifdef _WIN32
    return _fseeki64(file, 0, SEEK_END);
#else
    return fseeko(file, 0, SEEK_END);
#endif
}

std::uint64_t file_size_of(std::FILE* file) {
    if (file == nullptr) {
        return 0;
    }
#ifdef _WIN32
    const auto size = _filelengthi64(_fileno(file));
    return size < 0 ? 0 : static_cast<std::uint64_t>(size);
#else
    struct stat info {};
    if (fstat(fileno(file), &info) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(info.st_size);
#endif
}

void truncate_file(std::FILE* file, std::uint64_t size) {
#ifdef _WIN32
    _chsize_s(_fileno(file), static_cast<__int64>(size));
#else
    ::ftruncate(fileno(file), static_cast<off_t>(size));
#endif
    seek_end(file);
}

void fsync_file(std::FILE* file) {
    if (file == nullptr) {
        return;
    }
    std::fflush(file);
#ifdef _WIN32
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file)));
    if (handle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(handle);
    }
#else
    ::fsync(fileno(file));
#endif
}

void remove_path(const std::string& path) {
    std::remove(path.c_str());
}

std::string read_all_file(const std::string& path) {
    std::FILE* file = fopen_raw(path.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }
    if (seek_end(file) != 0) {
        std::fclose(file);
        return {};
    }
    const auto size = file_size_of(file);
    if (seek_abs(file, 0) != 0) {
        std::fclose(file);
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        const auto n = std::fread(out.data(), 1, static_cast<std::size_t>(size), file);
        out.resize(n);
    }
    std::fclose(file);
    return out;
}

std::string_view complete_records_prefix(std::string_view payload) {
    if (payload.empty()) {
        return {};
    }
    if (payload.back() == '\n') {
        return payload;
    }
    const auto pos = payload.rfind('\n');
    if (pos == std::string_view::npos) {
        return {};
    }
    return payload.substr(0, pos + 1);
}

} // namespace

std::string wal_path_for(const std::string& log_path) {
    return log_path + ".minispdlog-wal";
}

void write_wal_sidecar(const std::string& log_path, std::uint64_t target_offset, std::string_view payload) {
    const auto wal = wal_path_for(log_path);
    std::FILE* file = fopen_raw(wal.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("Failed to open WAL file: " + wal);
    }
    char header[kWalHeaderSize];
    pack_wal_header(header, target_offset, payload.size());
    std::fwrite(header, 1, kWalHeaderSize, file);
    if (!payload.empty()) {
        std::fwrite(payload.data(), 1, payload.size(), file);
    }
    std::fflush(file);
    std::fclose(file);
}

void register_durable_file(durable_file* file) {
    if (file == nullptr) {
        return;
    }
    for (auto& slot : g_crash_files) {
        durable_file* expected = nullptr;
        if (slot.compare_exchange_strong(expected, file, std::memory_order_acq_rel)) {
            return;
        }
    }
}

void unregister_durable_file(durable_file* file) {
    if (file == nullptr) {
        return;
    }
    for (auto& slot : g_crash_files) {
        durable_file* expected = file;
        if (slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
            return;
        }
    }
}

void dump_durable_files() noexcept {
    for (auto& slot : g_crash_files) {
        durable_file* file = slot.load(std::memory_order_acquire);
        if (file != nullptr) {
            file->emergency_dump();
        }
    }
}

durable_file::durable_file(std::string path, bool truncate, batch_config cfg)
    : path_(std::move(path)), cfg_(cfg) {
    if (path_.empty()) {
        throw std::invalid_argument("durable_file: path must not be empty");
    }
    const auto reserve_bytes = cfg_.max_bytes > 0 ? cfg_.max_bytes : std::size_t{64 * 1024};
    buf_[0].reserve(reserve_bytes);
    buf_[1].reserve(reserve_bytes);
    open_file_(truncate);
    register_durable_file(this);
}

durable_file::~durable_file() {
    unregister_durable_file(this);
    try {
        flush();
    } catch (...) {
    }
    close_file_();
}

void durable_file::open_file_(bool truncate) {
    if (truncate) {
        remove_path(wal_path_for(path_));
        file_ = fopen_raw(path_.c_str(), "wb+");
        if (file_ == nullptr) {
            throw std::runtime_error("Failed to open file: " + path_);
        }
        return;
    }

    file_ = fopen_raw(path_.c_str(), "rb+");
    if (file_ == nullptr) {
        file_ = fopen_raw(path_.c_str(), "wb+");
    }
    if (file_ == nullptr) {
        throw std::runtime_error("Failed to open file: " + path_);
    }
    if (cfg_.recover) {
        recover_on_open_();
    } else {
        seek_end(file_);
    }
}

void durable_file::recover_on_open_() {
    replay_wal_();
    salvage_torn_tail_();
    seek_end(file_);
}

void durable_file::replay_wal_() {
    const auto wal = wal_path_for(path_);
    const auto raw = read_all_file(wal);
    if (raw.empty()) {
        remove_path(wal);
        return;
    }
    if (raw.size() < kWalHeaderSize) {
        remove_path(wal);
        return;
    }
    if (std::memcmp(raw.data(), kWalMagic, 4) != 0) {
        remove_path(wal);
        return;
    }
    if (read_le16(raw.data() + 4) != kWalVersion) {
        remove_path(wal);
        return;
    }
    const auto target = read_le64(raw.data() + 8);
    const auto payload_size = read_le64(raw.data() + 16);
    std::string_view payload = std::string_view(raw).substr(kWalHeaderSize);
    if (payload.size() > payload_size) {
        payload = payload.substr(0, static_cast<std::size_t>(payload_size));
    }
    payload = complete_records_prefix(payload);
    if (payload.empty()) {
        remove_path(wal);
        return;
    }

    std::fflush(file_);
    const auto size = file_size_of(file_);
    const auto intended = target + static_cast<std::uint64_t>(payload.size());
    if (size < target) {
        seek_end(file_);
        std::fwrite(payload.data(), 1, payload.size(), file_);
        apply_durability_(file_);
    } else if (size == target) {
        seek_end(file_);
        std::fwrite(payload.data(), 1, payload.size(), file_);
        apply_durability_(file_);
    } else if (size < intended) {
        truncate_file(file_, target);
        std::fwrite(payload.data(), 1, payload.size(), file_);
        apply_durability_(file_);
    }
    remove_path(wal);
}

void durable_file::salvage_torn_tail_() {
    std::fflush(file_);
    const auto size = file_size_of(file_);
    if (size == 0) {
        return;
    }
    const auto start = size > kSalvageWindow ? size - kSalvageWindow : std::uint64_t{0};
    const auto window = static_cast<std::size_t>(size - start);
    std::vector<char> buf(window);
    if (seek_abs(file_, start) != 0) {
        seek_end(file_);
        return;
    }
    const auto n = std::fread(buf.data(), 1, window, file_);
    if (n == 0) {
        seek_end(file_);
        return;
    }
    if (buf[n - 1] == '\n') {
        seek_end(file_);
        return;
    }
    std::size_t last_nl = n;
    while (last_nl > 0) {
        --last_nl;
        if (buf[last_nl] == '\n') {
            truncate_file(file_, start + static_cast<std::uint64_t>(last_nl) + 1);
            return;
        }
    }
    if (start == 0) {
        truncate_file(file_, 0);
        return;
    }
    seek_end(file_);
}

void durable_file::apply_durability_(std::FILE* file) const {
    if (file == nullptr) {
        return;
    }
    switch (cfg_.commit) {
        case durability::none:
            break;
        case durability::fsync:
            fsync_file(file);
            break;
        case durability::fflush:
        default:
            std::fflush(file);
            break;
    }
}

void durable_file::write_wal_(std::uint64_t target_offset, const char* data, std::size_t size) const {
    const auto wal = wal_path_for(path_);
    std::FILE* file = fopen_raw(wal.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    char header[kWalHeaderSize];
    pack_wal_header(header, target_offset, size);
    std::fwrite(header, 1, kWalHeaderSize, file);
    if (size > 0 && data != nullptr) {
        std::fwrite(data, 1, size, file);
    }
    apply_durability_(file);
    std::fclose(file);
}

void durable_file::close_file_() noexcept {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void durable_file::prepare_front_clock_() {
    if (!has_first_) {
        first_append_ = std::chrono::steady_clock::now();
        has_first_ = true;
    }
}

void durable_file::append(const char* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }
    auto& front = buf_[static_cast<std::size_t>(front_)];
    front.insert(front.end(), data, data + size);
    ++records_;
    prepare_front_clock_();
}

bool durable_file::should_commit() const {
    const auto& front = buf_[static_cast<std::size_t>(front_)];
    if (front.empty()) {
        return false;
    }
    if (cfg_.max_bytes > 0 && front.size() >= cfg_.max_bytes) {
        return true;
    }
    if (cfg_.max_records > 0 && records_ >= cfg_.max_records) {
        return true;
    }
    if (cfg_.max_age.count() > 0 && has_first_ &&
        std::chrono::steady_clock::now() - first_append_ >= cfg_.max_age) {
        return true;
    }
    return false;
}

bool durable_file::begin_commit(const char*& data, std::size_t& size) {
    auto& front = buf_[static_cast<std::size_t>(front_)];
    if (committing_ || front.empty()) {
        return false;
    }
    frozen_ = front_;
    front_ = 1 - front_;
    buf_[static_cast<std::size_t>(front_)].clear();
    records_ = 0;
    has_first_ = false;
    committing_ = true;
    data = buf_[static_cast<std::size_t>(frozen_)].data();
    size = buf_[static_cast<std::size_t>(frozen_)].size();
    return true;
}

void durable_file::write_commit(const char* data, std::size_t size) {
    if (file_ == nullptr || data == nullptr || size == 0) {
        return;
    }
    std::fflush(file_);
    const auto offset = file_size_of(file_);
    if (cfg_.recover) {
        write_wal_(offset, data, size);
    }
    seek_end(file_);
    std::fwrite(data, 1, size, file_);
    apply_durability_(file_);
    if (cfg_.recover) {
        remove_path(wal_path_for(path_));
    }
}

void durable_file::end_commit() noexcept {
    if (frozen_ >= 0) {
        buf_[static_cast<std::size_t>(frozen_)].clear();
        frozen_ = -1;
    }
    committing_ = false;
}

void durable_file::commit_if_needed() {
    if (!should_commit()) {
        return;
    }
    const char* data = nullptr;
    std::size_t size = 0;
    if (begin_commit(data, size)) {
        write_commit(data, size);
        end_commit();
    }
}

void durable_file::flush() {
    const char* data = nullptr;
    std::size_t size = 0;
    if (begin_commit(data, size)) {
        write_commit(data, size);
        end_commit();
    }
    apply_durability_(file_);
}

void durable_file::emergency_dump() noexcept {
    if (file_ == nullptr) {
        return;
    }
    auto& front = buf_[static_cast<std::size_t>(front_)];
    if (front.empty()) {
        std::fflush(file_);
        return;
    }
    std::fwrite(front.data(), 1, front.size(), file_);
    std::fflush(file_);
    front.clear();
    records_ = 0;
    has_first_ = false;
}

} // namespace details

void dump_buffered_logs() noexcept {
    details::dump_durable_files();
}

namespace {

#if defined(_WIN32)
LPTOP_LEVEL_EXCEPTION_FILTER g_prev_seh = nullptr;
#endif
void (*g_prev_sigsegv)(int) = SIG_DFL;
void (*g_prev_sigabrt)(int) = SIG_DFL;
void (*g_prev_sigill)(int) = SIG_DFL;
void (*g_prev_sigfpe)(int) = SIG_DFL;
#ifndef _WIN32
void (*g_prev_sigbus)(int) = SIG_DFL;
#endif
std::atomic_flag g_crash_dumped = ATOMIC_FLAG_INIT;
std::atomic<bool> g_crash_handler_installed{false};

void crash_dump_once() {
    if (!g_crash_dumped.test_and_set()) {
        dump_buffered_logs();
    }
}

void posix_crash_handler(int sig) {
    crash_dump_once();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

#if defined(_WIN32)
LONG WINAPI seh_crash_handler(EXCEPTION_POINTERS* info) {
    crash_dump_once();
    if (g_prev_seh != nullptr) {
        return g_prev_seh(info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

} // namespace

void install_crash_flush() {
    bool expected = false;
    if (!g_crash_handler_installed.compare_exchange_strong(expected, true)) {
        return;
    }
    g_prev_sigsegv = std::signal(SIGSEGV, posix_crash_handler);
    g_prev_sigabrt = std::signal(SIGABRT, posix_crash_handler);
    g_prev_sigill = std::signal(SIGILL, posix_crash_handler);
    g_prev_sigfpe = std::signal(SIGFPE, posix_crash_handler);
#ifndef _WIN32
    g_prev_sigbus = std::signal(SIGBUS, posix_crash_handler);
#endif
#if defined(_WIN32)
    g_prev_seh = SetUnhandledExceptionFilter(seh_crash_handler);
#endif
    (void)g_prev_sigsegv;
    (void)g_prev_sigabrt;
    (void)g_prev_sigill;
    (void)g_prev_sigfpe;
#ifndef _WIN32
    (void)g_prev_sigbus;
#endif
}

} // namespace minispdlog
