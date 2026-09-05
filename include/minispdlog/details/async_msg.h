#pragma once

#include "log_msg.h"
#include <future>
#include <memory>
#include <string>

namespace minispdlog {

class logger;

namespace details {

enum class async_msg_type {
    log,
    flush,
    terminate
};

struct log_msg_buffer : log_msg {
    std::string buffer;

    log_msg_buffer() = default;

    log_msg_buffer(log_msg_buffer&& other) noexcept
        : log_msg(std::move(other))
        , buffer(std::move(other.buffer)) {
        payload = string_view_t(buffer);
    }

    log_msg_buffer& operator=(log_msg_buffer&& other) noexcept {
        static_cast<log_msg&>(*this) = std::move(other);
        buffer = std::move(other.buffer);
        payload = string_view_t(buffer);
        return *this;
    }

    log_msg_buffer(const log_msg_buffer&) = delete;
    log_msg_buffer& operator=(const log_msg_buffer&) = delete;

    explicit log_msg_buffer(const log_msg& msg)
        : log_msg(msg)
        , buffer(msg.payload.data(), msg.payload.size()) {
        payload = string_view_t(buffer);
    }
};

struct async_msg : log_msg_buffer {
    async_msg_type msg_type{async_msg_type::log};
    std::shared_ptr<logger> worker_ptr;
    std::shared_ptr<std::promise<void>> ack;

    async_msg() = default;
    async_msg(async_msg&& other) noexcept = default;
    async_msg& operator=(async_msg&& other) noexcept = default;
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

    async_msg(async_msg_type type, std::shared_ptr<logger>&& worker, const log_msg& msg)
        : log_msg_buffer(msg)
        , msg_type(type)
        , worker_ptr(std::move(worker)) {}

    async_msg(async_msg_type the_type, std::shared_ptr<logger>&& worker)
        : log_msg_buffer{}
        , msg_type{the_type}
        , worker_ptr{std::move(worker)} {}

    explicit async_msg(async_msg_type the_type)
        : async_msg{the_type, nullptr} {}
};

} // namespace details
} // namespace minispdlog
