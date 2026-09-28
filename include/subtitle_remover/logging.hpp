#pragma once

#include <iostream>
#include <mutex>
#include <string_view>

namespace subtitle_remover {

enum class LogLevel { Info, Warning, Error };

inline void log(LogLevel level, std::string_view message) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    auto& output = level == LogLevel::Error ? std::cerr : std::clog;
    const char* label = level == LogLevel::Info ? "INFO" :
                        level == LogLevel::Warning ? "WARN" : "ERROR";
    output << '[' << label << "] " << message << '\n';
}

} // namespace subtitle_remover