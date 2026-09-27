# Standalone host tool: no engine dependency, built with the default (host)
# toolchain so it can run during the build to compile Assets/Localization
# .str sources into .csf files embedded in the game exe.
add_executable(str2csf str2csf.cpp)
set_target_properties(str2csf PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WIN32 OR "${CMAKE_SYSTEM}" MATCHES "Windows")
    target_link_options(str2csf PRIVATE /subsystem:console)
endif()
