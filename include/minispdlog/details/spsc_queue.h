#pragma once

#include "queue_utils.h"

#include <atomic>
#include <cstddef>
#include <utility>
#include <vector>

namespace minispdlog {
namespace details {

    /**
 * @brief 有界单生产者/单消费者无锁环形队列。
 * @tparam T 满足 QueueItem 的元素类型。
 *
 * 构造容量向上取整为 2 的幂，因此槽位定位使用位掩码而非热路径整数除法。
 * 生产者只能调用 `push()`，消费者只能调用 `pop()`。
 */
    template <QueueItem T>
    class spsc_queue {
    public:
        /** @brief 创建容量不小于请求值的队列，最小容量为 2。 */
        explicit spsc_queue(std::size_t capacity)
            : capacity_(next_power_of_two(capacity)), mask_(capacity_ - 1), buffer_(capacity_), write_idx_(0),
              read_idx_(0) {}

        /** @brief 复制入队；队列已满时返回 false。仅生产者线程调用。 */
        bool push(const T& item) {
            const auto write_pos = write_idx_.load(std::memory_order_relaxed);
            const auto read_pos = read_idx_.load(std::memory_order_acquire);

            if (write_pos - read_pos >= capacity_) {
                return false;
            }

            buffer_[write_pos & mask_] = item;
            write_idx_.store(write_pos + 1, std::memory_order_release);
            return true;
        }

        /** @brief 移动入队；队列已满时返回 false。仅生产者线程调用。 */
        bool push(T&& item) {
            const auto write_pos = write_idx_.load(std::memory_order_relaxed);
            const auto read_pos = read_idx_.load(std::memory_order_acquire);

            if (write_pos - read_pos >= capacity_) {
                return false;
            }

            buffer_[write_pos & mask_] = std::move(item);
            write_idx_.store(write_pos + 1, std::memory_order_release);
            return true;
        }

        /** @brief 移动弹出队首元素；队列为空时返回 false。仅消费者线程调用。 */
        bool pop(T& item) {
            const auto read_pos = read_idx_.load(std::memory_order_relaxed);
            const auto write_pos = write_idx_.load(std::memory_order_acquire);

            if (read_pos == write_pos) {
                return false;
            }

            item = std::move(buffer_[read_pos & mask_]);
            read_idx_.store(read_pos + 1, std::memory_order_release);
            return true;
        }

        /** @brief 返回调用瞬间观察到的元素数量。 */
        [[nodiscard]] std::size_t size() const {
            const auto w = write_idx_.load(std::memory_order_acquire);
            const auto r = read_idx_.load(std::memory_order_acquire);
            return w - r;
        }

        /** @brief 判断调用瞬间队列是否为空。 */
        [[nodiscard]] bool empty() const { return size() == 0; }

        /** @brief 返回取整后的实际容量。 */
        [[nodiscard]] std::size_t capacity() const { return capacity_; }

    private:
        std::size_t capacity_;
        std::size_t mask_;
        std::vector<T> buffer_;
        alignas(kCacheLineSize) std::atomic<std::size_t> write_idx_;
        alignas(kCacheLineSize) std::atomic<std::size_t> read_idx_;
    };

} // details
} // minispdlog
