#pragma once

#include "log_msg.h"

#include <fmt/format.h>

#include <cstddef>
#include <future>
#include <memory>
#include <utility>

namespace minispdlog {

class logger;

namespace details {

enum class async_msg_type {
    log,
    flush,
    terminate
};

// Owns logger_name + payload so the producer stack buffer can die after enqueue.
// Short lines stay in the inline slot; longer lines grow once on the heap.
struct log_msg_buffer : log_msg {
    static constexpr std::size_t k_inline = 256;

    log_msg_buffer() = default;

    explicit log_msg_buffer(const log_msg& msg) { copy_from(msg); }

    log_msg_buffer(log_msg_buffer&& other) noexcept { move_from(std::move(other)); }

    log_msg_buffer& operator=(log_msg_buffer&& other) noexcept {
        if (this != &other) {
            move_from(std::move(other));
        }
        return *this;
    }

    log_msg_buffer(const log_msg_buffer&) = delete;
    log_msg_buffer& operator=(const log_msg_buffer&) = delete;

protected:
    void copy_from(const log_msg& msg) {
        const auto name_len = msg.logger_name.size();
        const auto payload_len = msg.payload.size();
        static_cast<log_msg&>(*this) = msg;
        buffer_.clear();
        if (name_len != 0 && msg.logger_name.data() != nullptr) {
            buffer_.append(msg.logger_name.data(), msg.logger_name.data() + name_len);
        }
        if (payload_len != 0 && msg.payload.data() != nullptr) {
            buffer_.append(msg.payload.data(), msg.payload.data() + payload_len);
        }
        rebase(name_len, payload_len);
    }

    void move_from(log_msg_buffer&& other) noexcept {
        const auto name_len = other.logger_name.size();
        const auto payload_len = other.payload.size();
        static_cast<log_msg&>(*this) = static_cast<log_msg&&>(other);
        buffer_ = std::move(other.buffer_);
        rebase(name_len, payload_len);
        other.logger_name = {};
        other.payload = {};
    }

    void rebase(std::size_t name_len, std::size_t payload_len) noexcept {
        const char* data = buffer_.data();
        logger_name = string_view_t(data, name_len);
        payload = string_view_t(data + name_len, payload_len);
    }

    fmt::basic_memory_buffer<char, k_inline> buffer_{};
};

struct async_msg : log_msg_buffer {
    async_msg_type msg_type{async_msg_type::log};
    logger* worker{nullptr};
    // Only set when the caller transfers a shared_ptr (tests / unique ownership).
    // The async_logger hot path posts a raw `this` and drains in its destructor.
    std::shared_ptr<logger> keep_alive;
    std::shared_ptr<std::promise<void>> ack;

    async_msg() = default;

    async_msg(async_msg&& other) noexcept
        : log_msg_buffer(std::move(other))
        , msg_type(other.msg_type)
        , worker(other.worker)
        , keep_alive(std::move(other.keep_alive))
        , ack(std::move(other.ack)) {
        other.worker = nullptr;
    }

    async_msg& operator=(async_msg&& other) noexcept {
        if (this != &other) {
            notify_ack();
            static_cast<log_msg_buffer&>(*this) = std::move(other);
            msg_type = other.msg_type;
            worker = other.worker;
            other.worker = nullptr;
            keep_alive = std::move(other.keep_alive);
            ack = std::move(other.ack);
        }
        return *this;
    }

    async_msg(const async_msg&) = delete;
    async_msg& operator=(const async_msg&) = delete;

    ~async_msg() { notify_ack(); }

    void notify_ack() noexcept {
        auto pending = std::move(ack);
        if (!pending) {
            return;
        }
        try {
            pending->set_value();
        } catch (...) {
        }
    }

    async_msg(async_msg_type type, logger* worker_ptr, const log_msg& msg)
        : log_msg_buffer(msg)
        , msg_type(type)
        , worker(worker_ptr) {}

    async_msg(async_msg_type type, std::shared_ptr<logger>&& worker_ptr, const log_msg& msg)
        : log_msg_buffer(msg)
        , msg_type(type)
        , worker(worker_ptr.get())
        , keep_alive(std::move(worker_ptr)) {}

    async_msg(async_msg_type type, logger* worker_ptr)
        : msg_type(type)
        , worker(worker_ptr) {}

    async_msg(async_msg_type type, std::shared_ptr<logger>&& worker_ptr)
        : msg_type(type)
        , worker(worker_ptr.get())
        , keep_alive(std::move(worker_ptr)) {}

    explicit async_msg(async_msg_type type)
        : async_msg(type, static_cast<logger*>(nullptr)) {}
};

} // namespace details
} // namespace minispdlog
