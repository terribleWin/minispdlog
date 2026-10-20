#pragma once

#include "circular_q.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

namespace minispdlog {
namespace details {

    /**
 * @brief 有界多生产者、多消费者阻塞队列。
 *
 * enqueue 在队列满时等待空间，try_enqueue 在队列满时拒绝新元素，
 * enqueue_nowait 则覆盖最旧元素。所有容量与覆盖计数操作都由同一互斥锁保护。
 *
 * @tparam T 元素类型，必须支持移动赋值。
 */
    template <typename T>
    class mpmc_blocking_queue {
    public:
        /// 队列元素类型。
        using item_type = T;

        /**
     * @brief 条件覆盖入队的结果。
     *
     * enqueued 为 false 表示谓词拒绝覆盖队首元素，此时传入元素保持未移动状态；
     * overwritten 有值表示入队时移出了原队首元素。
     */
        struct conditional_enqueue_result {
            bool enqueued{false};         ///< 新元素是否已入队。
            std::optional<T> overwritten; ///< 被覆盖的旧元素；未覆盖时为空。
        };

        /** @brief 创建固定容量队列。 @param max_items 精确可容纳的元素数量。 */
        explicit mpmc_blocking_queue(std::size_t max_items) : q_(max_items) {}

        mpmc_blocking_queue(const mpmc_blocking_queue&) = delete;
        mpmc_blocking_queue& operator=(const mpmc_blocking_queue&) = delete;

        /**
     * @brief 将元素加入队尾，空间不足时阻塞。
     * @param item 待移动的元素。
     * @param notify 是否唤醒一个等待消费的线程。
     */
        void enqueue(T&& item, bool notify = true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                pop_cv_.wait(lock, [this] { return !q_.full(); });
                q_.push_back(std::move(item));
            }
            if (notify) {
                push_cv_.notify_one();
            }
        }

        /**
     * @brief 无阻塞入队，队列满时覆盖最旧元素。
     * @param item 待移动的元素。
     * @param notify 是否唤醒一个等待消费的线程。
     * @return 被覆盖的旧元素；未发生覆盖时返回空值。
     */
        std::optional<T> enqueue_nowait(T&& item, bool notify = true) {
            auto result = enqueue_nowait_if(std::move(item), [](const T&) { return true; }, notify);
            return std::move(result.overwritten);
        }

        /**
     * @brief 按队首谓词执行无阻塞覆盖入队。
     * @tparam Predicate 可用 const T& 调用并返回 bool 的谓词类型。
     * @param item 待移动的元素；谓词拒绝覆盖时保持未移动状态。
     * @param can_overwrite 队列满时用于判断队首元素能否被覆盖的谓词。
     * @param notify 入队成功后是否唤醒一个等待消费的线程。
     * @return 入队状态及在同一临界区内移出的旧元素。
     */
        template <typename Predicate>
        conditional_enqueue_result enqueue_nowait_if(T&& item, Predicate&& can_overwrite, bool notify = true) {
            conditional_enqueue_result result;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (q_.full()) {
                    if (!std::forward<Predicate>(can_overwrite)(q_.front())) {
                        return result;
                    }
                    result.overwritten.emplace(std::move(q_.front()));
                }
                q_.push_back(std::move(item));
                result.enqueued = true;
            }
            if (notify) {
                push_cv_.notify_one();
            }
            return result;
        }

        /**
     * @brief 尝试将元素加入队尾。
     * @param item 待移动的元素；失败时保持未移动状态。
     * @param notify 是否唤醒一个等待消费的线程。
     * @return 入队成功时返回 true，队列已满时返回 false。
     */
        [[nodiscard]] bool try_enqueue(T&& item, bool notify = true) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (q_.full()) {
                    return false;
                }
                q_.push_back(std::move(item));
            }
            if (notify) {
                push_cv_.notify_one();
            }
            return true;
        }

        /**
     * @brief 在限定时间内等待并取出队首元素。
     * @param popped_item 接收移动出的元素。
     * @param wait_duration 最长等待时间。
     * @return 成功取出元素时返回 true，超时时返回 false。
     */
        [[nodiscard]] bool dequeue_for(T& popped_item, std::chrono::nanoseconds wait_duration) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!push_cv_.wait_for(lock, wait_duration, [this] { return !q_.empty(); })) {
                    return false;
                }
                popped_item = std::move(q_.front());
                q_.pop_front();
            }
            pop_cv_.notify_one();
            return true;
        }

        /// 唤醒一个等待消费的线程。
        void wake_one() { push_cv_.notify_one(); }

        /// 唤醒全部等待消费的线程。
        void wake_all() { push_cv_.notify_all(); }

        /** @brief 获取累计覆盖次数。 */
        [[nodiscard]] std::size_t overrun_count() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return q_.overrun_count();
        }

        /** @brief 获取当前元素数量。 */
        [[nodiscard]] std::size_t size() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return q_.size();
        }

    private:
        /// 受 mutex_ 保护的循环存储区。
        circular_q<T> q_;
        /// 保护队列状态和覆盖计数。
        mutable std::mutex mutex_;
        /// 消费者等待新元素的条件变量。
        std::condition_variable push_cv_;
        /// 生产者等待可用空间的条件变量。
        std::condition_variable pop_cv_;
    };

} // details
} // minispdlog
