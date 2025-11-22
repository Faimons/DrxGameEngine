#pragma once

/**
 * DRX Engine - Logging System
 * Thread-safe logging with multiple severity levels
 */

#include "CoreTypes.h"
#include "Platform.h"
#include <cstdio>
#include <cstdarg>
#include <mutex>

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

    void SetOutputFile(const String& path);
    void EnableConsoleOutput(bool enable);
    void EnableFileOutput(bool enable);
    void EnableColors(bool enable);

    void Log(LogLevel level, const char* file, int line, const char* fmt, ...);
    void LogV(LogLevel level, const char* file, int line, const char* fmt, va_list args);

    void Flush();

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    const char* GetLevelString(LogLevel level) const;
    const char* GetLevelColor(LogLevel level) const;

    LogLevel level_ = LogLevel::Info;
    bool console_output_ = true;
    bool file_output_ = false;
    bool colors_enabled_ = true;
    FILE* file_ = nullptr;
    std::mutex mutex_;
};

} // namespace DRX

// ============================================================================
// Logging Macros
// ============================================================================

#define DRX_LOG_TRACE(fmt, ...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Trace, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DRX_LOG_DEBUG(fmt, ...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DRX_LOG_INFO(fmt, ...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Info, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DRX_LOG_WARNING(fmt, ...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Warning, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DRX_LOG_ERROR(fmt, ...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DRX_LOG_FATAL(fmt, ...) \
    DRX::Logger::Instance().Log(DRX::LogLevel::Fatal, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// Conditional logging
#ifdef DRX_DEBUG
    #define DRX_LOG_DEBUG_ONLY(fmt, ...) DRX_LOG_DEBUG(fmt, ##__VA_ARGS__)
#else
    #define DRX_LOG_DEBUG_ONLY(fmt, ...) ((void)0)
#endif
