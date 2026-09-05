#pragma once

#include <atomic>
#include <vector>
#include <cstddef>

namespace minispdlog {
namespace details {

// 无锁 MPSC 队列（多生产者单消费者）
// 生产者用 CAS 竞争写入槽位，消费者无锁读取
template<typename T>
class mpsc_queue {
public:
    explicit mpsc_queue(size_t capacity)
        : capacity_(capacity)
        , buffer_(capacity_)
        , ready_(capacity_)
        , write_idx_(0)
        , read_idx_(0) {}

    // 生产者调用（多线程安全，CAS 竞争）
    bool push(T&& item) {
        size_t write_pos = write_idx_.fetch_add(1, std::memory_order_relaxed);
        size_t index = write_pos % capacity_;

        // 队列满？检查读索引
        size_t read_pos = read_idx_.load(std::memory_order_acquire);
        if (write_pos - read_pos >= capacity_) {
            // CAS 回退
            write_idx_.fetch_sub(1, std::memory_order_relaxed);
            return false;
        }

        // 写入数据
        buffer_[index] = std::move(item);

        // 标记该槽位已就绪
        ready_[index].store(1, std::memory_order_release);
        return true;
    }

    // 消费者调用（单线程，无锁）
    bool pop(T& item) {
        size_t read_pos = read_idx_.load(std::memory_order_relaxed);
        size_t index = read_pos % capacity_;

        // 检查槽位是否已就绪
        if (!ready_[index].load(std::memory_order_acquire)) {
            return false;  // 生产者还没写完
        }

        // 读取数据
        item = std::move(buffer_[index]);
        ready_[index].store(0, std::memory_order_relaxed);

        // 推进读指针
        read_idx_.store(read_pos + 1, std::memory_order_release);
        return true;
    }

    bool push(const T& item) {
        size_t write_pos = write_idx_.fetch_add(1, std::memory_order_relaxed);
        size_t index = write_pos % capacity_;

        size_t read_pos = read_idx_.load(std::memory_order_acquire);
        if (write_pos - read_pos >= capacity_) {
            write_idx_.fetch_sub(1, std::memory_order_relaxed);
            return false;
        }

        buffer_[index] = item;
        ready_[index].store(1, std::memory_order_release);
        return true;
    }

    size_t size() const {
        auto w = write_idx_.load(std::memory_order_acquire);
        auto r = read_idx_.load(std::memory_order_acquire);
        return w - r;
    }

    bool empty() const { return size() == 0; }

    size_t capacity() const { return capacity_; }

private:
    size_t capacity_;
    std::vector<T> buffer_;
    std::vector<std::atomic<int>> ready_;  // 每个槽位的就绪标志
    std::atomic<size_t> write_idx_;
    std::atomic<size_t> read_idx_;
};

} // namespace details
} // namespace minispdlog
