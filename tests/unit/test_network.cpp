#include "framework/doctest.h"
#include "minispdlog/async.h"
#include "minispdlog/minispdlog.h"
#include "minispdlog/network.h"
#include "minispdlog/pattern_formatter.h"
#include "minispdlog/sinks/network_sink.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace minispdlog;

#ifndef MINISPDLOG_LINUX

TEST_CASE("network_sink requires Linux [sink][network]") {
    REQUIRE_FALSE(network_sink_supported());
    REQUIRE_THROWS_AS(network_listener{network_protocol::udp}, std::runtime_error);
    REQUIRE_THROWS_AS(sinks::udp_sink_st("127.0.0.1", 9), std::runtime_error);
    REQUIRE_THROWS_AS(sinks::tcp_sink_st("127.0.0.1", 9), std::runtime_error);
}

#else

TEST_CASE("network_sink is available on Linux [sink][network]") {
    REQUIRE(network_sink_supported());
}

TEST_CASE("udp sink send is received through kernel loopback [sink][network][udp]") {
    network_listener listener(network_protocol::udp);
    REQUIRE(listener.local().port != 0);

    auto sink = std::make_shared<sinks::udp_sink_st>(listener.local().host, listener.local().port);
    sink->set_formatter(std::make_unique<pattern_formatter>("%v"));
    logger lg("udp", sink);
    lg.info("hello-udp");

    const auto line = listener.recv();
    REQUIRE(line == "hello-udp\n");
    REQUIRE(sink->packets_sent() == 1);
    REQUIRE(sink->bytes_sent() == line.size());
    REQUIRE(sink->connected());
    REQUIRE(sink->local().port != 0);
    REQUIRE(sink->peer().port == listener.local().port);
}

TEST_CASE("udp sink try_recv gets kernel reply from listener [sink][network][udp]") {
    network_listener listener(network_protocol::udp);
    auto sink = std::make_shared<sinks::udp_sink_st>(listener.local().host, listener.local().port);
    sink->set_formatter(std::make_unique<pattern_formatter>("%v"));
    logger lg("udp-ack", sink);
    lg.info("ping");
    REQUIRE(listener.recv() == "ping\n");

    listener.send("pong");
    std::array<char, 16> buf{};
    const auto n = sink->try_recv(buf.data(), buf.size());
    REQUIRE(n == 4);
    REQUIRE(std::string(buf.data(), n) == "pong");
}

TEST_CASE("tcp sink send/recv through kernel loopback [sink][network][tcp]") {
    network_listener listener(network_protocol::tcp);
    auto sink = std::make_shared<sinks::tcp_sink_st>(listener.local().host, listener.local().port);
    sink->set_formatter(std::make_unique<pattern_formatter>("%v"));
    logger lg("tcp", sink);
    lg.warn("hello-tcp");

    const auto line = listener.recv();
    REQUIRE(line == "hello-tcp\n");
    REQUIRE(sink->connected());

    listener.send("ack-tcp");
    std::array<char, 32> buf{};
    const auto n = sink->try_recv(buf.data(), buf.size());
    REQUIRE(n == 7);
    REQUIRE(std::string(buf.data(), n) == "ack-tcp");
}

TEST_CASE("tcp sink reconnects after peer disconnect [sink][network][tcp]") {
    network_listener listener(network_protocol::tcp);
    auto sink = std::make_shared<sinks::tcp_sink_st>(listener.local().host, listener.local().port);
    sink->set_formatter(std::make_unique<pattern_formatter>("%v"));
    logger lg("tcp-re", sink);

    lg.info("first");
    REQUIRE(listener.recv() == "first\n");
    listener.disconnect_peer();
    bool peer_closed = false;
    for (int i = 0; i < 100; ++i) {
        char probe = 0;
        (void)sink->try_recv(&probe, 1);
        if (!sink->connected()) {
            peer_closed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer_closed);

    lg.info("second");
    REQUIRE(listener.recv() == "second\n");
    REQUIRE(sink->packets_sent() == 2);
}

TEST_CASE("json udp sink ships NDJSON over UDP [sink][network][json][udp]") {
    network_listener listener(network_protocol::udp);
    json_formatter fields;
    fields.add("service", "net-test");
    auto sink = std::make_shared<sinks::json_udp_sink_st>(listener.local().host, listener.local().port);
    sink->json() = std::move(fields);
    logger lg("json-udp", sink);
    lg.info("wired");

    const auto line = listener.recv();
    REQUIRE(line.front() == '{');
    REQUIRE(line.find("\"msg\":\"wired\"") != std::string::npos);
    REQUIRE(line.find("\"service\":\"net-test\"") != std::string::npos);
    REQUIRE(line.find("\"level\":\"info\"") != std::string::npos);
    REQUIRE(line.back() == '\n');
}

TEST_CASE("json tcp sink ignores set_pattern [sink][network][json][tcp]") {
    network_listener listener(network_protocol::tcp);
    auto sink = std::make_shared<sinks::json_tcp_sink_st>(listener.local().host, listener.local().port);
    logger lg("json-tcp", sink);
    lg.set_pattern("[%L] %v");
    lg.error("still-json");

    const auto line = listener.recv();
    REQUIRE(line.find("\"msg\":\"still-json\"") != std::string::npos);
    REQUIRE(line.find("[error]") == std::string::npos);
}

TEST_CASE("udp_logger_mt factory registers and ships [sink][network][registry][udp]") {
    network_listener listener(network_protocol::udp);
    auto lg = udp_logger_mt("udp-factory", listener.local().host, listener.local().port);
    lg->set_pattern("%v");
    lg->info("from-factory");
    REQUIRE(get("udp-factory") != nullptr);
    REQUIRE(listener.recv() == "from-factory\n");
    drop("udp-factory");
}

TEST_CASE("tcp_logger_st factory ships a line [sink][network][registry][tcp]") {
    network_listener listener(network_protocol::tcp);
    auto lg = tcp_logger_st("tcp-factory", listener.local().host, listener.local().port);
    lg->set_pattern("%v");
    lg->error("tcp-factory-line");
    REQUIRE(listener.recv() == "tcp-factory-line\n");
    drop("tcp-factory");
}

TEST_CASE("json_udp_logger_mt factory ships JSON [sink][network][json][registry]") {
    network_listener listener(network_protocol::udp);
    json_formatter fmt;
    fmt.add("env", "test");
    auto lg = json_udp_logger_mt("json-udp-factory", listener.local().host, listener.local().port,
                                 std::move(fmt));
    lg->info("factory-json");
    const auto line = listener.recv();
    REQUIRE(line.find("\"msg\":\"factory-json\"") != std::string::npos);
    REQUIRE(line.find("\"env\":\"test\"") != std::string::npos);
    drop("json-udp-factory");
}

TEST_CASE("udp sink drops oversized datagram [sink][network][udp]") {
    network_listener listener(network_protocol::udp);
    auto cfg = udp_config(listener.local().host, listener.local().port);
    cfg.max_datagram = 4;
    auto sink = std::make_shared<sinks::udp_sink_st>(cfg);
    sink->set_formatter(std::make_unique<pattern_formatter>("%v"));
    logger lg("drop", sink);
    lg.info("too-big");
    REQUIRE(sink->packets_dropped() >= 1);
    REQUIRE_FALSE(listener.try_recv(50).has_value());
}

TEST_CASE("invalid network host throws [sink][network]") {
    REQUIRE_THROWS(sinks::udp_sink_st("256.256.256.256", 9));
}

TEST_CASE("async_udp_mt ships after flush [sink][network][async][udp]") {
    network_listener listener(network_protocol::udp);
    shutdown();
    init_thread_pool(256, 1);
    auto lg = async_udp_mt("async-udp", listener.local().host, listener.local().port);
    lg->set_pattern("%v");
    lg->info("async-udp-line");
    lg->flush();
    shutdown();
    REQUIRE(listener.recv() == "async-udp-line\n");
}

TEST_CASE("async_json_tcp_mt ships JSON after flush [sink][network][async][json][tcp]") {
    network_listener listener(network_protocol::tcp);
    shutdown();
    init_thread_pool(256, 1);
    auto lg = async_json_tcp_mt("async-json-tcp", listener.local().host, listener.local().port);
    lg->warn("async-json");
    lg->flush();
    shutdown();
    const auto line = listener.recv();
    REQUIRE(line.find("\"msg\":\"async-json\"") != std::string::npos);
    REQUIRE(line.find("\"level\":\"warn\"") != std::string::npos);
}

TEST_CASE("udp sink mt concurrent send is received [sink][network][udp][stress]") {
    network_listener listener(network_protocol::udp);
    auto sink = std::make_shared<sinks::udp_sink_mt>(listener.local().host, listener.local().port);
    sink->set_formatter(std::make_unique<pattern_formatter>("%v"));
    logger lg("udp-mt", sink);

    constexpr int kThreads = 4;
    constexpr int kEach = 25;
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(kThreads));
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&lg, t] {
            for (int i = 0; i < kEach; ++i) {
                lg.info("t{}-i{}", t, i);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    std::size_t got = 0;
    while (got < static_cast<std::size_t>(kThreads * kEach)) {
        const auto line = listener.try_recv(2000);
        REQUIRE(line.has_value());
        REQUIRE(line->find("t") != std::string::npos);
        ++got;
    }
    REQUIRE(sink->packets_sent() == static_cast<std::uint64_t>(kThreads * kEach));
}

TEST_CASE("network_logger_mt uses explicit tcp config [sink][network][tcp]") {
    network_listener listener(network_protocol::tcp);
    auto cfg = tcp_config(listener.local().host, listener.local().port);
    auto lg = network_logger_mt("net-cfg", cfg);
    lg->set_pattern("%v");
    lg->info("cfg-tcp");
    REQUIRE(listener.recv() == "cfg-tcp\n");
    drop("net-cfg");
}

#endif
