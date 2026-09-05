// C++20 deprecates free atomic_load/store for shared_ptr; keep them for
// libstdc++ 11 (Ubuntu 22.04) which lacks std::atomic<std::shared_ptr<T>>.
#define _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING
#include "minispdlog/logger.h"

#include <algorithm>
#include <utility>

namespace minispdlog {

logger::logger(std::string name)
    : name_(std::move(name))
    , sinks_(std::make_shared<sink_list>()) {}

logger::logger(std::string name, sinks::sink_ptr single_sink)
    : name_(std::move(name))
    , sinks_(std::make_shared<sink_list>()) {
    if (single_sink) {
        sinks_->push_back(std::move(single_sink));
    }
}

logger::logger(std::string name, sink_list sinks)
    : name_(std::move(name))
    , sinks_(std::make_shared<sink_list>(std::move(sinks))) {}

std::shared_ptr<const logger::sink_list> logger::load_sinks_() const {
    // All concurrent reads of sinks_ go through atomic_load (C++11 shared_ptr atomics).
    return std::atomic_load_explicit(&sinks_, std::memory_order_acquire);
}

void logger::store_sinks_(std::shared_ptr<sink_list> next) {
    std::atomic_store_explicit(&sinks_, std::move(next), std::memory_order_release);
}

void logger::add_sink(sinks::sink_ptr sink) {
    if (!sink) {
        return;
    }
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    auto current = std::atomic_load_explicit(&sinks_, std::memory_order_relaxed);
    auto next = std::make_shared<sink_list>(*current);
    next->push_back(std::move(sink));
    store_sinks_(std::move(next));
}

void logger::remove_sink(sinks::sink_ptr sink) {
    if (!sink) {
        return;
    }
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    auto current = std::atomic_load_explicit(&sinks_, std::memory_order_relaxed);
    auto next = std::make_shared<sink_list>(*current);
    next->erase(std::remove(next->begin(), next->end(), sink), next->end());
    store_sinks_(std::move(next));
}

logger::sink_list logger::sinks() const {
    return *load_sinks_();
}

void logger::set_level(level log_level) {
    level_.store(log_level, std::memory_order_relaxed);
}

level logger::get_level() const {
    return level_.load(std::memory_order_relaxed);
}

bool logger::should_log(level msg_level) const {
    return msg_level >= level_.load(std::memory_order_relaxed);
}

void logger::set_pattern(std::string pattern) {
    auto snapshot = load_sinks_();
    for (const auto& sink : *snapshot) {
        if (sink) {
            sink->set_pattern(pattern);
        }
    }
}

void logger::set_formatter(std::unique_ptr<formatter> new_formatter) {
    if (!new_formatter) {
        return;
    }
    auto snapshot = load_sinks_();
    sinks::sink* last = nullptr;
    for (const auto& sink : *snapshot) {
        if (sink) {
            last = sink.get();
        }
    }
    for (const auto& sink : *snapshot) {
        if (!sink) {
            continue;
        }
        if (sink.get() == last) {
            sink->set_formatter(std::move(new_formatter));
        } else {
            sink->set_formatter(new_formatter->clone());
        }
    }
}

void logger::backend_flush_() {
    auto snapshot = load_sinks_();
    flush_sinks_(*snapshot);
}

void logger::flush() {
    backend_flush_();
}

void logger::flush_on(level log_level) {
    flush_level_.store(log_level, std::memory_order_relaxed);
}

const std::string& logger::name() const {
    return name_;
}

void logger::flush_sinks_(const sink_list& sinks) const {
    for (const auto& sink : sinks) {
        if (sink) {
            sink->flush();
        }
    }
}

void logger::backend_sink_it_(const details::log_msg& msg) {
    auto snapshot = load_sinks_();
    for (const auto& sink : *snapshot) {
        if (sink && sink->should_log(msg.lvl)) {
            sink->log(msg);
        }
    }
    if (msg.lvl >= flush_level_.load(std::memory_order_relaxed)) {
        flush_sinks_(*snapshot);
    }
}

void logger::sink_it_(const details::log_msg& msg) {
    backend_sink_it_(msg);
}

}  // namespace minispdlog
