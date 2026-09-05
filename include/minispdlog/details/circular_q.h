#pragma once
#include <cstddef>  // size_t
#include <vector>   // std::vector
namespace minispdlog{
    namespace details {
        //circular_q 循环队列
        template<typename T>
        class circular_q {
            public:
                using value_type = T;

                explicit circular_q(size_t max_items)
                    : max_items_(max_items + 1) // 多一个位置用于区分满和空
                    , v_(max_items_)
                    , head_(0)
                    , tail_(0)
                    , overrun_counter_(0)   
                    {}
                circular_q(const circular_q&) = delete;
                circular_q& operator=(const circular_q&) = delete;
                //队尾添加元素
                void push_back(T&& item) {
                    v_[tail_] = std::move(item);
                    tail_ = (tail_ + 1) % max_items_;

                    // 如果队列满了,覆盖最旧的元素
                    if (tail_ == head_) {
                        head_ = (head_ + 1) % max_items_;
                        ++overrun_counter_;
                    }
                }
                //访问队头元素
                const T& front() const {
                    return v_[head_];
                }
                //非 const 版本
                T& front() {
                    return v_[head_];
                }
                //弹出队头元素
                void pop_front() {
                    head_ = (head_ + 1) % max_items_;// 移动头部指针
                }
                //检查队列是否为空                
                bool empty() const {
                    return head_ == tail_;
                }
                //检查队列是否已满
                bool full() const {
                    return (tail_ + 1) % max_items_ == head_;
                }
                //当前元素数量
                size_t size() const {
                    if (tail_ >= head_) {
                        return tail_ - head_;
                    } else {
                        return max_items_ - (head_ - tail_);
                    }
                }
                //容量
                size_t capacity() const {
                    return max_items_ - 1;
                }
                //溢出计数
                size_t overrun_count() const {
                    return overrun_counter_;
                }
            private:                
                size_t max_items_;
                std::vector<T> v_;
                size_t head_;
                size_t tail_;
                size_t overrun_counter_;
        };
    }
}