# SHIFT - team shooter on shifting geometry

# Simulation shared by server and client
add_library(ShiftCore STATIC
    Source/Core/ShiftConfig.cpp
    Source/Core/ShiftLevel.cpp
    Source/Core/ShiftWeapons.cpp
    Source/Core/ShiftWorld.cpp
)
target_include_directories(ShiftCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core)
target_link_libraries(ShiftCore PUBLIC DRXEngine)
drx_set_warnings(ShiftCore)

add_executable(ShiftGame Source/Game/main.cpp)
target_link_libraries(ShiftGame PRIVATE ShiftCore)
target_compile_definitions(ShiftGame PRIVATE SHIFT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shift.json")
drx_set_warnings(ShiftGame)

if(TARGET GTest::gtest_main)
    add_executable(ShiftTests Tests/TestShift.cpp)
    target_link_libraries(ShiftTests PRIVATE ShiftCore GTest::gtest_main)
    target_compile_definitions(ShiftTests PRIVATE SHIFT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shift.json")
    drx_set_warnings(ShiftTests)
    include(GoogleTest)
    gtest_discover_tests(ShiftTests)
endif()
