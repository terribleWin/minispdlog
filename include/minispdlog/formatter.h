#pragma once

#include "common.h"
#include "details/log_msg.h"
#include <memory>
#include <fmt/format.h>
namespace minispdlog {
    class formatter {
        public:
            virtual ~formatter() = default;
            virtual void format(const details::log_msg& msg, fmt::memory_buffer& dest) = 0;
            virtual std::unique_ptr<formatter> clone() const = 0;
    };
}