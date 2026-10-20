#pragma once

#include "base_sink.h"

#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <share.h>
#endif

namespace minispdlog {
namespace sinks {

    /** @internal @brief 以跨平台共享语义打开日志文件。 */
    inline std::FILE* open_log_file(const char* filename, const char* mode) {
#ifdef _WIN32
        return _fsopen(filename, mode, _SH_DENYNO);
#else
        return std::fopen(filename, mode);
#endif
    }

    /**
 * @brief 每条记录直接 `fwrite` 到 `FILE*` 的基础文件 Sink。
 * @tparam Mutex `std::mutex` 为多线程版本，`null_mutex` 为单线程版本。
 * @note `log()` 不隐式调用 `fflush`；需要持久化可显式调用 logger 的 `flush()`。
 */
    template <typename Mutex>
    class file_sink : public base_sink<Mutex> {
    public:
        /**
     * @brief 打开日志文件。
     * @param filename 文件路径。
     * @param truncate 为 true 时截断已有文件，否则追加。
     * @throws std::runtime_error 文件无法打开时抛出。
     */
        explicit file_sink(const std::string& filename, bool truncate = false) {
            const char* mode = truncate ? "wb" : "ab";
            file_ = open_log_file(filename.c_str(), mode);
            if (file_ == nullptr) {
                throw std::runtime_error("Failed to open file: " + filename);
            }
        }

        ~file_sink() override {
            if (file_ != nullptr) {
                std::fclose(file_);
                file_ = nullptr;
            }
        }

        file_sink(const file_sink&) = delete;
        file_sink& operator=(const file_sink&) = delete;

    protected:
        void sink_it_(const details::log_msg& msg) override {
            auto& formatted = this->format_message(msg);
            if (file_ != nullptr) {
                std::fwrite(formatted.data(), 1, formatted.size(), file_);
            }
        }

        void flush_() override {
            if (file_ != nullptr) {
                std::fflush(file_);
            }
        }

    private:
        std::FILE* file_{nullptr};
    };

    using file_sink_mt = file_sink<std::mutex>;
    using file_sink_st = file_sink<null_mutex>;

} // sinks
} // minispdlog
