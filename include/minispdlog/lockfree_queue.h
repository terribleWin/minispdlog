#pragma once

/**
 * @file lockfree_queue.h
 * @brief 无锁队列原语的公共入口。
 *
 * `spsc_queue` 适用于单生产者/单消费者，`mpsc_queue` 适用于多生产者/
 * 单消费者，也是异步无锁后端使用的队列。日志业务优先使用 `async.h` 中的
 * `init_lockfree_thread_pool()`；仅在嵌入自定义流水线时直接操作这些类型。
 */

#include "details/mpsc_queue.h"
#include "details/queue_utils.h"
#include "details/spsc_queue.h"

namespace minispdlog {

using details::mpsc_queue;
using details::next_power_of_two;
using details::QueueItem;
using details::spsc_queue;

} // minispdlog
