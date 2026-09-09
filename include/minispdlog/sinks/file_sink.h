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

inline std::FILE* open_log_file(const char* filename, const char* mode) {
#ifdef _WIN32
    return _fsopen(filename, mode, _SH_DENYNO);
#else
    return std::fopen(filename, mode);
#endif
}

template <typename Mutex>
class file_sink : public base_sink<Mutex> {
public:
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
        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);
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

} // namespace sinks
} // namespace minispdlog
