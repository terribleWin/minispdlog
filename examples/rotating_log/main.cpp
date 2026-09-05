#include <minispdlog/logger.h>
#include <minispdlog/registry.h>
#include <minispdlog/sinks/rotating_file_sink.h>

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using rotating_sink = minispdlog::sinks::rotating_file_sink_mt;

namespace {

constexpr std::size_t kMaxFileSize = 1024; // 1 KiB，仅用于演示快速轮转
constexpr std::size_t kMaxFiles = 3;       // 最多保留 3 个归档文件
constexpr int kMessageCount = 40;
constexpr std::size_t kPayloadSize = 80;

const fs::path kLogDirectory{"logs"};
const std::string kLogFile = "logs/demo_rotating.log";
const std::string kLoggerName = "rotating-demo";

void prepare_log_directory() {
    std::error_code ec;
    fs::create_directories(kLogDirectory, ec);
    if (ec) {
        throw std::runtime_error("Cannot create log directory: " + ec.message());
    }
}

void remove_old_logs() {
    for (std::size_t index = 0; index <= kMaxFiles; ++index) {
        const fs::path filename = rotating_sink::calc_filename(kLogFile, index);

        std::error_code ec;
        const bool removed = fs::remove(filename, ec);
        if (ec) {
            throw std::runtime_error(
                "Cannot remove old log '" + filename.string() + "': " + ec.message());
        }

        if (removed) {
            std::cout << "Removed old log: " << filename << '\n';
        }
    }
}

void print_log_files() {
    std::cout << "\nGenerated rotating log files:\n";

    for (std::size_t index = 0; index <= kMaxFiles; ++index) {
        const fs::path filename = rotating_sink::calc_filename(kLogFile, index);

        std::error_code ec;
        const auto size = fs::file_size(filename, ec);
        if (!ec) {
            std::cout << "  " << filename << " (" << size << " bytes)\n";
        } else if (ec != std::errc::no_such_file_or_directory) {
            std::cerr << "Cannot inspect '" << filename << "': "
                      << ec.message() << '\n';
        }
    }
}

} // namespace

int main() {
    bool logger_registered = false;

    try {
        prepare_log_directory();
        remove_old_logs();

        auto sink = std::make_shared<rotating_sink>(
            kLogFile, kMaxFileSize, kMaxFiles);
        auto logger = std::make_shared<minispdlog::logger>(kLoggerName, sink);

        minispdlog::registry::instance().register_logger(logger);
        logger_registered = true;

        const std::string payload(kPayloadSize, 'X');
        for (int index = 0; index < kMessageCount; ++index) {
            logger->info("{} message #{:02}", payload, index);
        }

        // 文件统计前必须刷新，确保缓冲内容已经写入磁盘。
        logger->flush();
        print_log_files();

        minispdlog::registry::instance().drop(kLoggerName);
        return 0;
    } catch (const std::exception& error) {
        if (logger_registered) {
            minispdlog::registry::instance().drop(kLoggerName);
        }

        std::cerr << "Rotating log demo failed: " << error.what() << '\n';
        return 1;
    }
}