#pragma once

#include "common.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace minispdlog {

/** @brief 网络日志使用的传输协议。 */
enum class network_protocol {
    udp, ///< 保留数据报边界，不保证到达。
    tcp  ///< 可靠字节流，断线时可按配置重连。
};

/** @brief 已解析的主机地址与端口。 */
struct network_endpoint {
    std::string host;      ///< 数字地址或主机名。
    std::uint16_t port{0}; ///< 主机字节序端口。
};

/** @brief 网络客户端的连接、超时和错误策略。 */
struct network_config {
    std::string host{"127.0.0.1"};                    ///< 目标主机。
    std::uint16_t port{514};                          ///< 目标端口。
    network_protocol protocol{network_protocol::udp}; ///< 传输协议。
    bool tcp_nodelay{true};                           ///< TCP 是否禁用 Nagle 算法。
    bool reconnect{true};                             ///< TCP 发送失败后是否尝试重连。
    bool drop_on_error{true};                         ///< 发送错误时丢弃而非向调用方抛出。
    int send_timeout_ms{2000};                        ///< 发送超时，单位毫秒。
    int connect_timeout_ms{2000};                     ///< 建连超时，单位毫秒。
    std::size_t max_datagram{65507};                  ///< UDP 单个数据报的最大字节数。
};

/** @brief 返回当前平台是否实现真实内核网络 Sink。 */
inline constexpr bool network_sink_supported() noexcept {
#ifdef MINISPDLOG_LINUX
    return true;
#else
    return false;
#endif
}

/** @brief 生成 UDP 客户端配置。 */
inline network_config udp_config(std::string host, std::uint16_t port) {
    network_config cfg;
    cfg.host = std::move(host);
    cfg.port = port;
    cfg.protocol = network_protocol::udp;
    return cfg;
}

/** @brief 生成 TCP 客户端配置。 */
inline network_config tcp_config(std::string host, std::uint16_t port) {
    network_config cfg;
    cfg.host = std::move(host);
    cfg.port = port;
    cfg.protocol = network_protocol::tcp;
    return cfg;
}

/**
 * @brief 直接使用内核 Socket 的 UDP/TCP 客户端。
 * @throws std::runtime_error 当前仅 Linux 提供实现；其他平台构造时抛出。
 * @par 线程安全性
 * 由外层 Sink 串行化调用；同一实例不支持无锁并发操作。
 */
class MINISPDLOG_API network_client {
public:
    explicit network_client(network_config cfg);
    ~network_client();

    network_client(const network_client&) = delete;
    network_client& operator=(const network_client&) = delete;
    network_client(network_client&&) = delete;
    network_client& operator=(network_client&&) = delete;

    /** @brief 发送一段完整日志字节。 */
    void send(const char* data, std::size_t size);
    /** @brief 非阻塞地读取对端回复；无数据时返回零。 */
    std::size_t try_recv(char* data, std::size_t size);
    /** @brief 关闭 Socket；可重复调用。 */
    void close() noexcept;
    /** @brief 判断底层 Socket 是否处于可用状态。 */
    bool connected() const noexcept;
    /** @brief 返回构造时采用的配置。 */
    const network_config& config() const noexcept;
    /** @brief 返回对端地址。 */
    network_endpoint peer() const;
    /** @brief 返回本地绑定地址。 */
    network_endpoint local() const;
    /** @brief 返回成功发送的数据包数量。 */
    std::uint64_t packets_sent() const noexcept;
    /** @brief 返回成功发送的总字节数。 */
    std::uint64_t bytes_sent() const noexcept;
    /** @brief 返回因发送错误而丢弃的数据包数量。 */
    std::uint64_t packets_dropped() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

/**
 * @brief 基于真实内核 Socket 的轻量收集端与回环测试辅助器。
 * @throws std::runtime_error 当前仅 Linux 提供实现；其他平台构造时抛出。
 */
class MINISPDLOG_API network_listener {
public:
    explicit network_listener(network_protocol protocol, std::string bind_host = "127.0.0.1", std::uint16_t port = 0);
    ~network_listener();

    network_listener(const network_listener&) = delete;
    network_listener& operator=(const network_listener&) = delete;
    network_listener(network_listener&&) = delete;
    network_listener& operator=(network_listener&&) = delete;

    /** @brief 返回监听器实际绑定的地址；端口为零时可据此取得系统分配端口。 */
    network_endpoint local() const;
    /** @brief 在指定毫秒内尝试接收一条记录，超时返回空值。 */
    std::optional<std::string> try_recv(int timeout_ms);
    /** @brief 接收一条记录，超时或网络错误时抛出异常。 */
    std::string recv(int timeout_ms = 2000);
    /** @brief 向最近的 UDP 发送方或已连接 TCP 对端发送字节。 */
    void send(const char* data, std::size_t size);
    /** @overload */
    void send(const std::string& text);
    /** @brief 主动断开当前 TCP 对端，便于验证重连路径。 */
    void disconnect_peer() noexcept;
    /** @brief 关闭监听与已连接 Socket；可重复调用。 */
    void close() noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // minispdlog
