cmake_minimum_required(VERSION 3.20)

project(DRXEngine
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Autonomous AI-Powered Game Engine"
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Options
option(DRX_BUILD_EDITOR "Build the DRX Editor" ON)
option(DRX_BUILD_TESTS "Build test suite" ON)
option(DRX_ENABLE_VALIDATION "Enable Vulkan validation layers" ON)
option(DRX_BUILD_EXAMPLES "Build example projects" ON)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDRX_DEBUG=1")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /DDRX_DEBUG=1")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find packages (Vulkan optional for demos)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Vulkan found: ${Vulkan_LIBRARY}")
else()
    message(STATUS "Vulkan not found - building without Vulkan support")
endif()

find_package(OpenGL QUIET)
if(OpenGL_FOUND)
    message(STATUS "OpenGL found")
else()
    message(STATUS "OpenGL not found")
endif()
find_package(glfw3 3.3 QUIET)
find_package(glm QUIET)

# If packages not found via find_package, use bundled versions
if(NOT glfw3_FOUND)
    message(STATUS "GLFW not found, using bundled version")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    if(EXISTS ${CMAKE_SOURCE_DIR}/ThirdParty/glfw/CMakeLists.txt)
        add_subdirectory(ThirdParty/glfw)
    endif()
endif()

if(NOT glm_FOUND)
    message(STATUS "GLM not found, using bundled version")
    if(EXISTS ${CMAKE_SOURCE_DIR}/ThirdParty/glm/CMakeLists.txt)
        add_subdirectory(ThirdParty/glm)
    else()
        # GLM is header-only, just add include path
        set(GLM_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/ThirdParty/glm)
    endif()
endif()

# Third party libraries
add_subdirectory(ThirdParty)

# Engine library (requires Vulkan)
if(Vulkan_FOUND)
    add_subdirectory(Engine)

    # Editor (requires Engine)
    if(DRX_BUILD_EDITOR)
        add_subdirectory(Editor)
    endif()

    # Tests (requires Engine)
    if(DRX_BUILD_TESTS)
        enable_testing()
        add_subdirectory(Tests)
    endif()

    # Examples (requires Engine)
    if(DRX_BUILD_EXAMPLES AND EXISTS ${CMAKE_SOURCE_DIR}/Examples/CMakeLists.txt)
        add_subdirectory(Examples)
    endif()
else()
    message(STATUS "Skipping Engine/Editor/Tests (no Vulkan)")
endif()

# Samples - standalone demos (no Engine required)
if(DRX_BUILD_EXAMPLES)
    add_subdirectory(Samples)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "=== DRX Engine Configuration ===")
message(STATUS "Version:          ${PROJECT_VERSION}")
message(STATUS "Build Type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Vulkan:           ${Vulkan_FOUND}")
message(STATUS "Build Editor:     ${DRX_BUILD_EDITOR}")
message(STATUS "Build Tests:      ${DRX_BUILD_TESTS}")
message(STATUS "Validation:       ${DRX_ENABLE_VALIDATION}")
message(STATUS "================================")
message(STATUS "")
