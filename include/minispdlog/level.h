#pragma once
#include "common.h"
#include <string>
namespace minispdlog{
	//level of log
enum class level{
	trace = 0,
	debug = 1,
	info = 2,
	warn = 3,
	error = 4,
	critical = 5,
	off = 6
};
MINISPDLOG_API const char* level_to_string(level lv1) noexcept;
MINISPDLOG_API const char* level_to_short_string(level lv1) noexcept;
MINISPDLOG_API level string_to_level(const std::string& str);
inline bool should_log(level logger_level, level msg_level) noexcept{
	return msg_level >= logger_level;
}
}
