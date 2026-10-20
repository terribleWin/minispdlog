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

    /**
 * @brief 通过 Linux 内核 UDP/TCP Socket 发送格式化日志。
 * @tparam Mutex `std::mutex` 为多线程版本，`null_mutex` 为单线程版本。
 *
 * 可在测试中与 `network_listener` 配对，也可将目标指向 syslog、Vector 或
 * Fluent Bit 等采集端。非 Linux 平台在构造底层客户端时抛出异常。
 */
    template <typename Mutex>
    class network_sink : public base_sink<Mutex> {
    public:
        /** @brief 使用完整网络配置构造 Sink。 */
        explicit network_sink(network_config cfg) : client_(std::move(cfg)) {}

        /** @brief 使用主机、端口与协议构造 Sink。 */
        network_sink(std::string host, std::uint16_t port, network_protocol protocol = network_protocol::udp)
            : network_sink(make_config(std::move(host), port, protocol)) {}

        ~network_sink() override = default;

        network_sink(const network_sink&) = delete;
        network_sink& operator=(const network_sink&) = delete;

        /** @brief 返回只读网络配置。 */
        const network_config& config() const noexcept { return client_.config(); }

        /** @brief 在线程安全保护下查询连接状态。 */
        bool connected() const {
            std::lock_guard<Mutex> lock(this->mutex_);
            return client_.connected();
        }

        /** @brief 返回对端地址。 */
        network_endpoint peer() const {
            std::lock_guard<Mutex> lock(this->mutex_);
            return client_.peer();
        }

        /** @brief 返回本地 Socket 地址。 */
        network_endpoint local() const {
            std::lock_guard<Mutex> lock(this->mutex_);
            return client_.local();
        }

        /** @brief 尝试读取对端回复，无数据时返回零。 */
        std::size_t try_recv(char* data, std::size_t size) {
            std::lock_guard<Mutex> lock(this->mutex_);
            return client_.try_recv(data, size);
        }

        /** @brief 返回成功发送的数据包数量。 */
        std::uint64_t packets_sent() const {
            std::lock_guard<Mutex> lock(this->mutex_);
            return client_.packets_sent();
        }

        /** @brief 返回成功发送的字节数。 */
        std::uint64_t bytes_sent() const {
            std::lock_guard<Mutex> lock(this->mutex_);
            return client_.bytes_sent();
        }

        /** @brief 返回按错误策略丢弃的数据包数量。 */
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
        static network_config make_config(std::string host, std::uint16_t port, network_protocol protocol) {
            network_config cfg;
            cfg.host = std::move(host);
            cfg.port = port;
            cfg.protocol = protocol;
            return cfg;
        }

        network_client client_;
    };

    /** @brief 强制使用 UDP 协议的网络 Sink。 */
    template <typename Mutex>
    class udp_sink : public network_sink<Mutex> {
    public:
        udp_sink(std::string host, std::uint16_t port) : network_sink<Mutex>(udp_config(std::move(host), port)) {}
        explicit udp_sink(network_config cfg)
            : network_sink<Mutex>(force_protocol(std::move(cfg), network_protocol::udp)) {}

    private:
        static network_config force_protocol(network_config cfg, network_protocol protocol) {
            cfg.protocol = protocol;
            return cfg;
        }
    };

    /** @brief 强制使用 TCP 协议的网络 Sink。 */
    template <typename Mutex>
    class tcp_sink : public network_sink<Mutex> {
    public:
        tcp_sink(std::string host, std::uint16_t port) : network_sink<Mutex>(tcp_config(std::move(host), port)) {}
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

} // sinks
} // minispdlog
