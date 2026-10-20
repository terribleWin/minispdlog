#pragma once

#include "base_sink.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

namespace minispdlog {
namespace sinks {

    /**
 * @brief 按本地日历日期切分文件的 Sink。
 * @tparam Mutex `std::mutex` 为多线程版本，`null_mutex` 为单线程版本。
 *
 * 文件名示例：`logs/app.log` 在 2026-09-05 写入 `logs/app.2026-09-05.log`。
 * 每日切分点由小时和分钟指定，默认本地时间 00:00。切分判断使用
 * `log_msg::time`，便于测试注入确定时间。`max_files == 0` 表示不清理历史文件。
 */
    template <typename Mutex>
    class daily_file_sink : public base_sink<Mutex> {
    public:
        /**
     * @brief 创建按天文件 Sink。
     * @throws std::invalid_argument 小时不在 0..23 或分钟不在 0..59 时抛出。
     */
        daily_file_sink(std::string base_filename, int rotation_hour = 0, int rotation_minute = 0,
                        bool truncate = false, std::size_t max_files = 0);

        ~daily_file_sink() override;

        daily_file_sink(const daily_file_sink&) = delete;
        daily_file_sink& operator=(const daily_file_sink&) = delete;

        /** @brief 返回当前日期对应的实际文件名。 */
        [[nodiscard]] std::string filename() const;

        /** @brief 按 `tm` 日期计算带 `YYYY-MM-DD` 后缀的文件名。 */
        static std::string calc_filename(const std::string& base_filename, const std::tm& date);
        /** @brief 按年月日计算带日期后缀的文件名。 */
        static std::string calc_filename(const std::string& base_filename, int year, int month, int day);

    protected:
        void sink_it_(const details::log_msg& msg) override;
        void flush_() override;

    private:
        struct file_closer {
            void operator()(std::FILE* file) const noexcept {
                if (file != nullptr) {
                    std::fclose(file);
                }
            }
        };

        void open_file_(const std::string& filename, bool truncate);
        void delete_old_(const std::tm& current_tm);
        [[nodiscard]] log_clock::time_point next_rotation_tp_(log_clock::time_point from) const;

        std::string base_filename_;
        int rotation_hour_;
        int rotation_minute_;
        bool truncate_;
        std::size_t max_files_;
        log_clock::time_point rotation_tp_{};
        std::string current_filename_;
        std::unique_ptr<std::FILE, file_closer> file_;
    };

    using daily_file_sink_mt = daily_file_sink<std::mutex>;
    using daily_file_sink_st = daily_file_sink<null_mutex>;

} ///< 结束 sinks 命名空间。
} ///< 结束 minispdlog 命名空间。
