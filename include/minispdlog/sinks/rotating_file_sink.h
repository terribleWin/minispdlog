#pragma once

#include "../common.h"
#include "base_sink.h"
#include "file_sink.h"
#include <string>
#include <cstdio>

namespace minispdlog {
namespace sinks {

// rotating_file_sink: 按大小滚动的文件 Sink
// 参考 spdlog 设计:当文件大小超过限制时自动轮转
//
// 轮转策略:
//   - mylog.txt 达到 max_size 后轮转
//   - mylog.txt → mylog.1.txt (重命名)
//   - mylog.1.txt → mylog.2.txt (如果存在)
//   - 创建新的 mylog.txt
//   - 当达到 max_files 限制时,删除最旧的文件
template<typename Mutex>
class rotating_file_sink : public base_sink<Mutex> {
public:
    // 构造函数
    // base_filename: 基础文件名,如 "logs/mylog.txt"
    // max_size: 单个文件最大字节数
    // max_files: 最多保留的文件数量(不包括当前文件)
    rotating_file_sink(
        const std::string& base_filename,
        size_t max_size,
        size_t max_files
    );
    
    ~rotating_file_sink() override = default;
    
    // 获取当前文件名
    std::string filename() const;
    
    // 根据索引计算文件名
    // calc_filename("logs/mylog.txt", 0) => "logs/mylog.txt"
    // calc_filename("logs/mylog.txt", 1) => "logs/mylog.1.txt"
    // calc_filename("logs/mylog.txt", 3) => "logs/mylog.3.txt"
    static std::string calc_filename(const std::string& base_filename, size_t index);
    
protected:
    void sink_it_(const details::log_msg& msg) override;
    void flush_() override;
    
private:
    // 执行文件轮转
    void rotate_();
    
    // 文件重命名(返回是否成功)
    bool rename_file_(const std::string& src, const std::string& target);
    
    // 删除文件(返回是否成功)
    bool remove_file_(const std::string& filename);
    
    // 检查文件是否存在
    bool file_exists_(const std::string& filename);
    
    // 获取文件大小
    size_t file_size_(const std::string& filename);
    
    std::string base_filename_;    // 基础文件名
    size_t max_size_;              // 单文件最大字节数
    size_t max_files_;             // 最多保留文件数
    size_t current_size_;          // 当前文件大小
    FILE* file_;                    // 文件句柄
};

// 类型别名
using rotating_file_sink_mt = rotating_file_sink<std::mutex>;
using rotating_file_sink_st = rotating_file_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog