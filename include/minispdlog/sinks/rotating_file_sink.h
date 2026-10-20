#pragma once

#include "../common.h"
#include "base_sink.h"
#include "file_sink.h"

#include <cstddef>
#include <cstdio>
#include <string>

namespace minispdlog {
namespace sinks {

    /**
 * @brief 按文件大小滚动并限制归档数量的 Sink。
 * @tparam Mutex `std::mutex` 为多线程版本，`null_mutex` 为单线程版本。
 *
 * 当前文件达到上限时，`app.log` 依次滚动为 `app.1.log`、`app.2.log` 等，
 * 超过 `max_files` 的最旧归档会被删除。写入前预判完整记录大小，避免主动把
 * 一条日志拆到两个文件中。
 */
    template <typename Mutex>
    class rotating_file_sink : public base_sink<Mutex> {
    public:
        /**
     * @brief 创建按大小滚动的文件 Sink。
     * @param base_filename 当前文件路径。
     * @param max_size 单个文件最大字节数。
     * @param max_files 最多保留的归档数量，不包括当前文件。
     * @throws std::invalid_argument `max_size` 或 `max_files` 为零时抛出。
     */
        rotating_file_sink(const std::string& base_filename, std::size_t max_size, std::size_t max_files);

        ~rotating_file_sink() override;

        /** @brief 返回当前活动文件名。 */
        std::string filename() const;

        /**
     * @brief 计算指定归档索引的文件名。
     * @note 索引零返回原文件名，例如索引 1 将 `app.log` 变为 `app.1.log`。
     */
        static std::string calc_filename(const std::string& base_filename, std::size_t index);

    protected:
        void sink_it_(const details::log_msg& msg) override;
        void flush_() override;

    private:
        void rotate_();
        bool rename_file_(const std::string& src, const std::string& target);
        bool remove_file_(const std::string& filename);
        bool file_exists_(const std::string& filename);
        std::size_t file_size_(const std::string& filename);

        std::string base_filename_;
        std::size_t max_size_;
        std::size_t max_files_;
        std::size_t current_size_;
        std::FILE* file_;
    };

    using rotating_file_sink_mt = rotating_file_sink<std::mutex>;
    using rotating_file_sink_st = rotating_file_sink<null_mutex>;

} // sinks
} // minispdlog
