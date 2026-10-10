#pragma once

/**
 * DRX Engine - Platform Abstraction
 * Platform detection, macros, and system interfaces
 */

#include "CoreTypes.h"

// ============================================================================
// Platform Detection
// ============================================================================

#if defined(_WIN32) || defined(_WIN64)
    #define DRX_PLATFORM_WINDOWS 1
    #define DRX_PLATFORM_NAME "Windows"
#elif defined(__linux__) && !defined(__ANDROID__)
    #define DRX_PLATFORM_LINUX 1
    #define DRX_PLATFORM_NAME "Linux"
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC
        #define DRX_PLATFORM_MACOS 1
        #define DRX_PLATFORM_NAME "macOS"
    #endif
#elif defined(__ANDROID__)
    #define DRX_PLATFORM_ANDROID 1
    #define DRX_PLATFORM_NAME "Android"
#else
    #error "Unsupported platform"
#endif

// ============================================================================
// Compiler Detection
// ============================================================================

#if defined(_MSC_VER)
    #define DRX_COMPILER_MSVC 1
    #define DRX_COMPILER_VERSION _MSC_VER
#elif defined(__clang__)
    #define DRX_COMPILER_CLANG 1
    #define DRX_COMPILER_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#elif defined(__GNUC__)
    #define DRX_COMPILER_GCC 1
    #define DRX_COMPILER_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif

// ============================================================================
// API Export/Import
// ============================================================================

#if defined(DRX_STATIC)
    #define DRX_API
    #define DRX_API_LOCAL
#elif defined(DRX_PLATFORM_WINDOWS)
    #ifdef DRX_BUILD_DLL
        #define DRX_API __declspec(dllexport)
    #else
        #define DRX_API __declspec(dllimport)
    #endif
    #define DRX_API_LOCAL
#else
    #define DRX_API __attribute__((visibility("default")))
    #define DRX_API_LOCAL __attribute__((visibility("hidden")))
#endif

// ============================================================================
// Compiler Attributes
// ============================================================================

#ifdef DRX_COMPILER_MSVC
    #define DRX_FORCE_INLINE __forceinline
    #define DRX_NO_INLINE __declspec(noinline)
    #define DRX_ALIGN(x) __declspec(align(x))
    #define DRX_DEPRECATED(msg) __declspec(deprecated(msg))
    #define DRX_DEBUG_BREAK() __debugbreak()
    #define DRX_LIKELY(x) (x)
    #define DRX_UNLIKELY(x) (x)
#else
    #define DRX_FORCE_INLINE __attribute__((always_inline)) inline
    #define DRX_NO_INLINE __attribute__((noinline))
    #define DRX_ALIGN(x) __attribute__((aligned(x)))
    #define DRX_DEPRECATED(msg) __attribute__((deprecated(msg)))
    #define DRX_LIKELY(x) __builtin_expect(!!(x), 1)
    #define DRX_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #ifdef DRX_PLATFORM_WINDOWS
        #define DRX_DEBUG_BREAK() __debugbreak()
    #else
        #include <signal.h>
        #define DRX_DEBUG_BREAK() raise(SIGTRAP)
    #endif
#endif

// ============================================================================
// Utility Macros
// ============================================================================

#define DRX_UNUSED(x) (void)(x)
#define DRX_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define DRX_STRINGIFY(x) #x
#define DRX_CONCAT(a, b) a##b

#define DRX_BIT(x) (1u << (x))
#define DRX_KB(x) ((x) * 1024ULL)
#define DRX_MB(x) ((x) * 1024ULL * 1024ULL)
#define DRX_GB(x) ((x) * 1024ULL * 1024ULL * 1024ULL)

// ============================================================================
// Platform Interface
// ============================================================================

namespace DRX::Platform {

/**
 * Initialize platform systems
 */
DRX_API bool Initialize();

/**
 * Shutdown platform systems
 */
DRX_API void Shutdown();

/**
 * Get platform name
 */
DRX_API const char* GetPlatformName();

/**
 * Get high-resolution time in seconds
 */
DRX_API float64 GetTimeSeconds();

/**
 * Get high-resolution time in milliseconds
 */
DRX_API uint64 GetTimeMilliseconds();

/**
 * Sleep for specified milliseconds
 */
DRX_API void Sleep(uint32 milliseconds);

/**
 * Wait until GetTimeSeconds() reaches target_seconds (sub-millisecond accuracy, used for frame pacing)
 */
DRX_API void WaitUntil(float64 target_seconds);

/**
 * Get number of CPU cores
 */
DRX_API uint32 GetCPUCoreCount();

/**
 * Get total system memory in bytes
 */
DRX_API uint64 GetTotalSystemMemory();

/**
 * Get available system memory in bytes
 */
DRX_API uint64 GetAvailableSystemMemory();

/**
 * Get executable path
 */
DRX_API String GetExecutablePath();

/**
 * Get current working directory
 */
DRX_API String GetWorkingDirectory();

/**
 * Set current working directory
 */
DRX_API bool SetWorkingDirectory(const String& path);

// ============================================================================
// File System
// ============================================================================

namespace FileSystem {

/**
 * Check if file exists
 */
DRX_API bool FileExists(const String& path);

/**
 * Check if directory exists
 */
DRX_API bool DirectoryExists(const String& path);

/**
 * Create directory (and parent directories if needed)
 */
DRX_API bool CreateDirectoryPath(const String& path);

/**
 * Delete file
 */
DRX_API bool DeleteFile(const String& path);

/**
 * Delete directory (must be empty)
 */
DRX_API bool DeleteDirectory(const String& path);

/**
 * Get file size in bytes
 */
DRX_API int64 GetFileSize(const String& path);

/**
 * Read entire file to string
 */
DRX_API Result<String> ReadTextFile(const String& path);

/**
 * Read entire file to byte array
 */
DRX_API Result<Array<byte>> ReadBinaryFile(const String& path);

/**
 * Write string to file
 */
DRX_API bool WriteTextFile(const String& path, const String& content);

/**
 * Write bytes to file
 */
DRX_API bool WriteBinaryFile(const String& path, const Array<byte>& data);

/**
 * List files in directory
 */
DRX_API Array<String> ListFiles(const String& path, const String& pattern = "*");

/**
 * List directories in directory
 */
DRX_API Array<String> ListDirectories(const String& path);

/**
 * Get file extension (without dot)
 */
DRX_API String GetExtension(const String& path);

/**
 * Get filename without path
 */
DRX_API String GetFilename(const String& path);

/**
 * Get directory from path
 */
DRX_API String GetDirectory(const String& path);

/**
 * Join paths
 */
DRX_API String JoinPath(const String& a, const String& b);

/**
 * Normalize path separators
 */
DRX_API String NormalizePath(const String& path);

} // namespace FileSystem

} // namespace DRX::Platform
