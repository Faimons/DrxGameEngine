# DRX Engine - FPS Demo Sample
cmake_minimum_required(VERSION 3.16)
project(FPSDemo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find DRX Engine
set(DRX_ENGINE_DIR "${CMAKE_SOURCE_DIR}/../..")

# Include directories
include_directories(
    ${DRX_ENGINE_DIR}/Engine/Source
)

# Source files
set(SOURCES
    main.cpp
)

# Create executable
add_executable(FPSDemo ${SOURCES})

# Link DRX Engine libraries
target_link_libraries(FPSDemo
    DRXCore
    DRXRenderer
    DRXPhysics
    DRXAudio
    DRXInput
    DRXGame
    DRXCamera
    DRXTerrain
    DRXDebug
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(FPSDemo PRIVATE DRX_PLATFORM_WINDOWS)
elseif(UNIX)
    target_compile_definitions(FPSDemo PRIVATE DRX_PLATFORM_LINUX)
endif()

# Copy assets
add_custom_command(TARGET FPSDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${DRX_ENGINE_DIR}/Assets"
    "$<TARGET_FILE_DIR:FPSDemo>/Assets"
)
