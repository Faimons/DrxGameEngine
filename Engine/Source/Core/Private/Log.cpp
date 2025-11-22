/**
 * DRX Engine - Logging Implementation
 */

#include "Core/Public/Log.h"
#include <ctime>
#include <chrono>

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
    if (file_) {
        file_output_ = true;
    }
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

void Logger::Log(LogLevel level, const char* file, int line, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(level, file, line, fmt, args);
    va_end(args);
}

void Logger::LogV(LogLevel level, const char* file, int line, const char* fmt, va_list args) {
    if (level < level_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Get timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count() % 1000;

    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", std::localtime(&time));

    // Format message
    char message[4096];
    vsnprintf(message, sizeof(message), fmt, args);

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
            fprintf(stderr, "%s[%s.%03lld] [%s] %s:%d: %s%s\n",
                    GetLevelColor(level),
                    timestamp,
                    static_cast<long long>(ms),
                    GetLevelString(level),
                    filename,
                    line,
                    message,
                    "\033[0m");
        } else {
            fprintf(stderr, "[%s.%03lld] [%s] %s:%d: %s\n",
                    timestamp,
                    static_cast<long long>(ms),
                    GetLevelString(level),
                    filename,
                    line,
                    message);
        }
    }

    // File output
    if (file_output_ && file_) {
        fprintf(file_, "[%s.%03lld] [%s] %s:%d: %s\n",
                timestamp,
                static_cast<long long>(ms),
                GetLevelString(level),
                filename,
                line,
                message);
    }

    // Flush for errors and fatal
    if (level >= LogLevel::Error) {
        Flush();
    }
}

void Logger::Flush() {
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
