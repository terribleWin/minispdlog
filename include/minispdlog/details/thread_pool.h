#pragma once
#include "../common.h"
#include "mpmc_blocking_q.h"
#include "async_msg.h"
#include <thread>
#include <vector>
#include <functional>
namespace minispdlog {
    namespace details {
class MINISPDLOG_API thread_pool {
    public:
        using item_type = async_msg;
        using q_type = mpmc_blocking_queue<item_type>;
        thread_pool(size_t queue_size, size_t threads_n);
        //禁止拷贝
        thread_pool(const thread_pool&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;
        ~thread_pool();
        //阻塞模式
        void post_log(std::shared_ptr<logger> &&logger_ptr, const log_msg& msg);
        //覆盖
        void post_log_nowait(std::shared_ptr<logger> &&logger_ptr, const log_msg& msg);
        //刷新请求
        void post_flush(std::shared_ptr<logger> &&logger_ptr);
        size_t overrun_count();
    private:
        void worker_loop_();
        bool process_next_msg();
        q_type q_;
        std::vector<std::thread> threads_;
};
    }
}