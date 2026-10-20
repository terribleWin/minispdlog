#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace minispdlog {
namespace details {

    /**
 * @brief 有界多生产者、单消费者环形队列。
 *
 * 生产者通过 CAS 独占写票据，消费者严格按票据顺序读取。队列保持构造时的
 * 精确容量；容量为 2 的幂时使用预计算掩码寻址，否则使用取模寻址。
 *
 * @tparam T 元素类型，必须支持默认构造和相应的赋值操作。
 * @note 仅允许一个线程调用 pop，多线程并发调用 push 是安全的。
 */
    template <typename T>
    class mpsc_queue {
    public:
        /**
     * @brief 创建指定精确容量的队列。
     * @param capacity 队列容量，必须大于零。
     * @throws std::invalid_argument capacity 为零时抛出。
     */
        explicit mpsc_queue(std::size_t capacity)
            : capacity_(checked_capacity_(capacity)), index_mask_(is_power_of_two_(capacity_) ? capacity_ - 1 : 0),
              use_mask_(is_power_of_two_(capacity_)), buffer_(capacity_), ready_(capacity_) {
            for (auto& ready : ready_) {
                ready.store(false, std::memory_order_relaxed);
            }
        }

        /**
     * @brief 移动入队一个元素。
     * @param item 待移动元素；队列已满时保持未移动状态。
     * @return 入队成功时返回 true，队列已满时返回 false。
     */
        [[nodiscard]] bool push(T&& item) {
            std::size_t pos = 0;
            if (!claim_write_slot_(pos)) {
                return false;
            }
            const auto index = index_for_(pos);
            buffer_[index] = std::move(item);
            ready_[index].store(true, std::memory_order_release);
            return true;
        }

        /**
     * @brief 复制入队一个元素。
     * @param item 待复制元素。
     * @return 入队成功时返回 true，队列已满时返回 false。
     */
        [[nodiscard]] bool push(const T& item) {
            std::size_t pos = 0;
            if (!claim_write_slot_(pos)) {
                return false;
            }
            const auto index = index_for_(pos);
            buffer_[index] = item;
            ready_[index].store(true, std::memory_order_release);
            return true;
        }

        /**
     * @brief 由单一消费者尝试取出队首元素。
     * @param item 接收移动出的元素。
     * @return 成功取出时返回 true，队首尚未发布或队列为空时返回 false。
     */
        [[nodiscard]] bool pop(T& item) {
            const auto read_pos = read_idx_.load(std::memory_order_relaxed);
            const auto index = index_for_(read_pos);
            if (!ready_[index].load(std::memory_order_acquire)) {
                return false;
            }

            item = std::move(buffer_[index]);
            ready_[index].store(false, std::memory_order_relaxed);
            read_idx_.store(read_pos + 1, std::memory_order_release);
            return true;
        }

        /**
     * @brief 获取已占用写票据数量。
     * @return 包含尚未发布票据在内的并发快照。
     */
        [[nodiscard]] std::size_t size() const {
            const auto write_pos = write_idx_.load(std::memory_order_acquire);
            const auto read_pos = read_idx_.load(std::memory_order_acquire);
            return write_pos - read_pos;
        }

        /** @brief 判断队列是否没有已占用票据。 */
        [[nodiscard]] bool empty() const { return size() == 0; }

        /** @brief 获取构造时指定的精确容量。 */
        [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    private:
        /// 缓存行保守值，可跨主流编译器稳定隔离生产者与消费者索引。
        static constexpr std::size_t cache_line_size_ = 64;

        /// 校验容量并返回原值。
        static std::size_t checked_capacity_(std::size_t capacity) {
            if (capacity == 0) {
                throw std::invalid_argument("mpsc_queue: capacity must be greater than zero");
            }
            return capacity;
        }

        /// 判断正整数是否为 2 的幂。
        [[nodiscard]] static constexpr bool is_power_of_two_(std::size_t value) noexcept {
            return value != 0 && (value & (value - 1)) == 0;
        }

        /// 将单调票据映射到环形存储槽。
        [[nodiscard]] std::size_t index_for_(std::size_t pos) const noexcept {
            return use_mask_ ? (pos & index_mask_) : (pos % capacity_);
        }

        /**
     * @brief 尝试独占一个写票据。
     * @param pos 接收成功占用的票据。
     * @return 成功时返回 true，容量已用尽时返回 false。
     */
        [[nodiscard]] bool claim_write_slot_(std::size_t& pos) {
            for (;;) {
                pos = write_idx_.load(std::memory_order_relaxed);
                const auto read_pos = read_idx_.load(std::memory_order_acquire);
                if (pos - read_pos >= capacity_) {
                    return false;
                }
                if (write_idx_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                                     std::memory_order_relaxed)) {
                    return true;
                }
            }
        }

        /// 精确容量，不做向上取整。
        const std::size_t capacity_;
        /// 仅在容量为 2 的幂时用于快速寻址。
        const std::size_t index_mask_;
        /// 指示是否启用掩码寻址。
        const bool use_mask_;
        /// 元素存储区。
        std::vector<T> buffer_;
        /// 每个槽位的发布标记。
        std::vector<std::atomic<bool>> ready_;

        /// 生产者竞争的下一个写票据。
        alignas(cache_line_size_) std::atomic<std::size_t> write_idx_{0};
        /// 仅由消费者推进的下一个读票据，与写索引分处不同缓存行。
        alignas(cache_line_size_) std::atomic<std::size_t> read_idx_{0};
    };

} ///< 结束 details 命名空间。
} ///< 结束 minispdlog 命名空间。
