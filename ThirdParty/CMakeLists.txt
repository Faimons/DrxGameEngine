# Third Party Libraries for DRX Engine

# STB - Single-file public domain libraries
# Header-only, just need to create implementation file
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# ImGui - Immediate Mode GUI
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp)
    add_library(imgui STATIC
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/backends/imgui_impl_glfw.cpp
        imgui/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
    )
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
    if(TARGET glfw)
        target_link_libraries(imgui PUBLIC glfw)
    endif()
    # Link OpenGL
    find_package(OpenGL REQUIRED)
    target_link_libraries(imgui PUBLIC OpenGL::GL)
endif()

# EnTT - Entity Component System (Header-only)
add_library(EnTT INTERFACE)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/entt/single_include)
    target_include_directories(EnTT INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/entt/single_include)
else()
    target_include_directories(EnTT INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/entt)
endif()

# spdlog - Fast C++ logging library
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/CMakeLists.txt)
    set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    add_subdirectory(spdlog)
else()
    # Create interface target for header-only usage
    add_library(spdlog INTERFACE)
    target_include_directories(spdlog INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include)
endif()

# VMA - Vulkan Memory Allocator (Header-only)
add_library(vma INTERFACE)
target_include_directories(vma INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vma)

message(STATUS "Third party libraries configured")
