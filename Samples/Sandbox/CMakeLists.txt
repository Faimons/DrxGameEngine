# DRX Engine - Sandbox (renderer and framework test scene)

add_executable(DRXSandbox main.cpp)
target_link_libraries(DRXSandbox PRIVATE DRXEngine)
target_compile_definitions(DRXSandbox PRIVATE DRX_SANDBOX_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/sandbox.json")
