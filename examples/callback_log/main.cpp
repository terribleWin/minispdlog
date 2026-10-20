#include <minispdlog/minispdlog.h>

#include <deque>
#include <iostream>
#include <mutex>
#include <string>

/**
 * @brief 统计日志级别并保留最近错误的业务回调示例。
 *
 * 控制台 Sink 仍输出便于阅读的文本，回调 Sink 作为独立的业务旁路。
 */
struct log_hook {
    std::mutex mu;
    int info = 0;
    int warn = 0;
    int error = 0;
    std::deque<std::string> last_errors;

    void on_log(const minispdlog::details::log_msg& msg, const std::string& formatted) {
        std::lock_guard<std::mutex> lock(mu);
        switch (msg.lvl) {
        case minispdlog::level::warn:
            ++warn;
            break;
        case minispdlog::level::error:
        case minispdlog::level::critical:
            ++error;
            last_errors.push_back(formatted);
            if (last_errors.size() > 3) {
                last_errors.pop_front();
            }
            break;
        default:
            ++info;
            break;
        }
        std::cout << "  [hook] " << minispdlog::level_to_string(msg.lvl) << " payload=\""
                  << std::string(msg.payload.data(), msg.payload.size()) << "\"\n";
    }

    void on_flush() {
        std::lock_guard<std::mutex> lock(mu);
        std::cout << "  [hook] flush summary: info=" << info << " warn=" << warn << " error=" << error << '\n';
        for (const auto& line : last_errors) {
            std::cout << "  [hook] recent error: " << line;
        }
    }
};

int main() {
    log_hook hook;

    auto console = std::make_shared<minispdlog::sinks::color_console_sink_mt>();
    auto callback = std::make_shared<minispdlog::sinks::callback_sink_mt>(
        [&hook](const minispdlog::details::log_msg& msg, const std::string& formatted) { hook.on_log(msg, formatted); },
        [&hook]() { hook.on_flush(); });

    auto lg = std::make_shared<minispdlog::logger>("app", minispdlog::logger::sink_list{console, callback});
    minispdlog::register_logger(lg);
    minispdlog::set_default_logger(lg);

    minispdlog::info("service started, user={}", 42);
    minispdlog::warn("retry cache miss");
    minispdlog::error("disk quota exceeded");
    minispdlog::error("upstream timeout");
    lg->flush();

    minispdlog::drop("app");
    return 0;
}
