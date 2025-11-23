# DRX Engine - Battlefield Demo

add_executable(BattlefieldDemo main.cpp)

target_link_libraries(BattlefieldDemo PRIVATE glfw)

find_package(OpenGL REQUIRED)
target_link_libraries(BattlefieldDemo PRIVATE OpenGL::GL)

if(UNIX AND NOT APPLE)
    target_link_libraries(BattlefieldDemo PRIVATE m)
endif()

set_target_properties(BattlefieldDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
