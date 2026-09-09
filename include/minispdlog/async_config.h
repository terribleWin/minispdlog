#pragma once

#include <cstddef>
#include <cstdint>

namespace minispdlog {

/// Async queue backend exposed to users.
enum class async_queue_type : std::uint8_t {
    blocking,  ///< mutex + CV MPMC (`mpmc_blocking_queue`), multi-worker OK
    lockfree   ///< lock-free MPSC ring, exactly one worker thread
};

/// Behavior when the async queue is full.
enum class async_overflow_policy : std::uint8_t {
    block,            ///< wait until space is available (spin/yield on lockfree)
    overrun_oldest,   ///< overwrite oldest; blocking MPMC only (lockfree throws)
    discard_new       ///< drop the new message and count it
};

/// Options for `init_thread_pool`.
struct thread_pool_options {
    std::size_t queue_size = 8192;
    std::size_t thread_count = 1;
    async_queue_type queue_type = async_queue_type::blocking;
    /// Wake a sleeping worker after this many log records (flush/terminate always wake).
    std::size_t wake_batch = 64;
    /// Or after this many microseconds with no wake (covers the tail of a burst).
    std::uint32_t wake_interval_us = 100;
};

}  // namespace minispdlog
