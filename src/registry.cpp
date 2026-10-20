#include "minispdlog/registry.h"
#include "minispdlog/async.h"
#include "minispdlog/sinks/color_console_sink.h"
#include <stdexcept>
#include <vector>
namespace minispdlog {
registry::registry() {
    auto console_sink = std::make_shared<sinks::color_console_sink_mt>();
    auto initial = std::make_shared<logger>("", console_sink);
    initial->set_level(level::info);
    default_logger_.store(std::move(initial), std::memory_order_relaxed);
}

registry& registry::instance() {
    static registry instance;
    return instance;
}

void registry::register_logger(std::shared_ptr<logger> new_logger) {
    if (!new_logger) {
        throw std::invalid_argument("registry: logger must not be null");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    throw_if_exists(new_logger->name());
    loggers_[new_logger->name()] = std::move(new_logger);
}

std::shared_ptr<logger> registry::get(const std::string& logger_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loggers_.find(logger_name);
    if (it != loggers_.end()) {
        return it->second;
    }
    return nullptr;
}

void registry::drop(const std::string& logger_name) {
    std::shared_ptr<logger> retired_logger;
    std::shared_ptr<logger> current_default;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_default = default_logger_.load(std::memory_order_acquire);
        const bool is_default_logger = current_default && current_default->name() == logger_name;
        const auto it = loggers_.find(logger_name);
        if (it != loggers_.end()) {
            retired_logger = std::move(it->second);
            loggers_.erase(it);
        }
        if (is_default_logger) {
            default_logger_.compare_exchange_strong(current_default, std::shared_ptr<logger>{},
                                                    std::memory_order_acq_rel, std::memory_order_acquire);
        }
    }
    /// Logger 析构可能刷新异步队列或执行用户回调，必须发生在注册表锁外。
}

void registry::drop_all() {
    decltype(loggers_) retired_loggers;
    std::shared_ptr<logger> retired_default;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retired_loggers.swap(loggers_);
        retired_default = default_logger_.exchange({}, std::memory_order_acq_rel);
    }
    /// 容器及默认 Logger 在锁外释放，允许析构路径安全重入注册表。
}

std::shared_ptr<logger> registry::default_logger() {
    return default_logger_.load(std::memory_order_acquire);
}

void registry::set_default_logger(std::shared_ptr<logger> new_default_logger) {
    default_logger_.store(std::move(new_default_logger), std::memory_order_release);
}

std::shared_ptr<logger> registry::set_default_logger_if_empty(std::shared_ptr<logger> candidate) {
    std::shared_ptr<logger> expected;
    if (default_logger_.compare_exchange_strong(expected, candidate, std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
        return candidate;
    }
    return expected;
}

void registry::set_level(level log_level) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto current_default = default_logger_.load(std::memory_order_acquire);
    if (current_default) {
        current_default->set_level(log_level);
    }
    for (auto& pair : loggers_) {
        pair.second->set_level(log_level);
    }
}

void registry::flush_all() {
    std::vector<std::shared_ptr<logger>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto current_default = default_logger_.load(std::memory_order_acquire);
        if (current_default) {
            snapshot.push_back(std::move(current_default));
        }
        snapshot.reserve(snapshot.size() + loggers_.size());
        for (auto& pair : loggers_) {
            snapshot.push_back(pair.second);
        }
    }
    for (auto& lg : snapshot) {
        if (lg) {
            lg->flush();
        }
    }
}

void registry::throw_if_exists(const std::string& logger_name) {
    if (loggers_.find(logger_name) != loggers_.end()) {
        throw std::runtime_error("Logger with name '" + logger_name + "' already exists.");
    }
}

void shutdown() {
    registry::instance().flush_all();
    thread_pool_manager::instance().shutdown();
    registry::instance().drop_all();
}
} ///< 结束 minispdlog 命名空间。
