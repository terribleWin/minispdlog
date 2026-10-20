#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace minispdlog {
namespace details {

    /** @brief 无锁环形队列元素必须可默认构造且可移动。 */
    template <typename T>
    concept QueueItem = std::default_initializable<T> && std::movable<T>;

    /**
 * @brief 将容量向上取整为 2 的幂，最小返回 2。
 * @note 输入为 0 或 1 时返回 2。
 */
    [[nodiscard]] constexpr std::size_t next_power_of_two(std::size_t n) noexcept {
        if (n < 2) {
            return 2;
        }
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        if constexpr (sizeof(std::size_t) > 4) {
            n |= n >> 32;
        }
        return n + 1;
    }

    /** @brief 用于隔离高争用原子的保守缓存行字节数。 */
    inline constexpr std::size_t kCacheLineSize = 64;

} ///< 结束 details 命名空间。
} ///< 结束 minispdlog 命名空间。
