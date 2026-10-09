# DRX Engine Tests

# Find or fetch Google Test
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Unit tests
add_executable(DRXTests
    UnitTests/TestMath.cpp
    UnitTests/TestCore.cpp
    UnitTests/TestSystems.cpp
)

target_link_libraries(DRXTests PRIVATE
    DRXEngine
    GTest::gtest_main
)
drx_set_warnings(DRXTests)

target_include_directories(DRXTests PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/Source
)

# Register tests
include(GoogleTest)
gtest_discover_tests(DRXTests)
