#pragma once

#include "common.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace minispdlog {

enum class network_protocol { udp, tcp };

struct network_endpoint {
    std::string host;
    std::uint16_t port{0};
};

struct network_config {
    std::string host{"127.0.0.1"};
    std::uint16_t port{514};
    network_protocol protocol{network_protocol::udp};
    bool tcp_nodelay{true};
    bool reconnect{true};
    bool drop_on_error{true};
    int send_timeout_ms{2000};
    int connect_timeout_ms{2000};
    std::size_t max_datagram{65507};
};

inline constexpr bool network_sink_supported() noexcept {
#ifdef MINISPDLOG_LINUX
    return true;
#else
    return false;
#endif
}

inline network_config udp_config(std::string host, std::uint16_t port) {
    network_config cfg;
    cfg.host = std::move(host);
    cfg.port = port;
    cfg.protocol = network_protocol::udp;
    return cfg;
}

inline network_config tcp_config(std::string host, std::uint16_t port) {
    network_config cfg;
    cfg.host = std::move(host);
    cfg.port = port;
    cfg.protocol = network_protocol::tcp;
    return cfg;
}

// Connected UDP or TCP client that talks to the kernel network stack
// (socket/connect/send/recv). Linux only; other platforms throw in the ctor.
class MINISPDLOG_API network_client {
public:
    explicit network_client(network_config cfg);
    ~network_client();

    network_client(const network_client&) = delete;
    network_client& operator=(const network_client&) = delete;
    network_client(network_client&&) = delete;
    network_client& operator=(network_client&&) = delete;

    void send(const char* data, std::size_t size);
    std::size_t try_recv(char* data, std::size_t size);
    void close() noexcept;
    bool connected() const noexcept;
    const network_config& config() const noexcept;
    network_endpoint peer() const;
    network_endpoint local() const;
    std::uint64_t packets_sent() const noexcept;
    std::uint64_t bytes_sent() const noexcept;
    std::uint64_t packets_dropped() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

// Loopback / collector helper: bind() + recv()/send() on a real kernel socket.
class MINISPDLOG_API network_listener {
public:
    explicit network_listener(network_protocol protocol, std::string bind_host = "127.0.0.1",
                              std::uint16_t port = 0);
    ~network_listener();

    network_listener(const network_listener&) = delete;
    network_listener& operator=(const network_listener&) = delete;
    network_listener(network_listener&&) = delete;
    network_listener& operator=(network_listener&&) = delete;

    network_endpoint local() const;
    std::optional<std::string> try_recv(int timeout_ms);
    std::string recv(int timeout_ms = 2000);
    void send(const char* data, std::size_t size);
    void send(const std::string& text);
    void disconnect_peer() noexcept;
    void close() noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace minispdlog
