cmake_minimum_required(VERSION 3.24)
project(context_overlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# GUI subsystem executable (no console window).
add_executable(context_overlay WIN32
    src/main.cpp
    src/overlay_window.cpp
    src/dwell_coordinator.cpp
)

target_compile_definitions(context_overlay PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Phase One links only what it actually uses. Later phases add, per the
# architecture document:
#   ole32 oleaut32 uiautomationcore d2d1 dwrite winhttp
target_link_libraries(context_overlay PRIVATE
    user32
    gdi32
)

if(MSVC)
    target_compile_options(context_overlay PRIVATE /W4 /permissive- /utf-8)
elseif(MINGW)
    target_compile_options(context_overlay PRIVATE -Wall -Wextra -Wpedantic -municode)
    target_link_options(context_overlay PRIVATE -municode)
endif()
