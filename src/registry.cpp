#include "minispdlog/registry.h"
#include "minispdlog/sinks/color_console_sink.h"
#include <stdexcept>

namespace minispdlog {

registry::registry() {
    auto console_sink = std::make_shared<sinks::color_console_sink_mt>();
    default_logger_ = std::make_shared<logger>("", console_sink);
    default_logger_->set_level(level::info);
}

registry& registry::instance() {
    static registry instance;
    return instance;
}

void registry::register_logger(std::shared_ptr<logger> new_logger) {
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
    std::lock_guard<std::mutex> lock(mutex_);
    loggers_.erase(logger_name);
    if (default_logger_ && default_logger_->name() == logger_name) {
        default_logger_.reset();
    }
}

void registry::drop_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    loggers_.clear();
    default_logger_.reset();
}

std::shared_ptr<logger> registry::default_logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!default_logger_) {
        default_logger_ = std::make_shared<logger>("", std::make_shared<sinks::color_console_sink_mt>());
        default_logger_->set_level(level::info);
    }
    return default_logger_;
}

void registry::set_default_logger(std::shared_ptr<logger> new_default_logger) {
    std::lock_guard<std::mutex> lock(mutex_);
    default_logger_ = std::move(new_default_logger);
}

void registry::set_level(level log_level) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (default_logger_) {
        default_logger_->set_level(log_level);
    }
    for (auto& pair : loggers_) {
        pair.second->set_level(log_level);
    }
}

void registry::flush_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (default_logger_) {
        default_logger_->flush();
    }
    for (auto& pair : loggers_) {
        pair.second->flush();
    }
}

void registry::throw_if_exists(const std::string& logger_name) {
    if (loggers_.find(logger_name) != loggers_.end()) {
        throw std::runtime_error("Logger with name '" + logger_name + "' already exists.");
    }
}

} // namespace minispdlog
