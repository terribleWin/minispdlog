#pragma once
#include<string>
#include<memory>
#include<cstdint>
#include<chrono>
namespace minispdlog{
constexpr const char* VERSION ="0.1.0";
#ifdef _WIN32
   #define MINISPDLOG_WINDOWS
#elif defined(__linux__)
   #define MINISPDLOG_LINUX
#elif defined(__APPLE__)
   #define MINISPDLOG_MACOS
#endif

#if defined(_WIN32) && defined(MINISPDLOG_SHARED)
    #ifdef MINISPDLOG_BUILD
        #define MINISPDLOG_API __declspec(dllexport)
    #else
        #define MINISPDLOG_API __declspec(dllimport)
    #endif
#else
    #define MINISPDLOG_API
#endif

using string_view_t = std::string;
using log_clock = std::chrono::system_clock;
}
