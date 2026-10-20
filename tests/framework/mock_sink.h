#pragma once

#include "minispdlog/sinks/base_sink.h"
#include <fmt/format.h>

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace minispdlog::tests {

/**
 * @brief 在内存中保存已格式化日志的测试 Sink。
 *
 * 该测试替身不访问文件或控制台，便于测试代码直接断言文本、
 * 写入次数与刷新次数。`Mutex` 决定它是否可供多线程共享。
 */
template <typename Mutex = std::mutex>
class mock_sink : public sinks::base_sink<Mutex> {
public:
    mock_sink() = default;

    /** @brief 返回已捕获文本的快照。 */
    [[nodiscard]] std::vector<std::string> messages() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return messages_;
    }

    /** @brief 返回已捕获的日志数量。 */
    [[nodiscard]] std::size_t message_count() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return messages_.size();
    }

    /** @brief 返回 Sink 收到的刷新次数。 */
    [[nodiscard]] std::size_t flush_count() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return flush_count_;
    }

    /** @brief 清空已捕获的日志，但保留刷新计数。 */
    void clear() {
        std::lock_guard<Mutex> lock(this->mutex_);
        messages_.clear();
    }

    /** @brief 判断任意一条已捕获日志是否包含子串。 */
    [[nodiscard]] bool contains(const std::string& substr) const {
        std::lock_guard<Mutex> lock(this->mutex_);
        for (const auto& msg : messages_) {
            if (msg.find(substr) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    /** @brief 判断最后一条日志是否包含子串。 */
    [[nodiscard]] bool last_contains(const std::string& substr) const {
        std::lock_guard<Mutex> lock(this->mutex_);
        if (messages_.empty())
            return false;
        return messages_.back().find(substr) != std::string::npos;
    }

    /** @brief 返回指定日志；索引越界时返回空字符串。 */
    [[nodiscard]] std::string at(std::size_t idx) const {
        std::lock_guard<Mutex> lock(this->mutex_);
        if (idx >= messages_.size())
            return {};
        return messages_[idx];
    }

protected:
    void sink_it_(const details::log_msg& msg) override {
        auto& formatted = this->format_message(msg);
        messages_.emplace_back(formatted.data(), formatted.size());
    }

    void flush_() override { ++flush_count_; }

private:
    std::vector<std::string> messages_;
    std::size_t flush_count_{0};
};

using mock_sink_mt = mock_sink<std::mutex>;
using mock_sink_st = mock_sink<sinks::null_mutex>;

} ///< 结束 minispdlog::tests 命名空间。
