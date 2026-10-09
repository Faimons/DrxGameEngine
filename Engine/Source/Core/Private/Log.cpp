/**
 * DRX Engine - Logging Implementation
 */

#include "Core/Public/Log.h"
#include <ctime>
#include <chrono>

#ifdef DRX_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace DRX {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
#ifdef DRX_DEBUG
    level_ = LogLevel::Debug;
#else
    level_ = LogLevel::Info;
#endif

    // Check if terminal supports colors
#ifdef DRX_PLATFORM_WINDOWS
    // Enable ANSI colors on Windows 10+
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(hConsole, &mode)) {
        SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        fclose(file_);
    }
}

void Logger::SetLevel(LogLevel level) {
    level_ = level;
}

LogLevel Logger::GetLevel() const {
    return level_;
}

void Logger::SetOutputFile(const String& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }

    file_ = fopen(path.c_str(), "w");
    file_output_ = (file_ != nullptr);
}

void Logger::EnableConsoleOutput(bool enable) {
    console_output_ = enable;
}

void Logger::EnableFileOutput(bool enable) {
    file_output_ = enable;
}

void Logger::EnableColors(bool enable) {
    colors_enabled_ = enable;
}

void Logger::Write(LogLevel level, const char* file, int line, std::string_view message) {
    if (!IsEnabled(level)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Get timestamp
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count() % 1000;

    std::tm local_time{};
#ifdef DRX_PLATFORM_WINDOWS
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &local_time);

    // Extract filename from path
    const char* filename = file;
    for (const char* p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            filename = p + 1;
        }
    }

    // Console output
    if (console_output_) {
        if (colors_enabled_) {
            fprintf(stderr, "%s[%s.%03lld] [%s] %s:%d: %.*s%s\n",
                    GetLevelColor(level),
                    timestamp,
                    static_cast<long long>(ms),
                    GetLevelString(level),
                    filename,
                    line,
                    static_cast<int>(message.size()),
                    message.data(),
                    "\033[0m");
        } else {
            fprintf(stderr, "[%s.%03lld] [%s] %s:%d: %.*s\n",
                    timestamp,
                    static_cast<long long>(ms),
                    GetLevelString(level),
                    filename,
                    line,
                    static_cast<int>(message.size()),
                    message.data());
        }
    }

    // File output
    if (file_output_ && file_) {
        fprintf(file_, "[%s.%03lld] [%s] %s:%d: %.*s\n",
                timestamp,
                static_cast<long long>(ms),
                GetLevelString(level),
                filename,
                line,
                static_cast<int>(message.size()),
                message.data());
    }

    // Flush for errors and fatal
    if (level >= LogLevel::Error) {
        FlushUnlocked();
    }
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    FlushUnlocked();
}

void Logger::FlushUnlocked() {
    if (console_output_) {
        fflush(stderr);
    }
    if (file_output_ && file_) {
        fflush(file_);
    }
}

const char* Logger::GetLevelString(LogLevel level) const {
    switch (level) {
        case LogLevel::Trace:   return "TRACE";
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
        default:                return "?????";
    }
}

const char* Logger::GetLevelColor(LogLevel level) const {
    switch (level) {
        case LogLevel::Trace:   return "\033[90m";      // Dark gray
        case LogLevel::Debug:   return "\033[36m";      // Cyan
        case LogLevel::Info:    return "\033[32m";      // Green
        case LogLevel::Warning: return "\033[33m";      // Yellow
        case LogLevel::Error:   return "\033[31m";      // Red
        case LogLevel::Fatal:   return "\033[35;1m";    // Bright magenta
        default:                return "\033[0m";
    }
}

} // namespace DRX
