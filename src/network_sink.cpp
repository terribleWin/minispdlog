#include "minispdlog/network.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef __linux__
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace minispdlog {
namespace {

[[noreturn]] void throw_net(const std::string& what) {
    throw std::runtime_error(what);
}

#ifndef __linux__

[[noreturn]] void throw_not_linux() {
    throw_net("network_sink requires Linux kernel sockets");
}

#endif

#ifdef __linux__

void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

int poll_fd(int fd, short events, int timeout_ms) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;
    for (;;) {
        const int rc = ::poll(&pfd, 1, timeout_ms);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        return rc;
    }
}

int remaining_ms(std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

sockaddr* as_sockaddr(void* addr) {
    return static_cast<sockaddr*>(addr);
}

const sockaddr* as_sockaddr(const void* addr) {
    return static_cast<const sockaddr*>(addr);
}

network_endpoint endpoint_from(const sockaddr* addr, socklen_t len) {
    char host[NI_MAXHOST]{};
    char serv[NI_MAXSERV]{};
    if (::getnameinfo(addr, len, host, sizeof(host), serv, sizeof(serv),
                      NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        return {};
    }
    network_endpoint ep;
    ep.host = host;
    ep.port = static_cast<std::uint16_t>(std::strtoul(serv, nullptr, 10));
    return ep;
}

struct resolved_addr {
    int family{AF_UNSPEC};
    int socktype{0};
    int ipproto{0};
    sockaddr_storage storage{};
    socklen_t len{0};
};

resolved_addr resolve(const std::string& host, std::uint16_t port, network_protocol protocol) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = protocol == network_protocol::udp ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = protocol == network_protocol::udp ? IPPROTO_UDP : IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV;

    addrinfo* raw = nullptr;
    const std::string port_text = std::to_string(port);
    const char* host_ptr = host.empty() ? nullptr : host.c_str();
    const int rc = ::getaddrinfo(host_ptr, port_text.c_str(), &hints, &raw);
    if (rc != 0 || raw == nullptr) {
        throw_net(std::string("getaddrinfo: ") + ::gai_strerror(rc));
    }
    std::unique_ptr<addrinfo, void (*)(addrinfo*)> guard(raw, ::freeaddrinfo);

    resolved_addr out;
    out.family = raw->ai_family;
    out.socktype = raw->ai_socktype;
    out.ipproto = raw->ai_protocol;
    out.len = static_cast<socklen_t>(raw->ai_addrlen);
    std::memcpy(&out.storage, raw->ai_addr, static_cast<std::size_t>(raw->ai_addrlen));
    return out;
}

class unique_fd {
public:
    unique_fd() = default;
    explicit unique_fd(int fd)
        : fd_(fd) {}
    ~unique_fd() { reset(); }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

    unique_fd(unique_fd&& other) noexcept
        : fd_(other.fd_) {
        other.fd_ = -1;
    }

    unique_fd& operator=(unique_fd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const noexcept { return fd_; }

    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    void reset() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_{-1};
};

void set_sock_timeout(int fd, int optname, int timeout_ms) {
    if (timeout_ms < 0) {
        return;
    }
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = static_cast<long>(timeout_ms % 1000) * 1000;
    if (::setsockopt(fd, SOL_SOCKET, optname, &tv, sizeof(tv)) != 0) {
        throw_errno("setsockopt timeout");
    }
}

unique_fd open_socket(const resolved_addr& addr) {
    unique_fd fd(::socket(addr.family, addr.socktype | SOCK_CLOEXEC, addr.ipproto));
    if (fd.get() < 0) {
        throw_errno("socket");
    }
    return fd;
}

void connect_blocking(int fd, const resolved_addr& addr, const network_config& cfg) {
    if (cfg.protocol == network_protocol::udp) {
        if (::connect(fd, as_sockaddr(&addr.storage), addr.len) != 0) {
            throw_errno("connect");
        }
        set_sock_timeout(fd, SO_SNDTIMEO, cfg.send_timeout_ms);
        return;
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throw_errno("fcntl");
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        throw_errno("fcntl");
    }

    const int rc = ::connect(fd, as_sockaddr(&addr.storage), addr.len);
    if (rc != 0 && errno != EINPROGRESS) {
        throw_errno("connect");
    }
    const int ready = poll_fd(fd, POLLOUT, cfg.connect_timeout_ms);
    if (ready <= 0) {
        throw_net("tcp connect timeout");
    }
    int err = 0;
    socklen_t err_len = sizeof(err);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0) {
        throw_errno("getsockopt");
    }
    if (err != 0) {
        errno = err;
        throw_errno("connect");
    }
    if (::fcntl(fd, F_SETFL, flags) != 0) {
        throw_errno("fcntl");
    }
    if (cfg.tcp_nodelay) {
        const int yes = 1;
        if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) != 0) {
            throw_errno("setsockopt TCP_NODELAY");
        }
    }
    set_sock_timeout(fd, SO_SNDTIMEO, cfg.send_timeout_ms);
}

ssize_t send_all(int fd, const char* data, std::size_t size, network_protocol protocol) {
    std::size_t off = 0;
    while (off < size) {
        const ssize_t n = ::send(fd, data + off, size - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            if (protocol == network_protocol::udp) {
                return static_cast<ssize_t>(off);
            }
            errno = EPIPE;
            return -1;
        }
        off += static_cast<std::size_t>(n);
        if (protocol == network_protocol::udp) {
            break;
        }
    }
    return static_cast<ssize_t>(off);
}

std::optional<std::string> pop_line(std::string& buf) {
    const auto pos = buf.find('\n');
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    std::string line = buf.substr(0, pos + 1);
    buf.erase(0, pos + 1);
    return line;
}

#endif

} // namespace

struct network_client::impl {
    network_config cfg{};
#ifdef __linux__
    resolved_addr dest{};
    unique_fd fd;
#endif
    bool connected{false};
    std::uint64_t packets_sent{0};
    std::uint64_t bytes_sent{0};
    std::uint64_t packets_dropped{0};
};

network_client::network_client(network_config cfg)
    : impl_(std::make_unique<impl>()) {
    impl_->cfg = std::move(cfg);
#ifdef __linux__
    impl_->dest = resolve(impl_->cfg.host, impl_->cfg.port, impl_->cfg.protocol);
    try {
        auto fd = open_socket(impl_->dest);
        connect_blocking(fd.get(), impl_->dest, impl_->cfg);
        impl_->fd = std::move(fd);
        impl_->connected = true;
    } catch (...) {
        if (!impl_->cfg.reconnect) {
            throw;
        }
        impl_->fd.reset();
        impl_->connected = false;
    }
#else
    (void)cfg;
    throw_not_linux();
#endif
}

network_client::~network_client() = default;

void network_client::close() noexcept {
#ifdef __linux__
    impl_->fd.reset();
    impl_->connected = false;
#else
    if (impl_) {
        impl_->connected = false;
    }
#endif
}

bool network_client::connected() const noexcept {
    return impl_->connected;
}

const network_config& network_client::config() const noexcept {
    return impl_->cfg;
}

network_endpoint network_client::peer() const {
#ifdef __linux__
    return endpoint_from(as_sockaddr(&impl_->dest.storage), impl_->dest.len);
#else
    throw_not_linux();
#endif
}

network_endpoint network_client::local() const {
#ifdef __linux__
    if (impl_->fd.get() < 0) {
        return {};
    }
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(impl_->fd.get(), as_sockaddr(&addr), &len) != 0) {
        return {};
    }
    return endpoint_from(as_sockaddr(&addr), len);
#else
    throw_not_linux();
#endif
}

std::uint64_t network_client::packets_sent() const noexcept {
    return impl_->packets_sent;
}

std::uint64_t network_client::bytes_sent() const noexcept {
    return impl_->bytes_sent;
}

std::uint64_t network_client::packets_dropped() const noexcept {
    return impl_->packets_dropped;
}

void network_client::send(const char* data, std::size_t size) {
#ifdef __linux__
    if (data == nullptr && size != 0) {
        throw_net("network_client::send null buffer");
    }
    if (impl_->cfg.protocol == network_protocol::tcp && impl_->fd.get() >= 0) {
        char probe = 0;
        for (;;) {
            const ssize_t n = ::recv(impl_->fd.get(), &probe, 1, MSG_DONTWAIT | MSG_PEEK);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                impl_->fd.reset();
                impl_->connected = false;
                break;
            }
            if (n == 0) {
                impl_->fd.reset();
                impl_->connected = false;
                break;
            }
            break;
        }
    }
    if (impl_->cfg.protocol == network_protocol::udp && size > impl_->cfg.max_datagram) {
        ++impl_->packets_dropped;
        if (!impl_->cfg.drop_on_error) {
            throw_net("udp datagram exceeds max_datagram");
        }
        return;
    }

    auto try_send = [&]() -> bool {
        if (impl_->fd.get() < 0 || !impl_->connected) {
            return false;
        }
        const ssize_t n = send_all(impl_->fd.get(), data, size, impl_->cfg.protocol);
        if (n < 0 || (impl_->cfg.protocol == network_protocol::udp &&
                      static_cast<std::size_t>(n) != size)) {
            return false;
        }
        ++impl_->packets_sent;
        impl_->bytes_sent += static_cast<std::uint64_t>(n);
        return true;
    };

    if (try_send()) {
        return;
    }

    impl_->fd.reset();
    impl_->connected = false;

    auto reconnect = [&]() {
        auto fd = open_socket(impl_->dest);
        connect_blocking(fd.get(), impl_->dest, impl_->cfg);
        impl_->fd = std::move(fd);
        impl_->connected = true;
    };

    if (!impl_->cfg.reconnect) {
        ++impl_->packets_dropped;
        if (!impl_->cfg.drop_on_error) {
            throw_net("network send failed");
        }
        return;
    }

    try {
        reconnect();
        if (try_send()) {
            return;
        }
    } catch (...) {
        impl_->fd.reset();
        impl_->connected = false;
        if (!impl_->cfg.drop_on_error) {
            throw;
        }
        ++impl_->packets_dropped;
        return;
    }

    ++impl_->packets_dropped;
    if (!impl_->cfg.drop_on_error) {
        throw_net("network send failed");
    }
#else
    (void)data;
    (void)size;
    throw_not_linux();
#endif
}

std::size_t network_client::try_recv(char* data, std::size_t size) {
#ifdef __linux__
    if (impl_->fd.get() < 0 || data == nullptr || size == 0) {
        return 0;
    }
    for (;;) {
        const ssize_t n = ::recv(impl_->fd.get(), data, size, MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (impl_->cfg.protocol == network_protocol::tcp) {
                impl_->fd.reset();
                impl_->connected = false;
            }
            return 0;
        }
        if (n == 0) {
            impl_->fd.reset();
            impl_->connected = false;
            return 0;
        }
        return static_cast<std::size_t>(n);
    }
#else
    (void)data;
    (void)size;
    throw_not_linux();
#endif
}

struct network_listener::impl {
    network_protocol protocol{network_protocol::udp};
#ifdef __linux__
    unique_fd listen_fd;
    unique_fd conn_fd;
    sockaddr_storage peer{};
    socklen_t peer_len{0};
#endif
    network_endpoint local{};
    network_endpoint last_peer{};
    std::string tcp_buf;
    bool have_peer{false};
};

network_listener::network_listener(network_protocol protocol, std::string bind_host,
                                   std::uint16_t port)
    : impl_(std::make_unique<impl>()) {
    impl_->protocol = protocol;
#ifdef __linux__
    const auto addr = resolve(bind_host, port, protocol);
    auto fd = open_socket(addr);
    const int yes = 1;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
        throw_errno("setsockopt SO_REUSEADDR");
    }
    if (::bind(fd.get(), as_sockaddr(&addr.storage), addr.len) != 0) {
        throw_errno("bind");
    }
    if (protocol == network_protocol::tcp) {
        if (::listen(fd.get(), 8) != 0) {
            throw_errno("listen");
        }
    }
    sockaddr_storage local{};
    socklen_t local_len = sizeof(local);
    if (::getsockname(fd.get(), as_sockaddr(&local), &local_len) != 0) {
        throw_errno("getsockname");
    }
    impl_->local = endpoint_from(as_sockaddr(&local), local_len);
    impl_->listen_fd = std::move(fd);
#else
    (void)bind_host;
    (void)port;
    throw_not_linux();
#endif
}

network_listener::~network_listener() = default;

network_endpoint network_listener::local() const {
    return impl_->local;
}

void network_listener::close() noexcept {
#ifdef __linux__
    impl_->conn_fd.reset();
    impl_->listen_fd.reset();
    impl_->have_peer = false;
    impl_->tcp_buf.clear();
#else
    if (impl_) {
        impl_->have_peer = false;
        impl_->tcp_buf.clear();
    }
#endif
}

void network_listener::disconnect_peer() noexcept {
#ifdef __linux__
    impl_->conn_fd.reset();
    impl_->have_peer = false;
    impl_->tcp_buf.clear();
#else
    if (impl_) {
        impl_->have_peer = false;
        impl_->tcp_buf.clear();
    }
#endif
}

void network_listener::send(const std::string& text) {
    send(text.data(), text.size());
}

void network_listener::send(const char* data, std::size_t size) {
#ifdef __linux__
    if (data == nullptr && size != 0) {
        throw_net("network_listener::send null buffer");
    }
    if (impl_->protocol == network_protocol::udp) {
        if (!impl_->have_peer) {
            throw_net("network_listener::send has no UDP peer");
        }
        const ssize_t n =
            ::sendto(impl_->listen_fd.get(), data, size, MSG_NOSIGNAL, as_sockaddr(&impl_->peer),
                     impl_->peer_len);
        if (n < 0 || static_cast<std::size_t>(n) != size) {
            throw_errno("sendto");
        }
        return;
    }
    if (impl_->conn_fd.get() < 0) {
        throw_net("network_listener::send has no TCP client");
    }
    if (send_all(impl_->conn_fd.get(), data, size, network_protocol::tcp) < 0) {
        throw_errno("send");
    }
#else
    (void)data;
    (void)size;
    throw_not_linux();
#endif
}

std::string network_listener::recv(int timeout_ms) {
    auto line = try_recv(timeout_ms);
    if (!line) {
        throw_net("network_listener: recv timeout");
    }
    return std::move(*line);
}

std::optional<std::string> network_listener::try_recv(int timeout_ms) {
#ifdef __linux__
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);

    if (impl_->protocol == network_protocol::udp) {
        const int wait = remaining_ms(deadline);
        const int ready = poll_fd(impl_->listen_fd.get(), POLLIN, wait);
        if (ready <= 0) {
            return std::nullopt;
        }
        char buf[65536];
        sockaddr_storage peer{};
        socklen_t peer_len = sizeof(peer);
        const ssize_t n =
            ::recvfrom(impl_->listen_fd.get(), buf, sizeof(buf), 0, as_sockaddr(&peer), &peer_len);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                return std::nullopt;
            }
            throw_errno("recvfrom");
        }
        impl_->peer = peer;
        impl_->peer_len = peer_len;
        impl_->last_peer = endpoint_from(as_sockaddr(&peer), peer_len);
        impl_->have_peer = true;
        return std::string(buf, static_cast<std::size_t>(n));
    }

    if (auto line = pop_line(impl_->tcp_buf)) {
        return line;
    }

    while (true) {
        if (impl_->conn_fd.get() < 0) {
            const int wait = remaining_ms(deadline);
            if (timeout_ms >= 0 && wait == 0 &&
                std::chrono::steady_clock::now() >= deadline) {
                return std::nullopt;
            }
            const int ready = poll_fd(impl_->listen_fd.get(), POLLIN, wait);
            if (ready <= 0) {
                return std::nullopt;
            }
            sockaddr_storage peer{};
            socklen_t peer_len = sizeof(peer);
            const int raw = ::accept4(impl_->listen_fd.get(), as_sockaddr(&peer), &peer_len,
                                      SOCK_CLOEXEC);
            if (raw < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                throw_errno("accept");
            }
            impl_->conn_fd = unique_fd(raw);
            impl_->peer = peer;
            impl_->peer_len = peer_len;
            impl_->last_peer = endpoint_from(as_sockaddr(&peer), peer_len);
            impl_->have_peer = true;
        }

        if (auto line = pop_line(impl_->tcp_buf)) {
            return line;
        }

        const int wait = remaining_ms(deadline);
        const int ready = poll_fd(impl_->conn_fd.get(), POLLIN, wait);
        if (ready <= 0) {
            return std::nullopt;
        }
        char buf[4096];
        const ssize_t n = ::recv(impl_->conn_fd.get(), buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            impl_->conn_fd.reset();
            impl_->have_peer = false;
            continue;
        }
        if (n == 0) {
            impl_->conn_fd.reset();
            impl_->have_peer = false;
            if (!impl_->tcp_buf.empty()) {
                std::string leftover = std::move(impl_->tcp_buf);
                impl_->tcp_buf.clear();
                return leftover;
            }
            continue;
        }
        impl_->tcp_buf.append(buf, static_cast<std::size_t>(n));
        if (auto line = pop_line(impl_->tcp_buf)) {
            return line;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
    }
#else
    (void)timeout_ms;
    throw_not_linux();
#endif
}

} // namespace minispdlog
