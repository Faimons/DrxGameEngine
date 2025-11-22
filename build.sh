#!/bin/bash

# DRX Engine Build Script
# Usage: ./build.sh [debug|release] [clean]

set -e

BUILD_TYPE="${1:-Release}"
CLEAN="${2:-}"

# Normalize build type
case "$BUILD_TYPE" in
    debug|Debug|DEBUG)
        BUILD_TYPE="Debug"
        ;;
    release|Release|RELEASE)
        BUILD_TYPE="Release"
        ;;
    *)
        echo "Unknown build type: $BUILD_TYPE"
        echo "Usage: $0 [debug|release] [clean]"
        exit 1
        ;;
esac

BUILD_DIR="build"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "======================================"
echo "DRX Engine Build Script"
echo "======================================"
echo "Build Type: $BUILD_TYPE"
echo "Build Directory: $BUILD_DIR"
echo ""

# Clean if requested
if [ "$CLEAN" == "clean" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "Configuring CMake..."
cmake "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DDRX_BUILD_EDITOR=ON \
    -DDRX_BUILD_TESTS=ON \
    -DDRX_BUILD_EXAMPLES=ON \
    -DDRX_ENABLE_VALIDATION=ON

# Build
echo ""
echo "Building..."
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build . --parallel "$NPROC"

echo ""
echo "======================================"
echo "Build Complete!"
echo "======================================"
echo ""
echo "Executables:"
ls -la bin/ 2>/dev/null || echo "No executables found"
echo ""
echo "Libraries:"
ls -la lib/ 2>/dev/null || echo "No libraries found"
echo ""
echo "To run the demo:"
echo "  ./build/bin/DRXTriangleDemo"
echo ""
echo "To run tests:"
echo "  cd build && ctest --output-on-failure"
