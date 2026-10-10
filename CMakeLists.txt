cmake_minimum_required(VERSION 3.20)

project(DRXEngine
    VERSION 1.0.0
    LANGUAGES C CXX
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

# Warnings are applied per target so third-party code keeps its own settings
function(drx_set_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endif()
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:DRX_DEBUG=1>)
endfunction()

# /utf-8 stays per target: assimp adds /source-charset:utf-8, which MSVC rejects together with it
if(MSVC)
    add_compile_options(/MP)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

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
if(NOT glfw3_FOUND)
    message(STATUS "GLFW not found, using bundled version")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    add_subdirectory(ThirdParty/glfw)
endif()

# Third party libraries
add_subdirectory(ThirdParty)

# Engine library (requires Vulkan)
if(Vulkan_FOUND)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
    include(DRXShaders)
    add_subdirectory(Engine)

    # Editor (requires Engine)
    if(DRX_BUILD_EDITOR AND TARGET imgui)
        add_subdirectory(Editor)
    elseif(DRX_BUILD_EDITOR)
        message(STATUS "Skipping Editor (ImGui requires OpenGL and GLFW)")
    endif()

    # Tests (requires Engine)
    if(DRX_BUILD_TESTS)
        enable_testing()
        add_subdirectory(Tests)
    endif()

    # Games (requires Engine)
    if(EXISTS ${CMAKE_SOURCE_DIR}/Games/CMakeLists.txt)
        add_subdirectory(Games)
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
