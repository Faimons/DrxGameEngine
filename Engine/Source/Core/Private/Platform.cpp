/**
 * DRX Engine - Platform Implementation
 */

#include "Core/Public/Platform.h"
#include "Core/Public/Log.h"

#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <filesystem>

#ifdef DRX_PLATFORM_WINDOWS
    #include <windows.h>
#elif defined(DRX_PLATFORM_LINUX) || defined(DRX_PLATFORM_MACOS)
    #include <unistd.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <dlfcn.h>
    #ifdef DRX_PLATFORM_LINUX
        #include <sys/sysinfo.h>
    #endif
    #ifdef DRX_PLATFORM_MACOS
        #include <mach/mach.h>
        #include <sys/sysctl.h>
    #endif
#endif

namespace DRX::Platform {

namespace {
    std::chrono::high_resolution_clock::time_point g_start_time;
    bool g_initialized = false;
}

bool Initialize() {
    if (g_initialized) return true;

    g_start_time = std::chrono::high_resolution_clock::now();
    g_initialized = true;

    DRX_LOG_INFO("Platform initialized: {}", GetPlatformName());
    return true;
}

void Shutdown() {
    if (!g_initialized) return;
    g_initialized = false;
    DRX_LOG_INFO("Platform shutdown");
}

const char* GetPlatformName() {
    return DRX_PLATFORM_NAME;
}

float64 GetTimeSeconds() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - g_start_time);
    return static_cast<float64>(duration.count()) / 1000000000.0;
}

uint64 GetTimeMilliseconds() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_time);
    return static_cast<uint64>(duration.count());
}

void Sleep(uint32 milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void WaitUntil(float64 target_seconds) {
    // Coarse sleep until shortly before the target, then yield; OS sleeps overshoot by up to a timer tick
    constexpr float64 kSpinWindow = 0.002;
#ifdef DRX_PLATFORM_WINDOWS
    #ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
        #define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
    #endif
    // The default Windows timer resolution (15.6 ms) is too coarse for Sleep; high-resolution timers exist since 10 1803
    static thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                              TIMER_ALL_ACCESS);
#endif
    for (;;) {
        const float64 remaining = target_seconds - GetTimeSeconds();
        if (remaining <= 0.0) return;
        if (remaining > kSpinWindow) {
#ifdef DRX_PLATFORM_WINDOWS
            if (timer) {
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>((remaining - kSpinWindow) * 1.0e7);   // relative, 100 ns units
                if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                    WaitForSingleObject(timer, INFINITE);
                    continue;
                }
            }
#else
            std::this_thread::sleep_for(std::chrono::duration<float64>(remaining - kSpinWindow));
            continue;
#endif
        }
        std::this_thread::yield();
    }
}

uint32 GetCPUCoreCount() {
    return std::thread::hardware_concurrency();
}

uint64 GetTotalSystemMemory() {
#ifdef DRX_PLATFORM_WINDOWS
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return status.ullTotalPhys;
#elif defined(DRX_PLATFORM_LINUX)
    struct sysinfo info;
    sysinfo(&info);
    return static_cast<uint64>(info.totalram) * info.mem_unit;
#elif defined(DRX_PLATFORM_MACOS)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64 memory;
    size_t length = sizeof(memory);
    sysctl(mib, 2, &memory, &length, nullptr, 0);
    return memory;
#else
    return 0;
#endif
}

uint64 GetAvailableSystemMemory() {
#ifdef DRX_PLATFORM_WINDOWS
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return status.ullAvailPhys;
#elif defined(DRX_PLATFORM_LINUX)
    struct sysinfo info;
    sysinfo(&info);
    return static_cast<uint64>(info.freeram) * info.mem_unit;
#elif defined(DRX_PLATFORM_MACOS)
    vm_statistics64_data_t stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                         reinterpret_cast<host_info64_t>(&stats), &count) == KERN_SUCCESS) {
        return static_cast<uint64>(stats.free_count) * getpagesize();
    }
    return 0;
#else
    return 0;
#endif
}

String GetExecutablePath() {
#ifdef DRX_PLATFORM_WINDOWS
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return String(buffer);
#elif defined(DRX_PLATFORM_LINUX)
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        return String(buffer);
    }
    return "";
#elif defined(DRX_PLATFORM_MACOS)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        return String(buffer);
    }
    return "";
#else
    return "";
#endif
}

String GetWorkingDirectory() {
    return std::filesystem::current_path().string();
}

bool SetWorkingDirectory(const String& path) {
    std::error_code ec;
    std::filesystem::current_path(path, ec);
    return !ec;
}

// ============================================================================
// File System Implementation
// ============================================================================

namespace FileSystem {

bool FileExists(const String& path) {
    return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

bool DirectoryExists(const String& path) {
    return std::filesystem::exists(path) && std::filesystem::is_directory(path);
}

bool CreateDirectoryPath(const String& path) {
    std::error_code ec;
    return std::filesystem::create_directories(path, ec);
}

bool DeleteFile(const String& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

bool DeleteDirectory(const String& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

int64 GetFileSize(const String& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    return ec ? -1 : static_cast<int64>(size);
}

Result<String> ReadTextFile(const String& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return Result<String>::Error("Failed to open file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Result<Array<byte>> ReadBinaryFile(const String& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return Result<Array<byte>>::Error("Failed to open file: " + path);
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    Array<byte> buffer(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(buffer.data()), size);

    return buffer;
}

bool WriteTextFile(const String& path, const String& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    return file.good();
}

bool WriteBinaryFile(const String& path, const Array<byte>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return file.good();
}

Array<String> ListFiles(const String& path, const String& pattern) {
    Array<String> result;

    if (!DirectoryExists(path)) {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            String filename = entry.path().filename().string();

            // Simple pattern matching (supports * wildcard)
            if (pattern == "*" || pattern.empty()) {
                result.push_back(entry.path().string());
            } else if (pattern[0] == '*') {
                String ext = pattern.substr(1);
                if (filename.length() >= ext.length() &&
                    filename.substr(filename.length() - ext.length()) == ext) {
                    result.push_back(entry.path().string());
                }
            } else if (filename == pattern) {
                result.push_back(entry.path().string());
            }
        }
    }

    return result;
}

Array<String> ListDirectories(const String& path) {
    Array<String> result;

    if (!DirectoryExists(path)) {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_directory()) {
            result.push_back(entry.path().string());
        }
    }

    return result;
}

String GetExtension(const String& path) {
    auto ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.') {
        return ext.substr(1);
    }
    return ext;
}

String GetFilename(const String& path) {
    return std::filesystem::path(path).filename().string();
}

String GetDirectory(const String& path) {
    return std::filesystem::path(path).parent_path().string();
}

String JoinPath(const String& a, const String& b) {
    return (std::filesystem::path(a) / b).string();
}

String NormalizePath(const String& path) {
    return std::filesystem::path(path).lexically_normal().string();
}

} // namespace FileSystem

} // namespace DRX::Platform
