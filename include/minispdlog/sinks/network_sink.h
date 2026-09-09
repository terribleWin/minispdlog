#pragma once

#include "../network.h"
#include "base_sink.h"
#include "json_sink.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace minispdlog {
namespace sinks {

// Ships formatted records through Linux kernel UDP or TCP sockets.
// Pair with network_listener in tests, or with syslog/Vector/Fluent Bit.
template <typename Mutex>
class network_sink : public base_sink<Mutex> {
public:
    explicit network_sink(network_config cfg)
        : client_(std::move(cfg)) {}

    network_sink(std::string host, std::uint16_t port,
                 network_protocol protocol = network_protocol::udp)
        : network_sink(make_config(std::move(host), port, protocol)) {}

    ~network_sink() override = default;

    network_sink(const network_sink&) = delete;
    network_sink& operator=(const network_sink&) = delete;

    const network_config& config() const noexcept { return client_.config(); }

    bool connected() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return client_.connected();
    }

    network_endpoint peer() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return client_.peer();
    }

    network_endpoint local() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return client_.local();
    }

    std::size_t try_recv(char* data, std::size_t size) {
        std::lock_guard<Mutex> lock(this->mutex_);
        return client_.try_recv(data, size);
    }

    std::uint64_t packets_sent() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return client_.packets_sent();
    }

    std::uint64_t bytes_sent() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return client_.bytes_sent();
    }

    std::uint64_t packets_dropped() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return client_.packets_dropped();
    }

protected:
    void sink_it_(const details::log_msg& msg) override {
        auto& formatted = this->format_message(msg);
        client_.send(formatted.data(), formatted.size());
    }

    void flush_() override {}

private:
    static network_config make_config(std::string host, std::uint16_t port,
                                      network_protocol protocol) {
        network_config cfg;
        cfg.host = std::move(host);
        cfg.port = port;
        cfg.protocol = protocol;
        return cfg;
    }

    network_client client_;
};

template <typename Mutex>
class udp_sink : public network_sink<Mutex> {
public:
    udp_sink(std::string host, std::uint16_t port)
        : network_sink<Mutex>(udp_config(std::move(host), port)) {}
    explicit udp_sink(network_config cfg)
        : network_sink<Mutex>(force_protocol(std::move(cfg), network_protocol::udp)) {}

private:
    static network_config force_protocol(network_config cfg, network_protocol protocol) {
        cfg.protocol = protocol;
        return cfg;
    }
};

template <typename Mutex>
class tcp_sink : public network_sink<Mutex> {
public:
    tcp_sink(std::string host, std::uint16_t port)
        : network_sink<Mutex>(tcp_config(std::move(host), port)) {}
    explicit tcp_sink(network_config cfg)
        : network_sink<Mutex>(force_protocol(std::move(cfg), network_protocol::tcp)) {}

private:
    static network_config force_protocol(network_config cfg, network_protocol protocol) {
        cfg.protocol = protocol;
        return cfg;
    }
};

template <typename Mutex>
using json_network_sink = json_sink_wrapper<network_sink<Mutex>, Mutex>;
template <typename Mutex>
using json_udp_sink = json_sink_wrapper<udp_sink<Mutex>, Mutex>;
template <typename Mutex>
using json_tcp_sink = json_sink_wrapper<tcp_sink<Mutex>, Mutex>;

using network_sink_mt = network_sink<std::mutex>;
using network_sink_st = network_sink<null_mutex>;
using udp_sink_mt = udp_sink<std::mutex>;
using udp_sink_st = udp_sink<null_mutex>;
using tcp_sink_mt = tcp_sink<std::mutex>;
using tcp_sink_st = tcp_sink<null_mutex>;

using json_network_sink_mt = json_network_sink<std::mutex>;
using json_network_sink_st = json_network_sink<null_mutex>;
using json_udp_sink_mt = json_udp_sink<std::mutex>;
using json_udp_sink_st = json_udp_sink<null_mutex>;
using json_tcp_sink_mt = json_tcp_sink<std::mutex>;
using json_tcp_sink_st = json_tcp_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog
