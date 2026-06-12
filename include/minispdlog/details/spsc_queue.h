#pragma once

#include <atomic>
#include <vector>
#include <cstddef>

namespace minispdlog {
namespace details {

template<typename T>
class spsc_queue {
public:
    explicit spsc_queue(size_t capacity)
        : capacity_(capacity)
        , buffer_(capacity_)
        , write_idx_(0)
        , read_idx_(0) {}

    // 生产者调用：返回 true 表示入队成功
    bool push(const T& item) {
        auto write_pos = write_idx_.load(std::memory_order_relaxed);
        auto read_pos  = read_idx_.load(std::memory_order_acquire);

        if (write_pos - read_pos >= capacity_) return false;

        buffer_[write_pos % capacity_] = item;
        write_idx_.store(write_pos + 1, std::memory_order_release);
        return true;
    }

    bool push(T&& item) {
        auto write_pos = write_idx_.load(std::memory_order_relaxed);
        auto read_pos  = read_idx_.load(std::memory_order_acquire);

        if (write_pos - read_pos >= capacity_) return false;

        buffer_[write_pos % capacity_] = std::move(item);
        write_idx_.store(write_pos + 1, std::memory_order_release);
        return true;
    }

    // 消费者调用：返回 true 表示出队成功
    bool pop(T& item) {
        auto read_pos = read_idx_.load(std::memory_order_relaxed);
        auto write_pos = write_idx_.load(std::memory_order_acquire);

        if (read_pos == write_pos) return false;

        item = std::move(buffer_[read_pos % capacity_]);
        read_idx_.store(read_pos + 1, std::memory_order_release);
        return true;
    }

    size_t size() const {
        auto w = write_idx_.load(std::memory_order_acquire);
        auto r = read_idx_.load(std::memory_order_acquire);
        return w - r;
    }

    bool empty() const { return size() == 0; }

private:
    size_t capacity_;
    std::vector<T> buffer_;
    std::atomic<size_t> write_idx_;
    std::atomic<size_t> read_idx_;
};

} // namespace details
} // namespace minispdlog
