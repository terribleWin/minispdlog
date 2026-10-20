#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace minispdlog {
namespace details {

    /**
 * @brief 固定容量、满时覆盖最旧元素的循环队列。
 * @tparam T 可移动且可默认构造的元素类型。
 *
 * 该类型本身不提供同步；`mpmc_blocking_queue` 在外层用互斥锁保护它。
 * 内部额外分配一个槽位，用头尾相等表示空队列。
 */
    template <typename T>
    class circular_q {
    public:
        using value_type = T;

        /** @brief 创建最多保存 `max_items` 个元素的队列。 */
        explicit circular_q(std::size_t max_items)
            : max_items_(max_items + 1), v_(max_items_), head_(0), tail_(0), overrun_counter_(0) {}

        circular_q(const circular_q&) = delete;
        circular_q& operator=(const circular_q&) = delete;

        /**
     * @brief 在队尾插入元素。
     * @note 队列已满时覆盖最旧元素，并增加覆盖计数。
     */
        void push_back(T&& item) {
            v_[tail_] = std::move(item);
            tail_ = next_index_(tail_);
            if (tail_ == head_) {
                head_ = next_index_(head_);
                ++overrun_counter_;
            }
        }

        /** @brief 返回队首元素；调用方必须保证队列非空。 */
        const T& front() const { return v_[head_]; }

        /** @overload */
        T& front() { return v_[head_]; }

        /** @brief 移除队首元素；调用方必须保证队列非空。 */
        void pop_front() { head_ = next_index_(head_); }

        /** @brief 判断队列是否为空。 */
        [[nodiscard]] bool empty() const { return head_ == tail_; }

        /** @brief 判断队列是否已达到可用容量。 */
        [[nodiscard]] bool full() const { return next_index_(tail_) == head_; }

        /** @brief 返回当前元素数量。 */
        [[nodiscard]] std::size_t size() const {
            if (tail_ >= head_) {
                return tail_ - head_;
            }
            return max_items_ - (head_ - tail_);
        }

        /** @brief 返回可用容量。 */
        [[nodiscard]] std::size_t capacity() const { return max_items_ - 1; }

        /** @brief 返回累计覆盖的旧元素数量。 */
        [[nodiscard]] std::size_t overrun_count() const { return overrun_counter_; }

    private:
        [[nodiscard]] std::size_t next_index_(std::size_t i) const noexcept {
            ++i;
            return i == max_items_ ? 0 : i;
        }

        std::size_t max_items_;
        std::vector<T> v_;
        std::size_t head_;
        std::size_t tail_;
        std::size_t overrun_counter_;
    };

} ///< 结束 details 命名空间。
} ///< 结束 minispdlog 命名空间。
