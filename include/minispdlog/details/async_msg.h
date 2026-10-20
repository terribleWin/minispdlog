#pragma once

#include "log_msg.h"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <utility>

namespace minispdlog {

class logger;

namespace details {

    /// 异步队列中的消息类型。
    enum class async_msg_type {
        log,      ///< 写入一条日志。
        flush,    ///< 刷新指定日志器。
        terminate ///< 终止一个工作线程。
    };

    /**
 * @brief 拥有异步日志文本的消息缓冲区。
 *
 * 消息会复制生产者临时缓冲区中的文本，使生产者返回后字符串视图仍然有效。
 * 短消息保存在内联空间，超过内联容量时才申请堆内存。
 */
    struct log_msg_buffer : log_msg {
        /// 内联字符容量。
        static constexpr std::size_t k_inline = 256;

        log_msg_buffer() = default;

        /** @brief 复制日志器名称和正文。 */
        explicit log_msg_buffer(const log_msg& msg) { copy_from(msg, true); }
        /** @brief 按需复制日志器名称，并始终复制正文。 */
        log_msg_buffer(const log_msg& msg, bool copy_name) { copy_from(msg, copy_name); }

        /** @brief 移动消息并重建指向内部缓冲区的字符串视图。 */
        log_msg_buffer(log_msg_buffer&& other) noexcept { move_from(std::move(other)); }

        /** @brief 移动赋值并重建指向内部缓冲区的字符串视图。 */
        log_msg_buffer& operator=(log_msg_buffer&& other) noexcept {
            if (this != &other) {
                move_from(std::move(other));
            }
            return *this;
        }

        log_msg_buffer(const log_msg_buffer&) = delete;
        log_msg_buffer& operator=(const log_msg_buffer&) = delete;

    protected:
        /// 从源消息复制元数据与文本，并修正字符串视图。
        void copy_from(const log_msg& msg, bool copy_name) {
            const auto name_len = msg.logger_name.size();
            const auto payload_len = msg.payload.size();
            static_cast<log_msg&>(*this) = msg;
            buffer_.clear();
            owns_name_ = copy_name;
            if (copy_name && name_len != 0 && msg.logger_name.data() != nullptr) {
                buffer_.append(msg.logger_name.data(), msg.logger_name.data() + name_len);
            }
            if (payload_len != 0 && msg.payload.data() != nullptr) {
                buffer_.append(msg.payload.data(), msg.payload.data() + payload_len);
            }
            if (copy_name) {
                rebase(name_len, payload_len);
            } else {
                payload = string_view_t(buffer_.data(), payload_len);
            }
        }

        /// 移动缓冲区后将原有内部字符串视图重新定位到新地址。
        void move_from(log_msg_buffer&& other) noexcept {
            const auto name_len = other.logger_name.size();
            const auto payload_len = other.payload.size();
            const bool owns_name = other.owns_name_;

            static_cast<log_msg&>(*this) = static_cast<log_msg&&>(other);
            buffer_ = std::move(other.buffer_);
            owns_name_ = owns_name;
            if (owns_name_) {
                rebase(name_len, payload_len);
            } else {
                payload = string_view_t(buffer_.data(), payload_len);
            }
            other.logger_name = {};
            other.payload = {};
            other.owns_name_ = false;
        }

        /// 将日志器名称和正文视图定位到当前缓冲区。
        void rebase(std::size_t name_len, std::size_t payload_len) noexcept {
            const char* data = buffer_.data();
            logger_name = string_view_t(data, name_len);
            payload = string_view_t(data + name_len, payload_len);
        }

        /// 拥有日志器名称与正文的连续字符缓冲区。
        fmt::basic_memory_buffer<char, k_inline> buffer_{};
        /// 指示日志器名称是否也存放在内部缓冲区中。
        bool owns_name_{false};
    };

    /**
 * @brief 在线程池生产者与消费者之间传递的可移动消息。
 *
 * 裸 worker 指针用于 async_logger 热路径；此时日志器析构会同步排空队列。
 * 直接使用线程池的调用方可传入 shared_ptr，由 keep_alive 保证消费前对象存活。
 */
    struct async_msg : log_msg_buffer {
        /// 消息类型。
        async_msg_type msg_type{async_msg_type::log};
        /// 实际执行后端操作的日志器。
        logger* worker{nullptr};
        /// 调用方传入共享所有权时保存日志器生命周期。
        std::shared_ptr<logger> keep_alive;
        /// 同步刷新等待完成时使用的确认信号。
        std::shared_ptr<std::promise<void>> ack;
        /// 日志所属的刷新屏障代际，仅对 log 消息有效。
        std::uint64_t completion_epoch{0};

        async_msg() = default;

        /// 移动消息并转移生命周期及确认信号。
        async_msg(async_msg&& other) noexcept
            : log_msg_buffer(std::move(other)), msg_type(other.msg_type), worker(other.worker),
              keep_alive(std::move(other.keep_alive)), ack(std::move(other.ack)),
              completion_epoch(other.completion_epoch) {
            other.worker = nullptr;
            other.completion_epoch = 0;
        }

        /// 移动赋值；覆盖旧刷新消息前先完成其确认信号。
        async_msg& operator=(async_msg&& other) noexcept {
            if (this != &other) {
                notify_ack();
                static_cast<log_msg_buffer&>(*this) = std::move(other);
                msg_type = other.msg_type;
                worker = other.worker;
                other.worker = nullptr;
                keep_alive = std::move(other.keep_alive);
                ack = std::move(other.ack);
                completion_epoch = other.completion_epoch;
                other.completion_epoch = 0;
            }
            return *this;
        }

        async_msg(const async_msg&) = delete;
        async_msg& operator=(const async_msg&) = delete;

        /// 销毁未处理消息前完成可能存在的确认信号。
        ~async_msg() { notify_ack(); }

        /**
         * @brief 以不抛异常的方式完成并释放确认信号。
         * @param error 后端操作失败时传递给等待方的异常；空值表示成功。
         */
        void notify_ack(std::exception_ptr error = {}) noexcept {
            auto pending = std::move(ack);
            if (!pending) {
                return;
            }
            try {
                if (error) {
                    pending->set_exception(std::move(error));
                } else {
                    pending->set_value();
                }
            } catch (...) {
            }
        }

        /// 使用裸日志器指针构造日志消息，仅复制正文。
        async_msg(async_msg_type type, logger* worker_ptr, const log_msg& msg)
            : log_msg_buffer(msg, false), msg_type(type), worker(worker_ptr) {}

        /// 使用共享日志器所有权构造日志消息，仅复制正文。
        async_msg(async_msg_type type, std::shared_ptr<logger>&& worker_ptr, const log_msg& msg)
            : log_msg_buffer(msg, false), msg_type(type), worker(worker_ptr.get()), keep_alive(std::move(worker_ptr)) {}

        /// 使用裸日志器指针构造控制消息。
        async_msg(async_msg_type type, logger* worker_ptr) : msg_type(type), worker(worker_ptr) {}

        /// 使用共享日志器所有权构造控制消息。
        async_msg(async_msg_type type, std::shared_ptr<logger>&& worker_ptr)
            : msg_type(type), worker(worker_ptr.get()), keep_alive(std::move(worker_ptr)) {}

        /// 构造不关联日志器的控制消息。
        explicit async_msg(async_msg_type type) : async_msg(type, static_cast<logger*>(nullptr)) {}
    };

} // details
} // minispdlog
