cmake_minimum_required(VERSION 3.20)

project(HardwareTaskbar VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files
set(SOURCES
    src/main.cpp
    src/Common.hpp
    src/HardwareMonitor.hpp
    src/HardwareMonitor.cpp
    src/NVMLWrapper.hpp
    src/NVMLWrapper.cpp
    src/OverlayWidget.hpp
    src/OverlayWidget.cpp
    src/IconRenderer.hpp
    src/IconRenderer.cpp
    src/TrayIcon.hpp
    src/TrayIcon.cpp
)

# Win32 GUI application (No console window)
add_executable(HardwareTaskbar WIN32 ${SOURCES} src/app.manifest)

target_include_directories(HardwareTaskbar PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Required Win32 libraries
target_link_libraries(HardwareTaskbar PRIVATE
    user32
    gdi32
    gdiplus
    shell32
    advapi32
    comctl32
    pdh
    dwmapi
)

# MSVC-specific optimizations, UTF-8 source charset, and DPI manifest embedding
if(MSVC)
    target_compile_options(HardwareTaskbar PRIVATE
        /W4
        /permissive-
        /utf-8
        $<$<CONFIG:Release>:/O2 /Oi /Gy>
    )

    target_link_options(HardwareTaskbar PRIVATE
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest"
        $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>
    )
endif()
