#pragma once

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace minispdlog::tests {

/**
 * @brief 测试夹具：提供临时目录和自动清理
 *
 * 解决测试文件系统操作（如 file_sink、rotating_file_sink）时的目录管理问题。
 * 每个测试用例可以创建独立的临时目录，测试结束后自动删除，避免污染。
 *
 * 使用示例：
 *   TEST_CASE("file sink writes to disk") {
 *       test_fixture fx;
 *       auto path = fx.temp_path("test.log");
 *       auto sink = std::make_shared<file_sink_mt>(path);
 *       // ... 测试 ...
 *       REQUIRE(std::filesystem::exists(path));
 *   } // 析构时自动删除 temp_dir_ 下的所有内容
 */
class test_fixture {
public:
    test_fixture() {
        // 生成唯一临时目录名：minispdlog_test_XXXXXX
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 999999);
        temp_dir_ = std::filesystem::temp_directory_path()
                    / ("minispdlog_test_" + std::to_string(dis(gen)));
        std::filesystem::create_directories(temp_dir_);
    }

    ~test_fixture() {
        // error_code: destructor is implicitly noexcept; a throwing
        // remove_all (e.g. file still open) would std::terminate.
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    // 禁止拷贝（目录唯一）
    test_fixture(const test_fixture&) = delete;
    test_fixture& operator=(const test_fixture&) = delete;

    // 允许移动（测试夹具通常不需要，但遵循 RAII 最佳实践）
    test_fixture(test_fixture&&) = default;
    test_fixture& operator=(test_fixture&&) = default;

    // 获取临时目录路径
    std::filesystem::path temp_dir() const {
        return temp_dir_;
    }

    // 在临时目录下生成一个文件路径
    std::filesystem::path temp_path(const std::string& filename) const {
        return temp_dir_ / filename;
    }

private:
    std::filesystem::path temp_dir_;
};

/**
 * Redirect a C stdio stream (stdout/stderr) to a file for the object's lifetime.
 * Needed because console sinks write with fwrite, not iostream rdbuf.
 */
class stdio_redirect {
public:
    stdio_redirect(std::FILE* stream, std::filesystem::path path)
        : stream_(stream), path_(std::move(path)) {
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

    const std::filesystem::path& path() const { return path_; }

private:
    std::FILE* stream_{nullptr};
    std::filesystem::path path_;
    std::FILE* file_{nullptr};
    int saved_fd_{-1};
};

/**
 * @brief 等待文件系统落盘（用于测试文件 sink 的异步写入）
 *
 * 在某些平台上（尤其 Windows），文件写入有缓存，调用 flush 后
 * 仍然需要短暂等待才能读取到完整内容。
 */
inline void wait_for_filesystem() {
#ifdef _WIN32
    // Windows 文件系统缓存刷新较慢
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
}

} // namespace minispdlog::tests
