#include "minispdlog/sinks/rotating_file_sink.h"
#include <cstdio>
#include <sys/stat.h>
#include <stdexcept>

namespace minispdlog {
namespace sinks {

template<typename Mutex>
rotating_file_sink<Mutex>::rotating_file_sink(
    const std::string& base_filename,
    size_t max_size,
    size_t max_files
)
    : base_filename_(base_filename)
    , max_size_(max_size)
    , max_files_(max_files)
    , current_size_(0)
    , file_(nullptr)
{
    if (max_size == 0) {
        throw std::invalid_argument("rotating_file_sink: max_size cannot be 0");
    }
    
    if (max_files == 0) {
        throw std::invalid_argument("rotating_file_sink: max_files cannot be 0");
    }
    
    // 打开文件(以追加模式)
    auto filename = calc_filename(base_filename_, 0);
    file_ = fopen(filename.c_str(), "ab");
    if (!file_) {
        throw std::runtime_error("rotating_file_sink: Failed to open file: " + filename);
    }
    
    // 获取当前文件大小
    if (file_exists_(filename)) {
        current_size_ = file_size_(filename);
    }
}

template<typename Mutex>
std::string rotating_file_sink<Mutex>::filename() const {
    return calc_filename(base_filename_, 0);
}

template<typename Mutex>
std::string rotating_file_sink<Mutex>::calc_filename(const std::string& base_filename, size_t index) {
    if (index == 0) {
        return base_filename;
    }
    
    // 分离文件名和扩展名
    // "logs/mylog.txt" → "logs/mylog" + ".txt"
    size_t dot_pos = base_filename.find_last_of('.');
    size_t slash_pos = base_filename.find_last_of("/\\");
    
    // 确保 '.' 在文件名部分,不在路径部分
    if (dot_pos != std::string::npos && 
        (slash_pos == std::string::npos || dot_pos > slash_pos)) {
        std::string basename = base_filename.substr(0, dot_pos);
        std::string ext = base_filename.substr(dot_pos);
        return basename + "." + std::to_string(index) + ext;
    } else {
        // 没有扩展名
        return base_filename + "." + std::to_string(index);
    }
}

template<typename Mutex>
void rotating_file_sink<Mutex>::sink_it_(const details::log_msg& msg) {
    // 格式化消息
    fmt::memory_buffer formatted;
    this->format_message(msg, formatted);
    
    size_t msg_size = formatted.size();
    
    // 检查是否需要轮转
    if (current_size_ + msg_size > max_size_) {
        rotate_();
        current_size_ = 0;
    }
    
    // 写入文件
    if (file_) {
        fwrite(formatted.data(), 1, formatted.size(), file_);
        current_size_ += msg_size;
    }
}

template<typename Mutex>
void rotating_file_sink<Mutex>::flush_() {
    if (file_) {
        fflush(file_);
    }
}

template<typename Mutex>
void rotating_file_sink<Mutex>::rotate_() {
    // 1. 关闭当前文件
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
    
    // 2. 轮转算法 (参考 spdlog 实现)
    //    从 max_files 开始向下重命名
    //    例如: max_files=3 时
    //      循环 i=3,2,1:
    //        i=3: mylog.2.txt -> mylog.3.txt (如果 mylog.2.txt 存在)
    //        i=2: mylog.1.txt -> mylog.2.txt (如果 mylog.1.txt 存在)
    //        i=1: mylog.txt -> mylog.1.txt (如果 mylog.txt 存在)
    //
    //    这样 mylog.3.txt 会被 mylog.2.txt 覆盖(自动删除最旧文件)
    for (size_t i = max_files_ ; i > 0; --i) {
        std::string src = calc_filename(base_filename_, i - 1);
        if (!file_exists_(src)) {
            continue;  // 源文件不存在,跳过
        }
        
        std::string target = calc_filename(base_filename_, i);
        
        // 重命名
        if (!rename_file_(src, target)) {
            // 重命名失败,尝试重新打开原文件并截断
            // (spdlog 的做法:防止文件无限增长)
            file_ = fopen(src.c_str(), "wb");
            if (!file_) {
                throw std::runtime_error("rotating_file_sink: Failed to reopen file after failed rotation: " + src);
            }
            return;
        }
    }
    
    // 3. 创建新的当前文件
    std::string current_file = calc_filename(base_filename_, 0);
    file_ = fopen(current_file.c_str(), "wb");
    if (!file_) {
        throw std::runtime_error("rotating_file_sink: Failed to create new file after rotation: " + current_file);
    }
}

template<typename Mutex>
bool rotating_file_sink<Mutex>::rename_file_(const std::string& src, const std::string& target) {
    // 先删除目标文件(如果存在)
    remove_file_(target);
    
    // 重命名
    return std::rename(src.c_str(), target.c_str()) == 0;
}

template<typename Mutex>
bool rotating_file_sink<Mutex>::remove_file_(const std::string& filename) {
    return std::remove(filename.c_str()) == 0;
}

template<typename Mutex>
bool rotating_file_sink<Mutex>::file_exists_(const std::string& filename) {
#ifdef _WIN32
    struct _stat buffer;
    return _stat(filename.c_str(), &buffer) == 0;
#else
    struct stat buffer;
    return stat(filename.c_str(), &buffer) == 0;
#endif
}

template<typename Mutex>
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

// 显式实例化模板
template class rotating_file_sink<std::mutex>;
template class rotating_file_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog