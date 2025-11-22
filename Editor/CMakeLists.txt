# DRX Editor Application

add_executable(DRXEditor)

target_sources(DRXEditor PRIVATE
    Source/Main.cpp
)

target_link_libraries(DRXEditor PRIVATE DRXEngine)

# Platform-specific
if(WIN32)
    # Windows subsystem for release builds
    set_target_properties(DRXEditor PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
endif()

# Install
install(TARGETS DRXEditor RUNTIME DESTINATION bin)
