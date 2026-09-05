#pragma once

/**
 * Public entry for lock-free queue primitives.
 *
 * - spsc_queue: 1 producer, 1 consumer
 * - mpsc_queue: N producers, 1 consumer (used by async lockfree backend)
 *
 * Prefer the high-level API in async.h (`init_lockfree_thread_pool`) for logging.
 * Use these types directly only when embedding custom pipelines.
 */

#include "details/spsc_queue.h"
#include "details/mpsc_queue.h"
#include "details/queue_utils.h"

namespace minispdlog {

using details::QueueItem;
using details::spsc_queue;
using details::mpsc_queue;
using details::next_power_of_two;

}  // namespace minispdlog
