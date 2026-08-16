cmake_minimum_required(VERSION 3.24)
project(context_overlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The overlay app is Windows-only. On a non-Windows host (used to run the
# platform-free unit tests) we skip the app target rather than fail to configure.
if(WIN32)

# GUI subsystem executable (no console window).
add_executable(context_overlay WIN32
    src/main.cpp
    src/overlay_window.cpp
    src/dwell_coordinator.cpp
    src/uia_identity.cpp
)

target_compile_definitions(context_overlay PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Link budget, per the architecture document's phased plan:
#   Phase 1: user32 gdi32
#   Phase 2 (+UIA identity): ole32 (COM) oleaut32 (BSTR/SysFreeString)
#   Phase 3 (text grabber): + uiautomationcore   [no MinGW import lib; resolved
#       at runtime via CoCreateInstance, so not added as a link dependency here]
#   Phase 4 (backend):       + winhttp
#   Phase 5 (renderer):      + d2d1 dwrite
target_link_libraries(context_overlay PRIVATE
    user32
    gdi32
    ole32
    oleaut32
)

if(MSVC)
    target_compile_options(context_overlay PRIVATE /W4 /permissive- /utf-8)
elseif(MINGW)
    target_compile_options(context_overlay PRIVATE -Wall -Wextra -Wpedantic -municode)
    target_link_options(context_overlay PRIVATE -municode)
endif()

endif()  # WIN32

# ---------------------------------------------------------------------------
# Tests
#
# src/identity_hash.h is deliberately free of Windows/UIA types so the element
# identity composition can be unit tested with the *host* compiler. Enabled by
# default only for a native build; a cross build (MinGW targeting Windows) can
# still opt in with -DCONTEXT_OVERLAY_BUILD_TESTS=ON if a Wine runner exists.
# ---------------------------------------------------------------------------
option(CONTEXT_OVERLAY_BUILD_TESTS "Build unit tests" ON)

if(CONTEXT_OVERLAY_BUILD_TESTS)
    enable_testing()
    add_executable(identity_hash_test tests/identity_hash_test.cpp)
    target_compile_features(identity_hash_test PRIVATE cxx_std_20)
    add_test(NAME identity_hash COMMAND identity_hash_test)
endif()
