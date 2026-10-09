# DRX Engine Library

set(DRX_ENGINE_MODULES
    Core
    HAL
    Renderer
    Physics
    Animation
    Audio
    Scene
    Resource
    Scripting
    Input
    Camera
    Particles
    Terrain
    Procedural
    UI
    Debug
    Network
    Game
    Runtime
)

set(ENGINE_SOURCES)
set(ENGINE_HEADERS)
foreach(module ${DRX_ENGINE_MODULES})
    file(GLOB_RECURSE module_sources CONFIGURE_DEPENDS "Source/${module}/Private/*.cpp")
    file(GLOB_RECURSE module_headers CONFIGURE_DEPENDS "Source/${module}/Public/*.h")
    list(APPEND ENGINE_SOURCES ${module_sources})
    list(APPEND ENGINE_HEADERS ${module_headers})
endforeach()

add_library(DRXEngine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})
add_library(DRX::Engine ALIAS DRXEngine)

target_compile_features(DRXEngine PUBLIC cxx_std_17)
target_compile_definitions(DRXEngine PUBLIC DRX_STATIC=1 GLFW_INCLUDE_VULKAN)
set_target_properties(DRXEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)
drx_set_warnings(DRXEngine)

target_include_directories(DRXEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# OpenAL (3D audio). A missing audio device at runtime is handled; the library is required to build.
find_package(OpenAL REQUIRED)
target_include_directories(DRXEngine PRIVATE ${OPENAL_INCLUDE_DIR})
target_link_libraries(DRXEngine PRIVATE ${OPENAL_LIBRARY})

target_link_libraries(DRXEngine
    PUBLIC
        Vulkan::Vulkan
        glfw
        spdlog::spdlog
        rapidjson
    PRIVATE
        bullet
        lua
        assimp
        stb
        EnTT
        vma
        Threads::Threads
        ${CMAKE_DL_LIBS}
)

if(WIN32)
    target_compile_definitions(DRXEngine PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(DRXEngine PRIVATE ws2_32)
endif()

if(DRX_ENABLE_VALIDATION)
    target_compile_definitions(DRXEngine PRIVATE DRX_ENABLE_VALIDATION=1)
endif()

# Engine-internal shaders are compiled to SPIR-V and embedded
set(DRX_ENGINE_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Shaders)
drx_embed_shaders(DRXEngine
    ${DRX_ENGINE_SHADER_DIR}/lit.vert
    ${DRX_ENGINE_SHADER_DIR}/lit.frag
    ${DRX_ENGINE_SHADER_DIR}/shadow.vert
    ${DRX_ENGINE_SHADER_DIR}/sky.vert
    ${DRX_ENGINE_SHADER_DIR}/sky.frag
    ${DRX_ENGINE_SHADER_DIR}/particle.vert
    ${DRX_ENGINE_SHADER_DIR}/particle.frag
    ${DRX_ENGINE_SHADER_DIR}/line.vert
    ${DRX_ENGINE_SHADER_DIR}/line.frag
    ${DRX_ENGINE_SHADER_DIR}/ui.vert
    ${DRX_ENGINE_SHADER_DIR}/ui.frag
)

# Built-in UI font
drx_embed_file(DRXEngine ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Fonts/Roboto-Medium.ttf kFontRoboto)

# Asset shaders are validated at build time and shipped as .spv
file(GLOB DRX_ASSET_SHADERS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/GLSL/*.vert
    ${CMAKE_SOURCE_DIR}/Assets/Shaders/GLSL/*.frag
)
drx_compile_shader_assets(DRXAssetShaders Assets/Shaders/SPIRV ${DRX_ASSET_SHADERS})

install(TARGETS DRXEngine
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY Source/ DESTINATION include/DRX FILES_MATCHING PATTERN "*/Public/*.h")
