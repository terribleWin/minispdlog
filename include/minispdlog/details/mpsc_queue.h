#pragma once

#include <atomic>
#include <cstddef>
#include <utility>
#include <vector>

namespace minispdlog {
namespace details {

// Bounded MPSC ring. Producers CAS-claim a write ticket; the consumer is
// single-threaded. Do not fetch_add/fetch_sub the write index — rolling it
// back races with other producers and can skip or double-use slots.
template<typename T>
class mpsc_queue {
public:
    explicit mpsc_queue(size_t capacity)
        : capacity_(capacity)
        , buffer_(capacity_)
        , ready_(capacity_)
        , write_idx_(0)
        , read_idx_(0) {}

    bool push(T&& item) {
        size_t pos = 0;
        if (!claim_write_slot_(pos)) {
            return false;
        }
        const size_t index = pos % capacity_;
        buffer_[index] = std::move(item);
        ready_[index].store(1, std::memory_order_release);
        return true;
    }

    bool push(const T& item) {
        size_t pos = 0;
        if (!claim_write_slot_(pos)) {
            return false;
        }
        const size_t index = pos % capacity_;
        buffer_[index] = item;
        ready_[index].store(1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const size_t read_pos = read_idx_.load(std::memory_order_relaxed);
        const size_t index = read_pos % capacity_;

        if (!ready_[index].load(std::memory_order_acquire)) {
            return false;
        }

        item = std::move(buffer_[index]);
        ready_[index].store(0, std::memory_order_relaxed);
        read_idx_.store(read_pos + 1, std::memory_order_release);
        return true;
    }

    size_t size() const {
        const auto w = write_idx_.load(std::memory_order_acquire);
        const auto r = read_idx_.load(std::memory_order_acquire);
        return w - r;
    }

    bool empty() const { return size() == 0; }

    size_t capacity() const { return capacity_; }

private:
    bool claim_write_slot_(size_t& pos) {
        for (;;) {
            pos = write_idx_.load(std::memory_order_relaxed);
            const auto read_pos = read_idx_.load(std::memory_order_acquire);
            if (pos - read_pos >= capacity_) {
                return false;
            }
            if (write_idx_.compare_exchange_weak(pos, pos + 1, std::memory_order_acq_rel,
                                                 std::memory_order_relaxed)) {
                return true;
            }
        }
    }

    size_t capacity_;
    std::vector<T> buffer_;
    std::vector<std::atomic<int>> ready_;
    std::atomic<size_t> write_idx_;
    std::atomic<size_t> read_idx_;
};

}  // namespace details
}  // namespace minispdlog
