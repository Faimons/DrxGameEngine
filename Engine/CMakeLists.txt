
# DRX Engine Library
add_library(DRXEngine SHARED)

# Collect source files
file(GLOB_RECURSE ENGINE_SOURCES
    "Source/Core/Private/*.cpp"
    "Source/HAL/Private/*.cpp"
    "Source/Renderer/Private/*.cpp"
    "Source/Physics/Private/*.cpp"
    "Source/Animation/Private/*.cpp"
    "Source/Audio/Private/*.cpp"
    "Source/Scene/Private/*.cpp"
    "Source/Resource/Private/*.cpp"
    "Source/Scripting/Private/*.cpp"
)

file(GLOB_RECURSE ENGINE_HEADERS
    "Source/Core/Public/*.h"
    "Source/HAL/Public/*.h"
    "Source/Renderer/Public/*.h"
    "Source/Physics/Public/*.h"
    "Source/Animation/Public/*.h"
    "Source/Audio/Public/*.h"
    "Source/Scene/Public/*.h"
    "Source/Resource/Public/*.h"
    "Source/Scripting/Public/*.h"
)

target_sources(DRXEngine PRIVATE ${ENGINE_SOURCES} ${ENGINE_HEADERS})

# Include directories
target_include_directories(DRXEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/HAL/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Renderer/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Physics/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Animation/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Scene/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Resource/Public
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Scripting/Public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/Private
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/HAL/Private
)

# Include third-party headers
target_include_directories(DRXEngine
    PRIVATE
        ${CMAKE_SOURCE_DIR}/ThirdParty/rapidjson/include
        ${CMAKE_SOURCE_DIR}/ThirdParty/lua
        ${CMAKE_SOURCE_DIR}/ThirdParty/assimp/include
        ${CMAKE_SOURCE_DIR}/ThirdParty/stb
        ${CMAKE_SOURCE_DIR}/ThirdParty/zlib
        ${CMAKE_SOURCE_DIR}/ThirdParty/libpng
)

# Link libraries
target_link_libraries(DRXEngine
    PUBLIC
        Vulkan::Vulkan
    PRIVATE
        spdlog
        EnTT
        vma
)

# Disable warnings for third-party code
if(MSVC)
    target_compile_options(DRXEngine PRIVATE /W4 /wd4251)
endif()

# Conditionally link GLFW
if(TARGET glfw)
    target_link_libraries(DRXEngine PUBLIC glfw)
else()
    find_package(glfw3 REQUIRED)
    target_link_libraries(DRXEngine PUBLIC glfw)
endif()

# GLM include
if(GLM_INCLUDE_DIR)
    target_include_directories(DRXEngine PUBLIC ${GLM_INCLUDE_DIR})
elseif(TARGET glm)
    target_link_libraries(DRXEngine PUBLIC glm)
endif()

# Platform-specific definitions and libraries
if(WIN32)
    target_compile_definitions(DRXEngine
        PRIVATE
            DRX_PLATFORM_WINDOWS=1
            DRX_BUILD_DLL=1
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )
    target_link_libraries(DRXEngine PRIVATE ws2_32)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(DRXEngine
        PRIVATE
            DRX_PLATFORM_LINUX=1
            DRX_BUILD_DLL=1
    )
    target_link_libraries(DRXEngine PRIVATE pthread dl)
elseif(APPLE)
    target_compile_definitions(DRXEngine
        PRIVATE
            DRX_PLATFORM_MACOS=1
            DRX_BUILD_DLL=1
    )
endif()

# Validation layers
if(DRX_ENABLE_VALIDATION)
    target_compile_definitions(DRXEngine PRIVATE DRX_ENABLE_VALIDATION=1)
endif()

# Set library properties
set_target_properties(DRXEngine PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "DRXEngine"
)

# Install rules
install(TARGETS DRXEngine
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY Source/Core/Public/ DESTINATION include/DRX/Core)
install(DIRECTORY Source/HAL/Public/ DESTINATION include/DRX/HAL)
install(DIRECTORY Source/Renderer/Public/ DESTINATION include/DRX/Renderer)
