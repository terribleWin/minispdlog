#pragma once
#include "../common.h"
#include <string>
#include <thread>
#ifdef _WIN32
   #include <windows.h>
#else
   #include <pthread.h>
#endif
namespace minispdlog{
    namespace details{
MINISPDLOG_API std::string format_time(
    const log_clock::time_point& tp, 
    const char* format = "%Y-%m-%d %H:%M:%S"
);
MINISPDLOG_API size_t get_thread_id();
MINISPDLOG_API int64_t get_timestamp_ms();
MINISPDLOG_API std::string& ltrim(std::string& s);
MINISPDLOG_API std::string& rtrim(std::string& s);
MINISPDLOG_API std::string& trim(std::string& s);
    }
}