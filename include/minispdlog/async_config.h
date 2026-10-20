#pragma once

#include <cstddef>
#include <cstdint>

namespace minispdlog {

/// 异步线程池可选的队列后端。
enum class async_queue_type : std::uint8_t {
    blocking, ///< 基于互斥锁和条件变量的 MPMC 队列，支持多个工作线程。
    lockfree  ///< 无锁 MPSC 环形队列，固定使用一个工作线程。
};

/// 异步队列已满时的处理策略。
enum class async_overflow_policy : std::uint8_t {
    block,          ///< 等待可用空间；无锁后端会让出当前线程后重试。
    overrun_oldest, ///< 覆盖最旧记录；仅阻塞后端支持，无锁后端会抛出异常。
    discard_new     ///< 丢弃新记录并累计丢弃计数。
};

/// 异步线程池初始化选项。
struct thread_pool_options {
    /// 队列的精确容量。
    std::size_t queue_size = 8192;
    /// 阻塞后端的工作线程数；无锁后端固定为一个。
    std::size_t thread_count = 1;
    /// 使用的异步队列后端。
    async_queue_type queue_type = async_queue_type::blocking;
    /// 累积到该日志数量后唤醒休眠线程；刷新和终止消息始终立即唤醒。
    std::size_t wake_batch = 64;
    /// 未主动唤醒时的最长休眠微秒数，用于及时处理突发流量的尾部记录。
    std::uint32_t wake_interval_us = 100;
};

} // minispdlog
