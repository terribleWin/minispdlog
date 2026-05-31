#pragma once

#include <cstddef>
#include <vector>

namespace minispdlog {
namespace details {

template<typename T>
class circular_q {
public:
    using value_type = T;

    explicit circular_q(size_t max_items)
        : max_items_(max_items + 1)
        , v_(max_items_)
        , head_(0)
        , tail_(0)
        , overrun_counter_(0)
    {}

    circular_q(const circular_q&) = delete;
    circular_q& operator=(const circular_q&) = delete;

    void push_back(T&& item) {
        v_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % max_items_;
        if (tail_ == head_) {
            head_ = (head_ + 1) % max_items_;
            ++overrun_counter_;
        }
    }

    const T& front() const { return v_[head_]; }
    T& front() { return v_[head_]; }

    void pop_front() {
        head_ = (head_ + 1) % max_items_;
    }

    bool empty() const { return head_ == tail_; }
    bool full() const { return (tail_ + 1) % max_items_ == head_; }

    size_t size() const {
        if (tail_ >= head_) {
            return tail_ - head_;
        } else {
            return max_items_ - (head_ - tail_);
        }
    }

    size_t capacity() const { return max_items_ - 1; }
    size_t overrun_count() const { return overrun_counter_; }

private:
    size_t max_items_;
    std::vector<T> v_;
    size_t head_;
    size_t tail_;
    size_t overrun_counter_;
};

} // namespace details
} // namespace minispdlog
