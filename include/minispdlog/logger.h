#pragma once
#include "common.h"
#include "level.h"
#include "sinks/base_sink.h"
#include "details/log_msg.h"
#include <fmt/format.h>
#include <memory>
#include <vector>
#include <string>

namespace minispdlog {
    class MINISPDLOG_API logger {
        public:
            explicit logger(const std::string name);
            logger(std::string name, sinks::sink_ptr single_sink);
            logger(std::string name, std::vector<sinks::sink_ptr> sinks);
            virtual ~logger() = default;
            
            logger(const logger&) = delete;
            logger& operator=(const logger&) = delete;
            //变参模板接口
            template<typename... Args>
            void trace(fmt::format_string<Args...> fmt, Args&&... args) {
                log(level::trace, fmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void debug(fmt::format_string<Args...> fmt, Args&&... args) {
                log(level::debug, fmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void info(fmt::format_string<Args...> fmt, Args&&... args) {
                log(level::info, fmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void warn(fmt::format_string<Args...> fmt, Args&&... args) {
                log(level::warn, fmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void error(fmt::format_string<Args...> fmt, Args&&... args) {
                log(level::error, fmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void critical(fmt::format_string<Args...> fmt, Args&&... args) {
                log(level::critical, fmt, std::forward<Args>(args)...);
            }
            template<typename... Args>
            void log(level lvl, fmt::format_string<Args...> fmt, Args&&... args) {
                //级别过滤
                if(!this->should_log(lvl)) return;//使用的是成员函数而非全局函数
                //格式化 传入到 memory_buffer
                fmt::memory_buffer buf;
                fmt::format_to(std::back_inserter(buf),fmt,std::forward<Args>(args)...);
                //格式化内容封装到 log_msg 结构体中
                details::log_msg log(
                    name_,
                    lvl,
                    string_view_t(buf.data(),buf.size())//格式化以后的字符串
                );//初始化 log_msg
                sink_it_(log);
            }
                //sink 管理
                void add_sink(sinks::sink_ptr sink);
                void remove_sink(sinks::sink_ptr sink);
                std::vector<sinks::sink_ptr>& sinks();
                const std::vector<sinks::sink_ptr>& sinks() const;
                //级别设置
                void set_level(level log_level);
                level get_level() const;
                bool should_log(level msg_level) const;
                void flush();    
                void flush_on(level log_level);
                const std::string& name() const;
                // 内部 API：供异步线程池调用
                virtual void  sink_it_(const details::log_msg& msg);
            protected:
                std::string name_;
                std::vector<sinks::sink_ptr> sinks_;
                level level_{level::trace};
                level flush_level_{level::off};
                };
    }