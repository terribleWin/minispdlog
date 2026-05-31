#pragma once
#include "logger.h"
#include "common.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <string>
namespace minispdlog {
    class MINISPDLOG_API registry {
        public:
        //禁止拷贝 确保全局唯一一个实例
            registry(const registry&) = delete;
            registry& operator=(const registry&) = delete;
            //单实例
            static registry& instance();
            // ========== Logger 注册管理 ==========
            void register_logger(std::shared_ptr<logger> new_logger);
            std::shared_ptr<logger> get(const std::string& logger_name);
            void drop(const std::string& logger_name);
            void drop_all();
            // ========== 默认 logger ==========
            std::shared_ptr<logger> default_logger();
            void set_default_logger(std::shared_ptr<logger> new_default_logger);
            void set_level(level log_level);
            void flush_all();
        private:
            registry();
            ~registry() = default;
            void throw_if_exists(const std::string & logger_name);
            std::mutex mutex_;
            std::unordered_map<std::string, std::shared_ptr<logger>> loggers_;
            std::shared_ptr<logger> default_logger_;
    };
}