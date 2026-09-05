#pragma once

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace minispdlog {
namespace details {

/// Items stored in lock-free rings must be default-constructible and movable.
template <typename T>
concept QueueItem = std::default_initializable<T> && std::movable<T>;

/// Round up to the next power of two (minimum 2). Capacity 0/1 → 2.
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

/// Cache-line size for false-sharing padding (portable fallback).
inline constexpr std::size_t kCacheLineSize = 64;

}  // namespace details
}  // namespace minispdlog
