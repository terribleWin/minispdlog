#include <minispdlog/minispdlog.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif

namespace fs = std::filesystem;

// Windows cmd/PowerShell 默认不解析 ANSI；打开 VT 后彩色 sink 才能上色。
static void enable_terminal_color() {
#ifdef _WIN32
    const auto enable = [](DWORD handle_id) {
        HANDLE handle = GetStdHandle(handle_id);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
            return;
        }
        DWORD mode = 0;
        if (!GetConsoleMode(handle, &mode)) {
            return;
        }
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    };
    enable(STD_OUTPUT_HANDLE);
    enable(STD_ERROR_HANDLE);
    SetConsoleOutputCP(CP_UTF8);
#endif
}

static void banner(const char* title) {
    std::cout << "\n==== " << title << " ====\n";
}

static std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static void emit_levels(minispdlog::logger& lg) {
    lg.trace("trace: diagnostic detail");
    lg.debug("debug: developer hint, value={}", 7);
    lg.info("info: service started, user={}", 42);
    lg.warn("warn: retry cache miss");
    lg.error("error: disk quota exceeded");
    lg.critical("critical: cannot continue");
}

// Same messages as the terminal walkthrough (1–4 + 6). Pattern still uses %^ / %$;
// file sinks ignore the span and write plain text.
static void emit_showcase(minispdlog::logger& lg) {
    lg.set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%L%$] %v");
    emit_levels(lg);

    lg.set_pattern("[%n] [%l] %^%v%$");
    lg.info("this sentence is green");
    lg.error("this sentence is red");

    lg.set_pattern("%^[%n] [%L]%$ %v");
    lg.warn("name and level share one color span");

    lg.set_pattern("%^[%n] [%L] %v%$");
    lg.info("the entire line is wrapped in %^ %$");

    lg.set_pattern("[%n] [%L] %v");
    lg.error("no %^ %$ markers: color sink paints the whole line");

    lg.set_pattern("[%Y-%m-%d %H:%M:%S] [%n] [%^%L%$] %v");
    lg.info("same payload as the terminal walkthrough");
    lg.warn("file sinks do not insert ANSI escape codes");
    lg.error("file counterpart of the stderr demo");
}

int main() {
    std::cout << std::unitbuf;
    enable_terminal_color();

    const fs::path dir{"logs"};
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "Cannot create log directory: " << ec.message() << '\n';
        return 1;
    }

    const fs::path dual_file = dir / "demo.color.log";
    const fs::path plain_file = dir / "demo.plain.log";

    try {
        // 1. 终端彩色：默认 pattern 只给级别名上色（%^ … %$）
        banner("1. Terminal color (default: only [level] is colored)");
        auto console = minispdlog::stdout_color_mt("console");
        emit_levels(*console);

        // 2. 局部颜色：只给消息正文上色
        banner("2. Partial color: only the message body");
        console->set_pattern("[%n] [%l] %^%v%$");
        console->info("this sentence is green");
        console->error("this sentence is red");

        // 3. 局部颜色：logger 名和级别共用一段着色区间
        banner("3. Partial color: logger name and level");
        console->set_pattern("%^[%n] [%L]%$ %v");
        console->warn("name and level share one color span");

        // 4. 整行着色：把整行包进 %^ … %$，或省略标记（区间为空则整行着色）
        banner("4. Whole-line color");
        console->set_pattern("%^[%n] [%L] %v%$");
        console->info("the entire line is wrapped in %^ %$");
        console->set_pattern("[%n] [%L] %v");
        console->error("no %^ %$ markers: color sink paints the whole line");

        // 5. stderr 彩色 logger
        banner("5. stderr color logger");
        auto err = minispdlog::stderr_color_mt("stderr");
        err->error("this line is written to stderr");

        // 6. 同一条日志：终端带色，指定文件写入完整展示（file_sink 不插入 ANSI）
        banner("6. Same logger: color on terminal, full showcase in file");
        auto color_sink = std::make_shared<minispdlog::sinks::color_console_sink_mt>();
        auto file_sink =
            std::make_shared<minispdlog::sinks::file_sink_mt>(dual_file.string(), true);
        auto dual = std::make_shared<minispdlog::logger>(
            "dual", minispdlog::logger::sink_list{color_sink, file_sink});
        minispdlog::register_logger(dual);
        emit_showcase(*dual);
        dual->flush();

        // 7. 只写指定文件：把前面相关展示完整落盘（无 ANSI）
        banner("7. File-only logger (specified path, full showcase, no color)");
        auto file = minispdlog::basic_logger_mt("file", plain_file.string(), true);
        emit_showcase(*file);
        file->info("showcase written to {}", plain_file.string());
        file->flush();

        std::cout << "\nWrote:\n  " << dual_file.string() << "\n  " << plain_file.string() << "\n";
        std::cout << "\n--- " << dual_file.string() << " ---\n" << read_all(dual_file);
        std::cout << "--- " << plain_file.string() << " ---\n" << read_all(plain_file);

        minispdlog::drop("console");
        minispdlog::drop("stderr");
        minispdlog::drop("dual");
        minispdlog::drop("file");
        return 0;
    } catch (const std::exception& error) {
        minispdlog::drop("console");
        minispdlog::drop("stderr");
        minispdlog::drop("dual");
        minispdlog::drop("file");
        std::cerr << "color log demo failed: " << error.what() << '\n';
        return 1;
    }
}
