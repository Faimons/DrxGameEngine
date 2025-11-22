# DRX Engine Examples

# Triangle Demo
add_executable(DRXTriangleDemo
    ${CMAKE_SOURCE_DIR}/Engine/Source/Main.cpp
)

target_link_libraries(DRXTriangleDemo PRIVATE DRXEngine)

# Install
install(TARGETS DRXTriangleDemo RUNTIME DESTINATION bin)
