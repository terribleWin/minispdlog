#pragma once
#include "circular_q.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace minispdlog {
namespace details {

        // MPMC 阻塞队列
        // enqueue :队列满时阻塞
        // enqueue_for :队列满时等待指定时间
        // enqueu_nowait :队列满时覆盖最旧消息
        template<typename T>
        class mpmc_blocking_queue {
            public:
            using item_type = T;

            explicit mpmc_blocking_queue(size_t max_items)
                : q_(max_items)
                {}
            mpmc_blocking_queue(const mpmc_blocking_queue&) = delete;
            mpmc_blocking_queue& operator=(const mpmc_blocking_queue&) = delete;

            void enqueue(T&& item, bool notify = true) {
                std::unique_lock<std::mutex> lock(mutex_);
                pop_cv_.wait(lock, [this] { return !q_.full(); });
                q_.push_back(std::move(item));
                if (notify) {
                    push_cv_.notify_one();
                }
            }

            void enqueue_nowait(T&& item, bool notify = true) {
                std::lock_guard<std::mutex> lock(mutex_);
                q_.push_back(std::move(item));
                if (notify) {
                    push_cv_.notify_one();
                }
            }

            [[nodiscard]] bool try_enqueue(T&& item, bool notify = true) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (q_.full()) {
                    return false;
                }
                q_.push_back(std::move(item));
                if (notify) {
                    push_cv_.notify_one();
                }
                return true;
            }

            bool dequeue_for(T& poped_item, std::chrono::nanoseconds wait_duration) {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!push_cv_.wait_for(lock, wait_duration, [this] { return !q_.empty(); })) {
                    return false;
                }
                poped_item = std::move(q_.front());
                q_.pop_front();
                pop_cv_.notify_one();
                return true;
            }

            void wake_one() {
                push_cv_.notify_one();
            }

            void wake_all() {
                push_cv_.notify_all();
            }

            size_t overrun_count() const {
                std::lock_guard<std::mutex> lock(mutex_);
                return q_.overrun_count();
            }
            size_t size() {
                std::unique_lock<std::mutex> lock(mutex_);
                return q_.size();
            }
            private:
                circular_q<T> q_;
                mutable std::mutex mutex_;
                std::condition_variable push_cv_;
                std::condition_variable pop_cv_;
        };
}
}
