# SPDX-License-Identifier: MIT
#
# EDGE Glasses C/C++ SDK
#
#   cmake -S . -B build
#   cmake --build build --config Release
#   ctest --test-dir build --output-on-failure
#
# On Windows/MSVC the WinRT Bluetooth transport is built by default. Everywhere
# else (and under MinGW/GCC/Clang) the SDK builds transport-agnostic: bring your
# own BLE stack via edge::CallbackTransport or the C callback API.

cmake_minimum_required(VERSION 3.15)
project(edge_glasses VERSION 2.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# C++/WinRT is an MSVC-only projection, so default the transport on there alone.
if(MSVC)
    option(EDGE_WITH_WINRT "Build the Windows (WinRT) BLE transport" ON)
else()
    option(EDGE_WITH_WINRT "Build the Windows (WinRT) BLE transport" OFF)
endif()
option(EDGE_BUILD_TESTS "Build the test suites" ON)
option(EDGE_BUILD_EXAMPLES "Build the examples" ON)
option(EDGE_BUILD_SHARED "Build a shared library (DLL) instead of static" OFF)

set(EDGE_SOURCES
    src/glasses.cpp
    src/feedback_stream.cpp
    src/c_api.cpp
)

if(EDGE_WITH_WINRT)
    if(NOT WIN32)
        message(FATAL_ERROR "EDGE_WITH_WINRT requires Windows")
    endif()
    if(NOT MSVC)
        message(FATAL_ERROR
            "EDGE_WITH_WINRT requires MSVC: C++/WinRT is not available under MinGW or GCC. "
            "Configure with -DEDGE_WITH_WINRT=OFF and supply your own transport.")
    endif()
    list(APPEND EDGE_SOURCES src/winrt_transport.cpp)
endif()

if(EDGE_BUILD_SHARED)
    add_library(edge_glasses SHARED ${EDGE_SOURCES})
    target_compile_definitions(edge_glasses
        PUBLIC EDGE_GLASSES_SHARED
        PRIVATE EDGE_GLASSES_BUILDING)
    set_target_properties(edge_glasses PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
else()
    add_library(edge_glasses STATIC ${EDGE_SOURCES})
endif()

add_library(edge::glasses ALIAS edge_glasses)

target_include_directories(edge_glasses PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

if(EDGE_WITH_WINRT)
    target_compile_definitions(edge_glasses PUBLIC EDGE_WITH_WINRT)
    # C++/WinRT needs standard exception handling. The transport blocks on
    # IAsyncOperation::get() rather than co_await, so no coroutine flag is needed.
    target_compile_options(edge_glasses PRIVATE /EHsc)
    target_link_libraries(edge_glasses PRIVATE windowsapp)
endif()

find_package(Threads REQUIRED)
target_link_libraries(edge_glasses PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(edge_glasses PRIVATE /W4)
else()
    target_compile_options(edge_glasses PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(EDGE_BUILD_TESTS)
    enable_testing()

    add_executable(edge_tests tests/test_main.cpp)
    target_link_libraries(edge_tests PRIVATE edge_glasses)
    target_include_directories(edge_tests PRIVATE tests)
    add_test(NAME cpp_sdk COMMAND edge_tests)

    # Compiled as C on purpose: proves edge_glasses.h is usable from pure C.
    add_executable(edge_c_tests tests/test_c_api.c)
    target_link_libraries(edge_c_tests PRIVATE edge_glasses)
    set_target_properties(edge_c_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME c_api COMMAND edge_c_tests)

    # Cross-language check: every frame must match the reference Python SDK.
    add_executable(edge_golden_check tests/golden_check.cpp)
    target_link_libraries(edge_golden_check PRIVATE edge_glasses)
    target_include_directories(edge_golden_check PRIVATE tests)
    add_test(NAME golden_vectors
             COMMAND edge_golden_check
                     ${CMAKE_CURRENT_SOURCE_DIR}/../tools/golden/golden_frames.txt)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

if(EDGE_BUILD_EXAMPLES)
    add_executable(edge_example_basic examples/01_basic.cpp)
    target_link_libraries(edge_example_basic PRIVATE edge_glasses)

    add_executable(edge_example_screen_dimmer examples/02_screen_dimmer.cpp)
    target_link_libraries(edge_example_screen_dimmer PRIVATE edge_glasses)

    add_executable(edge_example_breathe examples/03_breathe.cpp)
    target_link_libraries(edge_example_breathe PRIVATE edge_glasses)

    add_executable(edge_example_c examples/c_api_demo.c)
    target_link_libraries(edge_example_c PRIVATE edge_glasses)
    set_target_properties(edge_example_c PROPERTIES LINKER_LANGUAGE CXX)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

include(GNUInstallDirs)
install(TARGETS edge_glasses
        EXPORT edge_glassesTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT edge_glassesTargets
        NAMESPACE edge::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/edge_glasses)
