# Third Party Libraries for DRX Engine
# All targets are built as static libraries so the engine links them privately
# and third-party warnings never leak into engine targets.

# ----------------------------------------------------------------------------
# STB (header-only, implementation compiled once in the engine)
# ----------------------------------------------------------------------------
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# ----------------------------------------------------------------------------
# Lua 5.5 (C library)
# ----------------------------------------------------------------------------
set(LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lua)
file(GLOB LUA_SOURCES ${LUA_DIR}/*.c)
list(REMOVE_ITEM LUA_SOURCES
    ${LUA_DIR}/lua.c
    ${LUA_DIR}/luac.c
    ${LUA_DIR}/onelua.c
    ${LUA_DIR}/ltests.c
)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua SYSTEM PUBLIC ${LUA_DIR})
set_target_properties(lua PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(UNIX AND NOT APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua PRIVATE m dl)
elseif(APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
endif()

# ----------------------------------------------------------------------------
# Bullet Physics (LinearMath + Collision + Dynamics via unity sources)
# ----------------------------------------------------------------------------
set(BULLET_SRC ${CMAKE_CURRENT_SOURCE_DIR}/bullet3/src)
add_library(bullet STATIC
    ${BULLET_SRC}/btLinearMathAll.cpp
    ${BULLET_SRC}/btBulletCollisionAll.cpp
    ${BULLET_SRC}/btBulletDynamicsAll.cpp
)
target_include_directories(bullet SYSTEM PUBLIC ${BULLET_SRC})
set_target_properties(bullet PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bullet PRIVATE -w)
endif()

# ----------------------------------------------------------------------------
# Assimp (model import: OBJ, FBX, glTF)
# ----------------------------------------------------------------------------
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_USE_CCACHE OFF CACHE BOOL "" FORCE)
set(ASSIMP_IGNORE_GIT_HASH ON CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(assimp EXCLUDE_FROM_ALL)
unset(CMAKE_POSITION_INDEPENDENT_CODE)

# ----------------------------------------------------------------------------
# ImGui (GLFW + OpenGL3 backend, embedded GL loader)
# ----------------------------------------------------------------------------
find_package(OpenGL QUIET)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp AND OpenGL_FOUND AND TARGET glfw)
    add_library(imgui STATIC
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/backends/imgui_impl_glfw.cpp
        imgui/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
    )
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})
    if(UNIX AND NOT APPLE)
        # Docking backend talks to X11 directly for multi-viewport support
        find_package(X11 REQUIRED)
        target_link_libraries(imgui PRIVATE X11::X11)
    endif()
    message(STATUS "ImGui configured with OpenGL3/GLFW backend")
else()
    message(STATUS "ImGui skipped (missing OpenGL or GLFW)")
endif()

# ----------------------------------------------------------------------------
# EnTT - Entity Component System (header-only)
# ----------------------------------------------------------------------------
add_library(EnTT INTERFACE)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/entt/single_include)
    target_include_directories(EnTT SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/entt/single_include)
else()
    target_include_directories(EnTT SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/entt/src)
endif()

# ----------------------------------------------------------------------------
# spdlog (bundled fmt is used by the engine logger)
# ----------------------------------------------------------------------------
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_PIC ON CACHE BOOL "" FORCE)
add_subdirectory(spdlog)

# ----------------------------------------------------------------------------
# RapidJSON (header-only)
# ----------------------------------------------------------------------------
add_library(rapidjson INTERFACE)
target_include_directories(rapidjson SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/rapidjson/include)

# ----------------------------------------------------------------------------
# VMA - Vulkan Memory Allocator (header-only)
# ----------------------------------------------------------------------------
add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vma/include)

message(STATUS "Third party libraries configured")
