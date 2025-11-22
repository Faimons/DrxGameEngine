/**
 * DRX Engine - Assertion Implementation
 */

#include "Core/Public/Assert.h"
#include <cstdio>

namespace DRX {

void HandleAssertionFailure(
    const char* expression,
    const char* file,
    int line,
    const char* function,
    const char* message)
{
    // Extract filename from path
    const char* filename = file;
    for (const char* p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            filename = p + 1;
        }
    }

    if (message) {
        DRX_LOG_FATAL("Assertion failed: %s", expression);
        DRX_LOG_FATAL("Message: %s", message);
    } else {
        DRX_LOG_FATAL("Assertion failed: %s", expression);
    }

    DRX_LOG_FATAL("Location: %s:%d", filename, line);
    DRX_LOG_FATAL("Function: %s", function);

    // Also print to stderr in case logging is disabled
    fprintf(stderr, "\n=== ASSERTION FAILED ===\n");
    fprintf(stderr, "Expression: %s\n", expression);
    if (message) {
        fprintf(stderr, "Message: %s\n", message);
    }
    fprintf(stderr, "Location: %s:%d\n", filename, line);
    fprintf(stderr, "Function: %s\n", function);
    fprintf(stderr, "========================\n\n");
    fflush(stderr);
}

} // namespace DRX
