#pragma once

#include "../batch_config.h"
#include "../common.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace minispdlog {
namespace details {

    /** @internal @brief 返回日志文件对应的 WAL 旁路文件路径。 */
    MINISPDLOG_API std::string wal_path_for(const std::string& log_path);

    /**
 * @internal
 * @brief 按正式磁盘布局写入 WAL 旁路文件。
 * @note 主要用于恢复流程以及模拟崩溃的测试。
 */
    MINISPDLOG_API void write_wal_sidecar(const std::string& log_path, std::uint64_t target_offset,
                                          std::string_view payload);

    /**
 * @internal
 * @brief 围绕单个 `FILE*` 实现双缓冲、批量提交、WAL 回放与断尾修复。
 *
 * 前台缓冲区收集完整日志行，达到阈值或收到 `flush()` 时与冻结缓冲区交换，
 * 再通过一次 `fwrite` 提交。该类型自身不是线程安全的：Sink 必须串行化
 * `append()`、`begin_commit()` 与 `end_commit()`；冻结后可在 Sink 锁外调用
 * `write_commit()`，让生产者同时填充新的前台缓冲区。
 */
    class MINISPDLOG_API durable_file {
    public:
        durable_file(std::string path, bool truncate, batch_config cfg);
        ~durable_file();

        durable_file(const durable_file&) = delete;
        durable_file& operator=(const durable_file&) = delete;

        /** @brief 将一条已格式化记录追加到前台缓冲区。 */
        void append(const char* data, std::size_t size);
        /** @brief 判断任一批次阈值是否已达到。 */
        bool should_commit() const;
        /** @brief 判断当前是否存在冻结批次正在提交。 */
        bool committing() const noexcept { return committing_; }

        /**
     * @brief 将前台缓冲区冻结并返回只读视图。
     * @return 有数据可提交时返回 true；`data`/`size` 在 `end_commit()` 前有效。
     */
        bool begin_commit(const char*& data, std::size_t& size);
        /** @brief 将冻结批次写入 WAL 和主日志文件。 */
        void write_commit(const char* data, std::size_t size);
        /** @brief 释放冻结缓冲区并结束本次提交。 */
        void end_commit() noexcept;

        /** @brief 达到阈值时提交前台缓冲区；调用方必须已串行化访问。 */
        void commit_if_needed();
        /** @brief 提交剩余数据，并按配置对主文件执行持久化。 */
        void flush();

        /**
     * @brief 尽力直接写出前台缓冲区，供测试与崩溃处理器使用。
     * @warning 该函数不满足异步信号安全保证。
     */
        void emergency_dump() noexcept;

        /** @brief 返回主日志文件路径。 */
        const std::string& path() const noexcept { return path_; }
        /** @brief 返回当前前台缓冲区字节数。 */
        std::size_t front_size() const noexcept { return buf_[static_cast<std::size_t>(front_)].size(); }

    private:
        void open_file_(bool truncate);
        void recover_on_open_();
        void salvage_torn_tail_();
        void apply_durability_(std::FILE* file) const;
        void write_wal_(std::uint64_t target_offset, const char* data, std::size_t size) const;
        void replay_wal_();
        void close_file_() noexcept;
        void prepare_front_clock_();

        std::string path_;
        batch_config cfg_{};
        std::FILE* file_{nullptr};
        std::vector<char> buf_[2]{};
        int front_{0};
        int frozen_{-1};
        std::size_t records_{0};
        bool has_first_{false};
        std::chrono::steady_clock::time_point first_append_{};
        bool committing_{false};
    };

    MINISPDLOG_API void register_durable_file(durable_file* file);
    MINISPDLOG_API void unregister_durable_file(durable_file* file);
    MINISPDLOG_API void dump_durable_files() noexcept;

} ///< 结束 details 命名空间。

/**
 * @brief 尽力将所有已注册耐久文件的前台缓冲区写入磁盘。
 * @note 主要供测试和崩溃处理器使用，不替代正常的 `flush()`/`shutdown()`。
 */
MINISPDLOG_API void dump_buffered_logs() noexcept;

/**
 * @brief 安装进程级致命故障处理器，并在故障时尝试写出前台缓冲区。
 *
 * Windows 使用未处理 SEH，POSIX 处理 SIGSEGV、SIGABRT、SIGILL、SIGFPE、SIGBUS。
 * 可重复调用，但它只能尽力恢复内存中的已格式化数据，不保证信号安全或应对强制终止。
 */
MINISPDLOG_API void install_crash_flush();

} ///< 结束 minispdlog 命名空间。
