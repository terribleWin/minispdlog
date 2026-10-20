#pragma once

#include "common.h"

#include <chrono>
#include <cstddef>

namespace minispdlog {

/** @brief 批次提交后要求达到的持久化层级。 */
enum class durability {
    ///< 仅调用 `fwrite`；数据仍可能停留在 C 运行库缓冲区中。
    none,
    ///< 每批调用 `fflush`；内核接收数据后可抵御普通进程崩溃。
    fflush,
    ///< 调用 `fflush` 后再执行 `fsync`/`FlushFileBuffers`，开销最高。
    fsync
};

/**
 * @brief 批量文件 Sink 的提交与恢复参数。
 *
 * `max_bytes`、`max_records` 或 `max_age` 为零时，分别关闭对应触发条件。
 * `flush()`、Sink 析构以及 `dump_buffered_logs()` 仍会处理前台缓冲区。
 */
struct batch_config {
    std::size_t max_bytes = 64 * 1024;      ///< 单批最大字节数；零表示不按字节触发。
    std::size_t max_records = 256;          ///< 单批最大记录数；零表示不按条数触发。
    std::chrono::milliseconds max_age{50};  ///< 前台批次最大驻留时间；零表示关闭。
    durability commit = durability::fflush; ///< 批次落盘时采用的持久化层级。
    bool recover = true;                    ///< 打开文件时是否回放 WAL 并修复断尾。

    /**
     * @brief 创建仅由显式刷新或析构触发提交的配置。
     * @return 关闭字节数、记录数和时间阈值后的配置。
     */
    static batch_config until_flush() {
        batch_config cfg;
        cfg.max_bytes = 0;
        cfg.max_records = 0;
        cfg.max_age = std::chrono::milliseconds{0};
        return cfg;
    }
};

} ///< 结束 minispdlog 命名空间。
