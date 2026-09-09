#include <minispdlog/minispdlog.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

int main() {
    const fs::path dir{"logs"};
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "Cannot create log directory: " << ec.message() << '\n';
        return 1;
    }

    try {
        auto file = minispdlog::json_logger_mt("json-file", "logs/demo.json.log", true);
        file->info("hello {}", "json");
        file->warn("path is C:\\temp\\app.log");
        file->error("quote: \"boom\"");
        file->flush();

        auto rotating = minispdlog::rotating_json_logger_mt(
            "json-rotating", "logs/demo.json.rotating.log", 256, 3);
        rotating->set_pattern("[%L] %v");  // ignored: JSON sinks stay JSON Lines
        for (int i = 0; i < 8; ++i) {
            rotating->info("rotated-{}", i);
        }
        rotating->flush();

        minispdlog::drop("json-file");
        minispdlog::drop("json-rotating");
        std::cout << "Wrote JSON Lines to logs/demo.json.log and logs/demo.json.rotating.log\n";
        return 0;
    } catch (const std::exception& error) {
        minispdlog::drop("json-file");
        minispdlog::drop("json-rotating");
        std::cerr << "JSON log demo failed: " << error.what() << '\n';
        return 1;
    }
}
