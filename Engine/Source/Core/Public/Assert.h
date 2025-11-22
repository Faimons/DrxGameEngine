#pragma once

/**
 * DRX Engine - Assertion System
 * Debug assertions and runtime checks
 */

#include "Platform.h"
#include "Log.h"

namespace DRX {

/**
 * Handle assertion failure
 */
DRX_API void HandleAssertionFailure(
    const char* expression,
    const char* file,
    int line,
    const char* function,
    const char* message = nullptr
);

} // namespace DRX

// ============================================================================
// Assertion Macros
// ============================================================================

#ifdef DRX_DEBUG

/**
 * Debug assertion - only active in debug builds
 */
#define DRX_ASSERT(expr) \
    do { \
        if (DRX_UNLIKELY(!(expr))) { \
            DRX::HandleAssertionFailure(#expr, __FILE__, __LINE__, __FUNCTION__); \
            DRX_DEBUG_BREAK(); \
        } \
    } while (0)

/**
 * Debug assertion with message
 */
#define DRX_ASSERT_MSG(expr, msg) \
    do { \
        if (DRX_UNLIKELY(!(expr))) { \
            DRX::HandleAssertionFailure(#expr, __FILE__, __LINE__, __FUNCTION__, msg); \
            DRX_DEBUG_BREAK(); \
        } \
    } while (0)

#else

#define DRX_ASSERT(expr) ((void)0)
#define DRX_ASSERT_MSG(expr, msg) ((void)0)

#endif // DRX_DEBUG

/**
 * Runtime check - always active
 */
#define DRX_CHECK(expr) \
    do { \
        if (DRX_UNLIKELY(!(expr))) { \
            DRX::HandleAssertionFailure(#expr, __FILE__, __LINE__, __FUNCTION__); \
        } \
    } while (0)

/**
 * Runtime check with message
 */
#define DRX_CHECK_MSG(expr, msg) \
    do { \
        if (DRX_UNLIKELY(!(expr))) { \
            DRX::HandleAssertionFailure(#expr, __FILE__, __LINE__, __FUNCTION__, msg); \
        } \
    } while (0)

/**
 * Verify - check expression but also evaluate in release
 */
#define DRX_VERIFY(expr) \
    do { \
        if (DRX_UNLIKELY(!(expr))) { \
            DRX::HandleAssertionFailure(#expr, __FILE__, __LINE__, __FUNCTION__); \
            DRX_DEBUG_BREAK(); \
        } \
    } while (0)

/**
 * Unreachable code marker
 */
#define DRX_UNREACHABLE() \
    do { \
        DRX::HandleAssertionFailure("Unreachable code reached", __FILE__, __LINE__, __FUNCTION__); \
        DRX_DEBUG_BREAK(); \
    } while (0)

/**
 * Not implemented marker
 */
#define DRX_NOT_IMPLEMENTED() \
    do { \
        DRX::HandleAssertionFailure("Not implemented", __FILE__, __LINE__, __FUNCTION__); \
        DRX_DEBUG_BREAK(); \
    } while (0)
