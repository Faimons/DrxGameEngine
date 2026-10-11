# SHIFT - team shooter on shifting geometry

# Simulation shared by server and client
add_library(ShiftCore STATIC
    Source/Core/ShiftConfig.cpp
    Source/Core/ShiftLevel.cpp
    Source/Core/ShiftNet.cpp
    Source/Core/ShiftSchedule.cpp
    Source/Core/ShiftWeapons.cpp
    Source/Core/ShiftWorld.cpp
)
target_include_directories(ShiftCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core)
target_link_libraries(ShiftCore PUBLIC DRXEngine)
drx_set_warnings(ShiftCore)

add_executable(ShiftGame Source/Game/main.cpp)
target_link_libraries(ShiftGame PRIVATE ShiftCore)
target_compile_definitions(ShiftGame PRIVATE SHIFT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shift.json"
                                             SHIFT_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Assets")
drx_set_warnings(ShiftGame)
if(WIN32)
    # Game window without an extra console window
    set_target_properties(ShiftGame PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(ShiftGame PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/ENTRY:mainCRTStartup>)
endif()

if(TARGET GTest::gtest_main)
    add_executable(ShiftTests Tests/TestShift.cpp)
    target_link_libraries(ShiftTests PRIVATE ShiftCore GTest::gtest_main)
    target_compile_definitions(ShiftTests PRIVATE SHIFT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shift.json"
                                                  SHIFT_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Assets")
    drx_set_warnings(ShiftTests)
    include(GoogleTest)
    gtest_discover_tests(ShiftTests)
endif()
