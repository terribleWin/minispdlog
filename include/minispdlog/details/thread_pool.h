#pragma once

#include "../async_config.h"
#include "../common.h"
#include "async_msg.h"
#include "mpmc_blocking_q.h"
#include "mpsc_queue.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace minispdlog {
namespace details {

    /**
 * @brief 同步日志器之外的异步执行核心。
 *
 * 线程池保留两种后端：阻塞 MPMC 队列可配置多个消费者，无锁 MPSC 队列固定
 * 使用一个消费者。等待式 flush 通过双代际屏障只等待提交点之前的记录，提交点
 * 之后持续产生的新日志不会延长本次等待。
 */
    class MINISPDLOG_API thread_pool {
    public:
        /// 队列元素类型。
        using item_type = async_msg;

        /**
     * @brief 使用基础参数创建线程池。
     * @param queue_size 队列精确容量，必须大于零。
     * @param threads_n 工作线程数量，范围为 1 到 1000。
     * @param queue_type 队列后端类型。
     */
        thread_pool(std::size_t queue_size, std::size_t threads_n,
                    async_queue_type queue_type = async_queue_type::blocking);
        /** @brief 使用完整选项创建线程池。 @param opts 初始化选项。 */
        explicit thread_pool(const thread_pool_options& opts);

        thread_pool(const thread_pool&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;
        ~thread_pool();

        /// 停止并汇合所有工作线程；该操作可重复调用，析构函数也会调用它。
        void shutdown();
        /// 判断线程池是否仍接受异步投递。
        [[nodiscard]] bool active() const noexcept { return !shutting_down_.load(std::memory_order_acquire); }

        /**
     * @brief 以裸指针投递日志，避免热路径增加共享引用计数。
     * @param worker 处理消息的日志器，调用方必须保证队列排空前对象存活。
     * @param msg 待复制到异步消息中的日志记录。
     * @param policy 队列满时的处理策略。
     */
        void post_log(logger* worker, const log_msg& msg, async_overflow_policy policy = async_overflow_policy::block);
        /**
     * @brief 投递日志并将日志器所有权转移给异步消息。
     */
        void post_log(std::shared_ptr<logger>&& logger_ptr, const log_msg& msg,
                      async_overflow_policy policy = async_overflow_policy::block);
        /**
     * @brief 投递日志并在异步消息中保留日志器共享所有权。
     */
        void post_log(const std::shared_ptr<logger>& logger_ptr, const log_msg& msg,
                      async_overflow_policy policy = async_overflow_policy::block) {
            post_log(std::shared_ptr<logger>(logger_ptr), msg, policy);
        }
        /// 以 discard_new 策略投递裸指针日志器的日志。
        void post_log_nowait(logger* worker, const log_msg& msg);
        /// 以 discard_new 策略投递并转移日志器共享所有权。
        void post_log_nowait(std::shared_ptr<logger>&& logger_ptr, const log_msg& msg);

        /**
     * @brief 投递裸指针日志器的刷新请求。
     * @param worker 待刷新的日志器，必须在请求完成前保持存活。
     * @param wait 是否等待刷新及此前已接收的日志全部完成。
     */
        void post_flush(logger* worker, bool wait = false);
        /// 投递刷新请求并转移日志器共享所有权。
        void post_flush(std::shared_ptr<logger>&& logger_ptr, bool wait = false);
        /// 投递刷新请求并在异步消息中保留日志器共享所有权。
        void post_flush(const std::shared_ptr<logger>& logger_ptr, bool wait = false) {
            post_flush(std::shared_ptr<logger>(logger_ptr), wait);
        }

        /// 获取阻塞队列累计覆盖的消息数量。
        [[nodiscard]] std::size_t overrun_count() const;
        /// 获取因 discard_new 策略丢弃的消息数量。
        [[nodiscard]] std::size_t discard_count() const;
        /// 获取异步日志后端累计抛出的异常数量。
        [[nodiscard]] std::size_t error_count() const noexcept { return error_count_.load(std::memory_order_relaxed); }
        /// 获取当前队列后端类型。
        [[nodiscard]] async_queue_type queue_type() const noexcept { return queue_type_; }
        /// 获取实际工作线程数量。
        [[nodiscard]] std::size_t worker_count() const noexcept { return threads_.size(); }

    private:
        /// 阻塞后端工作循环。
        void worker_loop_blocking_();
        /// 无锁后端工作循环。
        void worker_loop_lockfree_();
        /// 处理单条异步消息，终止消息返回 false。
        bool process_msg_(async_msg& incoming);
        /// 登记并投递一条日志消息。
        void enqueue_log_(async_msg&& msg, async_overflow_policy policy);
        /// 按指定策略投递到阻塞队列。
        [[nodiscard]] bool enqueue_blocking_(async_msg&& msg, async_overflow_policy policy, bool notify);
        /// 按指定策略投递到无锁队列。
        [[nodiscard]] bool enqueue_lockfree_(async_msg&& msg, async_overflow_policy policy);
        /// 根据批量阈值与空闲状态决定是否唤醒消费者。
        void maybe_wake_log_() noexcept;
        /// 强制唤醒至少一个消费者。
        void force_wake_() noexcept;
        /// 强制唤醒全部消费者。
        void force_wake_all_() noexcept;
        /// 发布一次消费者唤醒事件。
        void notify_consumer_() noexcept;
        /// 将日志登记到当前刷新代际，并返回稳定的代际编号。
        [[nodiscard]] std::uint64_t register_log_epoch_() noexcept;
        /// 完成、丢弃或覆盖日志后归还其代际计数，并按需记录后端异常。
        void on_log_completed_(std::uint64_t epoch, std::exception_ptr error = {}) noexcept;
        /// 等待指定刷新代际内的日志全部完成。
        void wait_for_epoch_(std::uint64_t epoch) noexcept;
        /// 取出指定代际首个尚未报告的后端异常；同一异常只报告一次。
        [[nodiscard]] std::exception_ptr take_epoch_error_(std::uint64_t epoch) noexcept;

        /// 固定复用的刷新代际状态；等待式 flush 串行切换，确保槽位不会提前复用。
        struct epoch_state {
            std::atomic<std::size_t> pending{0};
            std::mutex error_mutex;
            std::exception_ptr first_error;
        };

        /// 队列后端类型。
        async_queue_type queue_type_;
        /// 构造阶段确定的实际工作线程数量。
        std::size_t worker_count_{1};
        /// 主动唤醒消费者的批量阈值。
        std::size_t wake_batch_{64};
        /// 消费者处理尾部消息的最长等待间隔。
        std::chrono::microseconds wake_interval_{100};

        /// 阻塞 MPMC 后端，仅在选择 blocking 时存在。
        std::unique_ptr<mpmc_blocking_queue<item_type>> blocking_q_;
        /// 无锁 MPSC 后端，仅在选择 lockfree 时存在。
        std::unique_ptr<mpsc_queue<item_type>> lockfree_q_;

        /// 保护无锁后端的休眠条件变量。
        std::mutex park_mu_;
        /// 无锁消费者的休眠条件变量。
        std::condition_variable park_cv_;
        /// 防止无锁消费者丢失唤醒的事件序号。
        std::atomic<std::uint64_t> park_seq_{0};
        /// 当前处于等待阶段的工作线程数量。
        std::atomic<std::size_t> idle_workers_{0};
        /// 尚未触发主动唤醒的日志数量。
        std::atomic<std::size_t> unnotified_{0};

        /// 线程池关闭标志。
        std::atomic<bool> shutting_down_{false};
        /// discard_new 策略累计丢弃数量。
        std::atomic<std::size_t> discard_count_{0};
        /// 异步日志后端累计异常数量。
        std::atomic<std::size_t> error_count_{0};
        /// 当前接收新日志的刷新代际。
        std::atomic<std::uint64_t> current_epoch_{0};
        /// 两个固定槽位交替承载相邻代际，避免每条日志动态分配状态。
        epoch_state epochs_[2];
        /// 仅串行化低频等待式 flush，生产者热路径不获取该锁。
        std::mutex flush_barrier_mutex_;

        /// 工作线程集合。
        std::vector<std::thread> threads_;
    };

} // details
} // minispdlog
