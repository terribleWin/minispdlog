#pragma once
#include <fstream>
#include "base_sink.h"
#include <iostream>
#include <mutex>
#include <string>
namespace minispdlog {
    namespace sinks {
        template<typename Mutex>
        class file_sink : public base_sink<Mutex> {
            public:
                explicit file_sink(const std::string& filename, bool truncate = false){
                    auto mode =truncate ? std::ios::trunc : std::ios::app;
                    file_.open(filename,std::ios::out | mode);
                    if(!file_.is_open()){
                        throw std::runtime_error("Failed to open file: " + filename);
                    }
                }
                ~file_sink() override {
                    if(file_.is_open()){
                        file_.close();
                    }
                }
            protected:
                void sink_it_(const details::log_msg& msg) override {
                    fmt::memory_buffer formatted;
                    this->format_message(msg, formatted);
                    // 写入文件
                    file_.write(formatted.data(), formatted.size());
                }
                void flush_() override {
                file_.flush();
                }
            private:
                std::ofstream file_;
            };
    using file_sink_mt = file_sink<std::mutex>;
    using file_sink_st = file_sink<null_mutex>;
    }
}