#pragma once

#include "minispdlog/sinks/base_sink.h"
#include <fmt/format.h>  // 显式包含 fmt::memory_buffer
#include <vector>
#include <string>
#include <mutex>

namespace minispdlog::tests {

/**
 * @brief 内存捕获型 Sink —— 测试专用的测试替身（Test Double）
 *
 * 解决日志库测试的核心难题：如何自动化断言日志确实被写了？
 *
 * 传统做法（当前项目）：
 *   - 日志写入文件/控制台，测试者用眼睛看输出
 *   - 无法自动化，无法在 CI 中运行
 *
 * 本方案：
 *   - mock_sink 将格式化后的日志内容捕获到内存 vector
 *   - 测试代码直接用 REQUIRE/CHECK 做断言
 *   - 纯内存操作，毫秒级、零副作用、可并行
 *
 * 使用示例：
 *   auto mock = std::make_shared<mock_sink_mt>();
 *   logger lg("test", mock);
 *   lg.info("hello");
 *   REQUIRE(mock->messages().size() == 1);
 *   REQUIRE(mock->last_contains("hello"));
 */
template<typename Mutex = std::mutex>
class mock_sink : public sinks::base_sink<Mutex> {
public:
    mock_sink() = default;

    // 获取已捕获的所有日志内容（格式化后的完整字符串）
    std::vector<std::string> messages() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return messages_;
    }

    // 获取已捕获的日志数量
    size_t message_count() const {
        std::lock_guard<Mutex> lock(this->mutex_);
        return messages_.size();
    }

    // 清空捕获的内容
    void clear() {
        std::lock_guard<Mutex> lock(this->mutex_);
        messages_.clear();
    }

    // 断言某条日志包含指定子串（搜索所有消息）
    bool contains(const std::string& substr) const {
        std::lock_guard<Mutex> lock(this->mutex_);
        for (const auto& msg : messages_) {
            if (msg.find(substr) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // 断言最后一条日志包含指定子串
    bool last_contains(const std::string& substr) const {
        std::lock_guard<Mutex> lock(this->mutex_);
        if (messages_.empty()) return false;
        return messages_.back().find(substr) != std::string::npos;
    }

    // 获取第 idx 条日志内容（越界返回空字符串）
    std::string at(size_t idx) const {
        std::lock_guard<Mutex> lock(this->mutex_);
        if (idx >= messages_.size()) return {};
        return messages_[idx];
    }

protected:
    void sink_it_(const details::log_msg& msg) override {
        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);
        messages_.emplace_back(formatted.data(), formatted.size());
    }

    void flush_() override {
        // 内存操作无需 flush
    }

private:
    std::vector<std::string> messages_;
};

using mock_sink_mt = mock_sink<std::mutex>;
using mock_sink_st = mock_sink<sinks::null_mutex>;

} // namespace minispdlog::tests
