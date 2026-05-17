#pragma once

#include <iostream>
#include <string_view>

namespace mgv {

// Minimal stdout/stderr logger. Header-only on purpose: no dependency on a
// logging library in Phase 1. Replace with spdlog later if/when needed.
class Logger {
public:
    static void info (std::string_view msg) { std::cout << "[INFO]  " << msg << '\n'; }
    static void warn (std::string_view msg) { std::cerr << "[WARN]  " << msg << '\n'; }
    static void error(std::string_view msg) { std::cerr << "[ERROR] " << msg << '\n'; }
    static void debug(std::string_view msg) {
#if defined(MGV_DEBUG)
        std::cout << "[DEBUG] " << msg << '\n';
#else
        (void)msg;
#endif
    }
};

} // namespace mgv
