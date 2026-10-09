/**
 * DRX Engine - Memory Management Implementation
 */

#include "Core/Public/CoreTypes.h"
#include "Core/Public/Platform.h"
#include "Core/Public/Log.h"

#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>

namespace DRX {

// ============================================================================
// Memory Tracking (Debug only)
// ============================================================================

#ifdef DRX_DEBUG

namespace {
    std::atomic<uint64> g_total_allocations{0};
    std::atomic<uint64> g_total_bytes_allocated{0};
    std::atomic<uint64> g_current_allocations{0};
    std::atomic<uint64> g_current_bytes{0};
    std::atomic<uint64> g_peak_bytes{0};
}

#endif

// ============================================================================
// Memory Statistics
// ============================================================================

struct MemoryStats {
    uint64 total_allocations;
    uint64 total_bytes;
    uint64 current_allocations;
    uint64 current_bytes;
    uint64 peak_bytes;
};

MemoryStats GetMemoryStats() {
    MemoryStats stats{};
#ifdef DRX_DEBUG
    stats.total_allocations = g_total_allocations.load();
    stats.total_bytes = g_total_bytes_allocated.load();
    stats.current_allocations = g_current_allocations.load();
    stats.current_bytes = g_current_bytes.load();
    stats.peak_bytes = g_peak_bytes.load();
#endif
    return stats;
}

void LogMemoryStats() {
#ifdef DRX_DEBUG
    auto stats = GetMemoryStats();
    DRX_LOG_INFO("=== Memory Statistics ===");
    DRX_LOG_INFO("Total allocations: {}", stats.total_allocations);
    DRX_LOG_INFO("Total bytes allocated: {} ({:.2f} MB)",
                 stats.total_bytes, stats.total_bytes / (1024.0 * 1024.0));
    DRX_LOG_INFO("Current allocations: {}", stats.current_allocations);
    DRX_LOG_INFO("Current bytes: {} ({:.2f} MB)",
                 stats.current_bytes, stats.current_bytes / (1024.0 * 1024.0));
    DRX_LOG_INFO("Peak bytes: {} ({:.2f} MB)",
                 stats.peak_bytes, stats.peak_bytes / (1024.0 * 1024.0));
#endif
}

// ============================================================================
// Aligned Allocation
// ============================================================================

void* AllocateAligned(size_t size, size_t alignment) {
    void* ptr = nullptr;

#ifdef DRX_PLATFORM_WINDOWS
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = nullptr;
    }
#endif

#ifdef DRX_DEBUG
    if (ptr) {
        g_total_allocations++;
        g_total_bytes_allocated += size;
        g_current_allocations++;
        g_current_bytes += size;

        uint64 current = g_current_bytes.load();
        uint64 peak = g_peak_bytes.load();
        while (current > peak && !g_peak_bytes.compare_exchange_weak(peak, current)) {}
    }
#endif

    return ptr;
}

void FreeAligned(void* ptr) {
    if (!ptr) return;

#ifdef DRX_PLATFORM_WINDOWS
    _aligned_free(ptr);
#else
    free(ptr);
#endif

#ifdef DRX_DEBUG
    g_current_allocations--;
#endif
}

// ============================================================================
// Memory Pool
// ============================================================================

class MemoryPool {
public:
    MemoryPool(size_t block_size, size_t block_count)
        : block_size_(block_size)
        , block_count_(block_count)
        , free_count_(block_count)
    {
        // Align block size to 16 bytes
        block_size_ = (block_size + 15) & ~15;

        // Allocate memory
        memory_ = static_cast<byte*>(AllocateAligned(block_size_ * block_count_, 16));

        // Initialize free list
        free_list_ = memory_;
        byte* current = memory_;
        for (size_t i = 0; i < block_count_ - 1; ++i) {
            byte* next = current + block_size_;
            *reinterpret_cast<byte**>(current) = next;
            current = next;
        }
        *reinterpret_cast<byte**>(current) = nullptr;
    }

    ~MemoryPool() {
        FreeAligned(memory_);
    }

    void* Allocate() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!free_list_) {
            return nullptr;
        }

        void* block = free_list_;
        free_list_ = *reinterpret_cast<byte**>(free_list_);
        free_count_--;

        return block;
    }

    void Free(void* ptr) {
        if (!ptr) return;

        std::lock_guard<std::mutex> lock(mutex_);

        *reinterpret_cast<byte**>(ptr) = free_list_;
        free_list_ = static_cast<byte*>(ptr);
        free_count_++;
    }

    size_t GetBlockSize() const { return block_size_; }
    size_t GetBlockCount() const { return block_count_; }
    size_t GetFreeCount() const { return free_count_; }

private:
    byte* memory_ = nullptr;
    byte* free_list_ = nullptr;
    size_t block_size_;
    size_t block_count_;
    size_t free_count_;
    std::mutex mutex_;
};

// ============================================================================
// Linear Allocator (Frame Allocator)
// ============================================================================

class LinearAllocator {
public:
    explicit LinearAllocator(size_t size)
        : size_(size)
        , offset_(0)
    {
        memory_ = static_cast<byte*>(AllocateAligned(size, 16));
    }

    ~LinearAllocator() {
        FreeAligned(memory_);
    }

    void* Allocate(size_t size, size_t alignment = 16) {
        // Align offset
        size_t aligned_offset = (offset_ + alignment - 1) & ~(alignment - 1);

        if (aligned_offset + size > size_) {
            return nullptr;
        }

        void* ptr = memory_ + aligned_offset;
        offset_ = aligned_offset + size;

        return ptr;
    }

    void Reset() {
        offset_ = 0;
    }

    size_t GetUsed() const { return offset_; }
    size_t GetSize() const { return size_; }
    size_t GetFree() const { return size_ - offset_; }

private:
    byte* memory_ = nullptr;
    size_t size_;
    size_t offset_;
};

} // namespace DRX
