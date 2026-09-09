#include <minispdlog/minispdlog.h>
#include <minispdlog/network.h>
#include <minispdlog/sinks/network_sink.h>

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

int main() {
    try {
        minispdlog::network_listener udp(minispdlog::network_protocol::udp);
        const auto udp_ep = udp.local();
        auto udp_sink =
            std::make_shared<minispdlog::sinks::udp_sink_mt>(udp_ep.host, udp_ep.port);
        udp_sink->set_pattern("%v");
        auto udp_log = std::make_shared<minispdlog::logger>("udp", udp_sink);
        minispdlog::register_logger(udp_log);
        udp_log->info("hello-udp");
        udp_log->flush();
        const auto udp_line = udp.recv();
        std::cout << "UDP recv: " << udp_line;

        udp.send("ack-udp");
        std::array<char, 32> buf{};
        const auto n = udp_sink->try_recv(buf.data(), buf.size());
        std::cout << "UDP sink recv: " << std::string(buf.data(), n) << '\n';

        minispdlog::network_listener tcp(minispdlog::network_protocol::tcp);
        const auto tcp_ep = tcp.local();
        minispdlog::json_formatter fields;
        fields.add("service", "network-demo").add("env", "dev");
        auto json_log =
            minispdlog::json_tcp_logger_mt("json-tcp", tcp_ep.host, tcp_ep.port, std::move(fields));
        json_log->info("hello {}", "tcp");
        json_log->flush();
        const auto json_line = tcp.recv();
        std::cout << "TCP JSON recv: " << json_line;

        minispdlog::drop("udp");
        minispdlog::drop("json-tcp");
        std::cout << "Network sink demo used kernel UDP+TCP loopback "
                  << udp_ep.host << ':' << udp_ep.port << " and " << tcp_ep.host << ':'
                  << tcp_ep.port << '\n';
        return 0;
    } catch (const std::exception& error) {
        minispdlog::drop("udp");
        minispdlog::drop("json-tcp");
        std::cerr << "network log demo failed: " << error.what() << '\n';
        return 1;
    }
}
