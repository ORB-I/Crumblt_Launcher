cmake_minimum_required(VERSION 3.20)
project(CrumbltLauncher VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(CrumbltLauncher WIN32
    src/main.cpp
    src/launcher.rc
)

target_link_libraries(CrumbltLauncher PRIVATE
    wininet
    shlwapi
    shell32
    gdiplus
    dwmapi
    comctl32
)

# Disable ALL automatic manifest generation
set_target_properties(CrumbltLauncher PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "CrumbltLauncher"
    LINK_FLAGS "/MANIFEST:NO /MANIFESTUAC:NO"
)

if(MSVC)
    target_compile_options(CrumbltLauncher PRIVATE /O2 /GL)
    target_link_options(CrumbltLauncher PRIVATE /LTCG /SUBSYSTEM:WINDOWS /MANIFEST:NO)
endif()
