# DRX Engine - FPS Demo Sample

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    main.cpp
)

# Create executable
add_executable(FPSDemo ${SOURCES})

# Include directories
target_include_directories(FPSDemo PRIVATE ${CMAKE_SOURCE_DIR}/ThirdParty/glfw/include)

# Link GLFW and OpenGL
if(TARGET glfw)
    target_link_libraries(FPSDemo PRIVATE glfw)
endif()

# Link OpenGL and GLU
if(WIN32)
    target_link_libraries(FPSDemo PRIVATE opengl32 glu32)
elseif(APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(FPSDemo PRIVATE OpenGL::GL OpenGL::GLU)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(FPSDemo PRIVATE OpenGL::GL OpenGL::GLU)
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(FPSDemo PRIVATE DRX_PLATFORM_WINDOWS)
elseif(UNIX)
    target_compile_definitions(FPSDemo PRIVATE DRX_PLATFORM_LINUX)
endif()

# Copy assets
add_custom_command(TARGET FPSDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/Assets"
    "$<TARGET_FILE_DIR:FPSDemo>/Assets"
    COMMENT "Copying assets for FPSDemo..."
)
