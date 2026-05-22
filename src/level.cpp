#include "minispdlog/level.h"
#include <array>
#include <algorithm>

namespace minispdlog {

static constexpr std::array<const char*, 7> level_names = {
    "trace", "debug", "info", "warn", "error", "critical", "off"
};

static constexpr std::array<const char*, 7> level_short_names = {
    "T", "D", "I", "W", "E", "C", "O"
};

const char* level_to_string(level lvl) noexcept {
    auto index = static_cast<size_t>(lvl);
    if (index < level_names.size()) {
        return level_names[index];
    }
    return "unknown";
}

const char* level_to_short_string(level lvl) noexcept {
    auto index = static_cast<size_t>(lvl);
    if (index < level_short_names.size()) {
        return level_short_names[index];
    }
    return "U";
}

level string_to_level(const std::string& str) {
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    for (size_t i = 0; i < level_names.size(); ++i) {
        if (lower_str == level_names[i]) {
            return static_cast<level>(i);
        }
    }
    return level::info;
}

} // namespace minispdlog