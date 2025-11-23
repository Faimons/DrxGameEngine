# DRX Engine Samples

# Check if GLFW and OpenGL are available
find_package(OpenGL QUIET)

if(TARGET glfw AND OpenGL_FOUND)
    # FPS Demo - Simple shooter
    add_subdirectory(FPSDemo)

    # Battlefield Demo - Advanced FPS with terrain, vehicles, buildings
    add_subdirectory(BattlefieldDemo)

    message(STATUS "Samples configured: FPSDemo, BattlefieldDemo")
else()
    message(STATUS "Samples skipped (requires GLFW and OpenGL)")
    message(STATUS "  To build samples, install: libglfw3-dev libgl1-mesa-dev")
endif()
