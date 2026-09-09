#pragma once

#include "../batch_config.h"
#include "../details/durable_file.h"
#include "base_sink.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <type_traits>

namespace minispdlog {
namespace sinks {

/// File sink with a front/frozen double buffer, batched fwrite, WAL, and tail salvage.
/// `_mt` releases the sink mutex while the frozen buffer is written so producers can
/// keep filling the new front. `set_pattern` / `set_formatter` work as on `file_sink`.
template <typename Mutex>
class buffered_file_sink : public base_sink<Mutex> {
public:
    explicit buffered_file_sink(const std::string& filename, bool truncate = false,
                                batch_config cfg = {})
        : writer_(filename, truncate, cfg) {}

    ~buffered_file_sink() override {
        try {
            flush();
        } catch (...) {
        }
    }

    buffered_file_sink(const buffered_file_sink&) = delete;
    buffered_file_sink& operator=(const buffered_file_sink&) = delete;

    const std::string& filename() const { return writer_.path(); }

    std::size_t queued_bytes() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return writer_.front_size();
    }

    void log(const details::log_msg& msg) override {
        if constexpr (std::is_same_v<Mutex, std::mutex>) {
            std::unique_lock<std::mutex> lock(this->mutex_);
            auto& formatted = this->format_message(msg);
            writer_.append(formatted.data(), formatted.size());
            drain_commits_(lock);
        } else {
            std::lock_guard<Mutex> lock(this->mutex_);
            auto& formatted = this->format_message(msg);
            writer_.append(formatted.data(), formatted.size());
            writer_.commit_if_needed();
        }
    }

    void flush() override {
        if constexpr (std::is_same_v<Mutex, std::mutex>) {
            std::unique_lock<std::mutex> lock(this->mutex_);
            wait_idle_(lock);
            drain_front_(lock);
        } else {
            std::lock_guard<Mutex> lock(this->mutex_);
            writer_.flush();
        }
    }

protected:
    void sink_it_(const details::log_msg&) override {}
    void flush_() override {}

private:
    void wait_idle_(std::unique_lock<std::mutex>& lock) {
        cv_.wait(lock, [this] { return !writer_.committing(); });
    }

    void drain_front_(std::unique_lock<std::mutex>& lock) {
        const char* data = nullptr;
        std::size_t size = 0;
        if (!writer_.begin_commit(data, size)) {
            writer_.flush();
            return;
        }
        lock.unlock();
        try {
            writer_.write_commit(data, size);
        } catch (...) {
            lock.lock();
            writer_.end_commit();
            cv_.notify_all();
            throw;
        }
        lock.lock();
        writer_.end_commit();
        cv_.notify_all();
    }

    void drain_commits_(std::unique_lock<std::mutex>& lock) {
        for (;;) {
            wait_idle_(lock);
            if (!writer_.should_commit()) {
                return;
            }
            const char* data = nullptr;
            std::size_t size = 0;
            if (!writer_.begin_commit(data, size)) {
                return;
            }
            lock.unlock();
            try {
                writer_.write_commit(data, size);
            } catch (...) {
                lock.lock();
                writer_.end_commit();
                cv_.notify_all();
                throw;
            }
            lock.lock();
            writer_.end_commit();
            cv_.notify_all();
        }
    }

    details::durable_file writer_;
    std::condition_variable cv_{};
};

using buffered_file_sink_mt = buffered_file_sink<std::mutex>;
using buffered_file_sink_st = buffered_file_sink<null_mutex>;

} // namespace sinks
} // namespace minispdlog
