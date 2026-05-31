#pragma once

#include "log_msg.h"
#include <memory>
#include <string>

namespace minispdlog {

// 前向声明
class logger;

namespace details {

// 异步消息类型
enum class async_msg_type {
    log,        // 普通日志消息
    flush,      // 刷新请求
    terminate   // 终止线程池
};

// log_msg_buffer: 带缓冲的日志消息
// 参考 spdlog 设计:继承 log_msg 并深拷贝 payload
struct log_msg_buffer : log_msg {
    std::string buffer;  // 存储payload的深拷贝
    
    log_msg_buffer() = default;
    //移动构造
    log_msg_buffer(log_msg_buffer&& other) noexcept
        : log_msg(std::move(other))
        , buffer(std::move(other.buffer))
    {
        payload = string_view_t(buffer);  // ← 重新指向新 buffer
    }
        log_msg_buffer& operator=(log_msg_buffer&& other) noexcept {
        static_cast<log_msg&>(*this) = std::move(other);
        buffer = std::move(other.buffer);
        payload = string_view_t(buffer);
        return *this;
    }
    // 从 log_msg 构造(深拷贝)
    explicit log_msg_buffer(const log_msg& msg)
        : log_msg(msg)
        , buffer(msg.payload.data(), msg.payload.size())
    {
        payload = string_view_t(buffer);
    }
};

// async_msg: 异步日志消息
// 参考 spdlog 设计:继承 log_msg_buffer + logger 的 shared_ptr
//
// 关键设计:
//   - 继承 log_msg_buffer 自动处理消息深拷贝
//   - 使用 shared_ptr<logger> 确保 logger 在队列中的消息处理完前不被销毁
//   - 支持多种消息类型
struct async_msg : log_msg_buffer {
    async_msg_type msg_type{async_msg_type::log};
    
    // Logger 的 shared_ptr
    // 注意:这里使用 shared_ptr 而非 weak_ptr
    // 原因:队列中的消息需要确保 logger 存活直到消息被处理
    std::shared_ptr<logger> worker_ptr;
    
    // 默认构造
    async_msg() = default;
    async_msg& operator=(async_msg&&) = default;

    // should only be moved in or out of the queue..
    async_msg(const async_msg &) = delete;
    
    // 从 log_msg 构造
    async_msg(async_msg_type type, std::shared_ptr<logger> &&worker, const log_msg& msg)
        : log_msg_buffer(msg)
        , msg_type(type)
        , worker_ptr(std::move(worker))
    {}

    async_msg(async_msg_type the_type,std::shared_ptr<logger> &&worker)
        : log_msg_buffer{},
          msg_type{the_type},
          worker_ptr{std::move(worker)} {}

    explicit async_msg(async_msg_type the_type)
        : async_msg{the_type,nullptr} {}
};

} // namespace details
} // namespace minispdlog