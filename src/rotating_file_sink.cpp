#include "minispdlog/sinks/rotating_file_sink.h"
#include <cstdio>
#include <stdexcept>
#include <sys/stat.h>

namespace minispdlog {
namespace sinks {

    template <typename Mutex>
    rotating_file_sink<Mutex>::rotating_file_sink(const std::string& base_filename, size_t max_size, size_t max_files)
        : base_filename_(base_filename), max_size_(max_size), max_files_(max_files), current_size_(0), file_(nullptr) {
        if (max_size == 0) {
            throw std::invalid_argument("rotating_file_sink: max_size cannot be 0");
        }

        if (max_files == 0) {
            throw std::invalid_argument("rotating_file_sink: max_files cannot be 0");
        }

        /// 以追加模式打开当前文件，保留进程重启前的日志。
        auto filename = calc_filename(base_filename_, 0);
        file_ = fopen(filename.c_str(), "ab");
        if (!file_) {
            throw std::runtime_error("rotating_file_sink: Failed to open file: " + filename);
        }

        /// 从已有文件大小继续计算滚动阈值。
        if (file_exists_(filename)) {
            current_size_ = file_size_(filename);
        }
    }

    template <typename Mutex>
    rotating_file_sink<Mutex>::~rotating_file_sink() {
        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }
    }

    template <typename Mutex>
    std::string rotating_file_sink<Mutex>::filename() const {
        return calc_filename(base_filename_, 0);
    }

    template <typename Mutex>
    std::string rotating_file_sink<Mutex>::calc_filename(const std::string& base_filename, size_t index) {
        if (index == 0) {
            return base_filename;
        }

        /// 将序号插入主文件名与扩展名之间。
        /// 示例："logs/mylog.txt" → "logs/mylog" + ".txt"。
        size_t dot_pos = base_filename.find_last_of('.');
        size_t slash_pos = base_filename.find_last_of("/\\");

        /// 路径目录中的点不应被视为文件扩展名。
        if (dot_pos != std::string::npos && (slash_pos == std::string::npos || dot_pos > slash_pos)) {
            std::string basename = base_filename.substr(0, dot_pos);
            std::string ext = base_filename.substr(dot_pos);
            return basename + "." + std::to_string(index) + ext;
        } else {
            /// 无扩展名时直接追加序号。
            return base_filename + "." + std::to_string(index);
        }
    }

    template <typename Mutex>
    void rotating_file_sink<Mutex>::sink_it_(const details::log_msg& msg) {
        auto& formatted = this->format_message(msg);

        size_t msg_size = formatted.size();

        /// 写入前滚动，避免主动将同一条记录拆到两个文件。
        if (current_size_ + msg_size > max_size_) {
            rotate_();
            current_size_ = 0;
        }

        if (file_) {
            fwrite(formatted.data(), 1, formatted.size(), file_);
            current_size_ += msg_size;
        }
    }

    template <typename Mutex>
    void rotating_file_sink<Mutex>::flush_() {
        if (file_) {
            fflush(file_);
        }
    }

    template <typename Mutex>
    void rotating_file_sink<Mutex>::rotate_() {
        /// 先关闭活动文件，确保 Windows 允许后续重命名。
        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }

        /// 从最大序号向下重命名，防止较新归档被提前覆盖。
        ///    例如: max_files=3 时
        ///      循环 i=3,2,1:
        ///        i=3: mylog.2.txt -> mylog.3.txt (如果 mylog.2.txt 存在)
        ///        i=2: mylog.1.txt -> mylog.2.txt (如果 mylog.1.txt 存在)
        ///        i=1: mylog.txt -> mylog.1.txt (如果 mylog.txt 存在)
        ///
        ///    这样 mylog.3.txt 会被 mylog.2.txt 覆盖(自动删除最旧文件)
        for (size_t i = max_files_; i > 0; --i) {
            std::string src = calc_filename(base_filename_, i - 1);
            if (!file_exists_(src)) {
                continue; ///< 中间归档可能已被外部清理。
            }

            std::string target = calc_filename(base_filename_, i);

            if (!rename_file_(src, target)) {
                /// 重命名失败时截断原文件，避免日志因持续增长占满磁盘。
                file_ = fopen(src.c_str(), "wb");
                if (!file_) {
                    throw std::runtime_error("rotating_file_sink: Failed to reopen file after failed rotation: " + src);
                }
                return;
            }
        }

        /// 新的活动文件从空内容开始。
        std::string current_file = calc_filename(base_filename_, 0);
        file_ = fopen(current_file.c_str(), "wb");
        if (!file_) {
            throw std::runtime_error("rotating_file_sink: Failed to create new file after rotation: " + current_file);
        }
    }

    template <typename Mutex>
    bool rotating_file_sink<Mutex>::rename_file_(const std::string& src, const std::string& target) {
        /// C 运行库在 Windows 上不保证 rename 覆盖已有文件。
        remove_file_(target);

        return std::rename(src.c_str(), target.c_str()) == 0;
    }

    template <typename Mutex>
    bool rotating_file_sink<Mutex>::remove_file_(const std::string& filename) {
        return std::remove(filename.c_str()) == 0;
    }

    template <typename Mutex>
    bool rotating_file_sink<Mutex>::file_exists_(const std::string& filename) {
#ifdef _WIN32
        struct _stat buffer;
        return _stat(filename.c_str(), &buffer) == 0;
#else
        struct stat buffer;
        return stat(filename.c_str(), &buffer) == 0;
#endif
    }

    template <typename Mutex>
    size_t rotating_file_sink<Mutex>::file_size_(const std::string& filename) {
#ifdef _WIN32
        struct _stat buffer;
        if (_stat(filename.c_str(), &buffer) != 0) {
            return 0;
        }
#else
        struct stat buffer;
        if (stat(filename.c_str(), &buffer) != 0) {
            return 0;
        }
#endif
        return static_cast<size_t>(buffer.st_size);
    }

    /// 显式实例化对外提供的单线程与多线程类型。
    template class rotating_file_sink<std::mutex>;
    template class rotating_file_sink<null_mutex>;

} ///< 结束 sinks 命名空间。
} ///< 结束 minispdlog 命名空间。
