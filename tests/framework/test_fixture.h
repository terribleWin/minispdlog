#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace minispdlog::tests {

/**
 * @brief 为每个文件系统用例创建独立的临时目录。
 *
 * 析构时使用非抛异常的删除方式回收目录，不会因文件句柄暂时未释放
 * 而终止整个测试进程。
 */
class test_fixture {
public:
    test_fixture() {
        static std::atomic<std::uint64_t> sequence{0};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
        const auto process_id = static_cast<std::uint64_t>(::_getpid());
#else
        const auto process_id = static_cast<std::uint64_t>(::getpid());
#endif
#ifdef MINISPDLOG_TEST_TMP_DIR
        const std::filesystem::path root{MINISPDLOG_TEST_TMP_DIR};
#else
        const auto root = std::filesystem::current_path() / "minispdlog-test-tmp";
#endif
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        if (ec) {
            throw std::runtime_error("test_fixture: failed to create temp root: " + ec.message());
        }

        temp_dir_ =
            root / ("case_" + std::to_string(process_id) + "_" + std::to_string(tick) + "_" + std::to_string(id));
        if (!std::filesystem::create_directory(temp_dir_, ec) || ec) {
            throw std::runtime_error("test_fixture: failed to create unique temp directory: " + ec.message());
        }
    }

    ~test_fixture() {
        /// 析构函数不应因 Windows 上短暂未释放的文件句柄而抛出异常。
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    /** @brief 临时目录具有唯一所有权，因此不允许拷贝。 */
    test_fixture(const test_fixture&) = delete;
    test_fixture& operator=(const test_fixture&) = delete;

    test_fixture(test_fixture&&) = delete;
    test_fixture& operator=(test_fixture&&) = delete;

    /** @brief 返回当前用例的临时目录。 */
    [[nodiscard]] std::filesystem::path temp_dir() const { return temp_dir_; }

    /** @brief 在当前临时目录下组合文件路径。 */
    [[nodiscard]] std::filesystem::path temp_path(const std::string& filename) const { return temp_dir_ / filename; }

private:
    std::filesystem::path temp_dir_;
};

/**
 * @brief 在对象生命期内将 C 标准 I/O 流重定向到文件。
 * @note 控制台 Sink 使用 `fwrite`，仅替换 iostream 缓冲区无法捕获其输出。
 */
class stdio_redirect {
public:
    stdio_redirect(std::FILE* stream, std::filesystem::path path) : stream_(stream), path_(std::move(path)) {
        std::fflush(stream_);
#ifdef _WIN32
        saved_fd_ = _dup(_fileno(stream_));
        file_ = _fsopen(path_.string().c_str(), "wb+", _SH_DENYNO);
        if (file_ == nullptr) {
            throw std::runtime_error("stdio_redirect: failed to open " + path_.string());
        }
        _dup2(_fileno(file_), _fileno(stream_));
#else
        saved_fd_ = ::dup(::fileno(stream_));
        file_ = std::fopen(path_.string().c_str(), "wb+");
        if (file_ == nullptr) {
            throw std::runtime_error("stdio_redirect: failed to open " + path_.string());
        }
        ::dup2(::fileno(file_), ::fileno(stream_));
#endif
    }

    ~stdio_redirect() {
        if (stream_ != nullptr) {
            std::fflush(stream_);
        }
        if (saved_fd_ >= 0) {
#ifdef _WIN32
            _dup2(saved_fd_, _fileno(stream_));
            _close(saved_fd_);
#else
            ::dup2(saved_fd_, ::fileno(stream_));
            ::close(saved_fd_);
#endif
        }
        if (file_ != nullptr) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    stdio_redirect(const stdio_redirect&) = delete;
    stdio_redirect& operator=(const stdio_redirect&) = delete;

    /** @brief 返回重定向的目标路径。 */
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::FILE* stream_{nullptr};
    std::filesystem::path path_;
    std::FILE* file_{nullptr};
    int saved_fd_{-1};
};

/**
 * @brief 为测试中的文件系统可见性留出少量时间。
 *
 * 在某些平台上（尤其 Windows），文件写入有缓存，调用 flush 后
 * 仍然需要短暂等待才能读取到完整内容。
 */
inline void wait_for_filesystem() {
#ifdef _WIN32
    /// Windows 文件系统缓存的可见性通常稍慢。
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
}

} ///< 结束 minispdlog::tests 命名空间。
