#include "minispdlog/details/utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace minispdlog {
namespace details {

    std::string format_time(const log_clock::time_point& tp, const char* format) {
        auto time_t_val = log_clock::to_time_t(tp);
        std::tm tm_val;

#ifdef _WIN32
        localtime_s(&tm_val, &time_t_val);
#else
        localtime_r(&time_t_val, &tm_val);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_val, format);
        return oss.str();
    }

    int64_t get_timestamp_ms() {
        auto now = log_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    size_t get_thread_id() {
        thread_local const size_t tid = []() {
#ifdef _WIN32
            return static_cast<size_t>(::GetCurrentThreadId());
#elif defined(__linux__) || defined(__APPLE__)
            return static_cast<size_t>(pthread_self());
#else
            return std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
        }();
        return tid;
    }

    size_t get_pid() {
#ifdef _WIN32
        /// Windows 进程不存在 fork 后 PID 改变的语义，因此可安全缓存。
        static const size_t pid = []() { return static_cast<size_t>(::GetCurrentProcessId()); }();
        return pid;
#else
        /// POSIX 子进程必须在 fork 后立即报告新 PID，不能使用进程级静态缓存。
        return static_cast<size_t>(::getpid());
#endif
    }

    const std::string& get_hostname() {
        static const std::string host = []() {
            char buf[256]{};
#ifdef _WIN32
            DWORD n = static_cast<DWORD>(sizeof(buf));
            if (::GetComputerNameA(buf, &n) != 0 && buf[0] != '\0') {
                return std::string(buf);
            }
#else
            if (::gethostname(buf, sizeof(buf)) == 0) {
                buf[sizeof(buf) - 1] = '\0';
                if (buf[0] != '\0') {
                    return std::string(buf);
                }
            }
#endif
            return std::string("unknown");
        }();
        return host;
    }

    std::string& ltrim(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
        return s;
    }

    std::string& rtrim(std::string& s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
        return s;
    }

    std::string& trim(std::string& s) {
        return ltrim(rtrim(s));
    }

} // details
} // minispdlog
