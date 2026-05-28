/*实现sink的抽象基类*/
#pragma once
#include "../common.h"
#include "../details/log_msg.h"
#include <mutex>
#include <memory>
namespace minispdlog {
    namespace sinks {
class sink {
    public:
        virtual ~sink() = default;
        virtual void log(const details::log_msg& msg) = 0;
        virtual void flush() = 0;
        //日志级别
        virtual void set_level(level log_level) = 0;
        virtual level get_level() const = 0;
        
        virtual bool should_log(level msg_level) const = 0;
        virtual void set_formatter(std::unique_ptr<formatter> sink_formatter) =  0;
};
template<typename MutexT = std::mutex>
class base_sink : public sink {
    public:
        base_sink()
            : level_(level::trace), formatter_(std::make_unique<pattern_formatter>()) {}
        base_sink(const bace_sink&) = delete;
        base_sink& operator=(const base_sink&) delete;
        void log(const details::log_msg& msg) override {
            std::lock_guard<Mutex> lock(mutex_);
            sink_it_(msg);
        }
        level get_level() const override {
            std::lock_guard<Mutex> lock(mutex_);
            return level_;
        }
        void set_level(level log_level) override {
            std::lock_guard<Mutex> lock(mutex_);
            level_ = log_level;
        }
        bool should_log(level msg_level) const override {
            return msg_level >= get_level();
        }
        void set_formatter(std::unique_ptr<formatter> sink_formatter) override {
            std::lock_guard<Mutex> lock(mutex_);
            formatter_ = std::move(sink_formatter);
        }
    protected:
        virtual void sink_it_(const details::log_msg& msg) = 0;
        virtual void flush_() = 0;
        void format_message(const details::log_msg& msg, fmt::memory_buffer& dest){
            formatter_->format(msg, dest);
        }
        mutable Mutex mutex_;
        level level_;
        std::unique_ptr<formatter> formatter_;
};
    struct null_mutex {
       void lock() {}
         void unlock() {}
     };
    }
}