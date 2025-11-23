# DRX Editor Application

add_executable(DRXEditor)

target_sources(DRXEditor PRIVATE
    Source/Main.cpp
    Source/EditorApp.cpp
)

target_include_directories(DRXEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_link_libraries(DRXEditor PRIVATE
    DRXEngine
    imgui
    glfw
)

# Find OpenGL
find_package(OpenGL REQUIRED)
target_link_libraries(DRXEditor PRIVATE OpenGL::GL)

# Platform-specific
if(WIN32)
    # Windows subsystem for release builds
    set_target_properties(DRXEditor PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
endif()

# Install
install(TARGETS DRXEditor RUNTIME DESTINATION bin)
