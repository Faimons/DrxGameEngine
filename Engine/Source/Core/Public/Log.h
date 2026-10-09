#pragma once

/**
 * DRX Engine - Logging System
 * Thread-safe logging with multiple severity levels
 */

#include "CoreTypes.h"
#include "Platform.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string_view>

#include <spdlog/fmt/fmt.h>

namespace DRX {

// ============================================================================
// Log Levels
// ============================================================================

enum class LogLevel : uint8 {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5
};

// ============================================================================
// Logger Class
// ============================================================================

class DRX_API Logger {
public:
    static Logger& Instance();

    void SetLevel(LogLevel level);
    LogLevel GetLevel() const;
    bool IsEnabled(LogLevel level) const { return level >= level_.load(std::memory_order_relaxed); }

    void SetOutputFile(const String& path);
    void EnableConsoleOutput(bool enable);
    void EnableFileOutput(bool enable);
    void EnableColors(bool enable);

    template<typename... Args>
    void Log(LogLevel level, const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
        if (!IsEnabled(level)) {
            return;
        }
        Write(level, file, line, fmt::vformat(format, fmt::make_format_args(args...)));
    }

    void Write(LogLevel level, const char* file, int line, std::string_view message);

    void Flush();

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void FlushUnlocked();
    const char* GetLevelString(LogLevel level) const;
    const char* GetLevelColor(LogLevel level) const;

    std::atomic<LogLevel> level_{LogLevel::Info};
    std::atomic<bool> console_output_{true};
    std::atomic<bool> file_output_{false};
    std::atomic<bool> colors_enabled_{true};
    FILE* file_ = nullptr;
    std::mutex mutex_;
};

} // namespace DRX

// ============================================================================
// Logging Macros
// ============================================================================

#define DRX_LOG_TRACE(...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)

#define DRX_LOG_DEBUG(...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)

#define DRX_LOG_INFO(...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)

#define DRX_LOG_WARNING(...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Warning, __FILE__, __LINE__, __VA_ARGS__)

#define DRX_LOG_WARN(...) DRX_LOG_WARNING(__VA_ARGS__)

#define DRX_LOG_ERROR(...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)

#define DRX_LOG_FATAL(...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Fatal, __FILE__, __LINE__, __VA_ARGS__)

// Conditional logging
#ifdef DRX_DEBUG
    #define DRX_LOG_DEBUG_ONLY(...) DRX_LOG_DEBUG(__VA_ARGS__)
#else
    #define DRX_LOG_DEBUG_ONLY(...) ((void)0)
#endif
