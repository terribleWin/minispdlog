#include <minispdlog/details/durable_file.h>
#include <minispdlog/minispdlog.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main() {
    const fs::path dir{"logs"};
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "Cannot create log directory: " << ec.message() << '\n';
        return 1;
    }

    minispdlog::install_crash_flush();

    const fs::path live = dir / "demo.buffered.log";
    const fs::path salvaged = dir / "demo.salvaged.log";
    const fs::path from_wal = dir / "demo.wal.log";

    try {
        minispdlog::batch_config cfg;
        cfg.max_bytes = 64;
        cfg.max_records = 4;
        cfg.max_age = std::chrono::milliseconds{0};
        cfg.commit = minispdlog::durability::fflush;
        cfg.recover = true;

        auto live_log = minispdlog::buffered_logger_mt("buffered", live.string(), true, cfg);
        live_log->set_pattern("%v");
        for (int i = 0; i < 10; ++i) {
            live_log->info("batch-{}", i);
        }
        live_log->flush();
        minispdlog::drop("buffered");

        {
            std::ofstream torn(salvaged, std::ios::binary | std::ios::trunc);
            torn << "line-ok\npartial-without-newline";
        }
        auto salvaged_log =
            minispdlog::buffered_logger_mt("salvaged", salvaged.string(), false, cfg);
        salvaged_log->set_pattern("%v");
        salvaged_log->info("after-salvage");
        salvaged_log->flush();
        minispdlog::drop("salvaged");

        {
            std::ofstream base(from_wal, std::ios::binary | std::ios::trunc);
            base << "keep\n";
        }
        minispdlog::details::write_wal_sidecar(from_wal.string(), 5, "from-wal\n");
        auto wal_log = minispdlog::buffered_logger_mt("from-wal", from_wal.string(), false, cfg);
        wal_log->set_pattern("%v");
        wal_log->info("after-wal");
        wal_log->flush();
        minispdlog::drop("from-wal");

        std::cout << "buffered log:\n" << read_all(live);
        std::cout << "salvaged log:\n" << read_all(salvaged);
        std::cout << "wal replay log:\n" << read_all(from_wal);
        return 0;
    } catch (const std::exception& error) {
        minispdlog::drop("buffered");
        minispdlog::drop("salvaged");
        minispdlog::drop("from-wal");
        std::cerr << "buffered log demo failed: " << error.what() << '\n';
        return 1;
    }
}
