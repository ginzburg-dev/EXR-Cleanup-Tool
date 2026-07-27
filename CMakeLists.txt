cmake_minimum_required(VERSION 3.20)

project(
    EXRCleanupTool
    VERSION 2.0.0
    DESCRIPTION "CLI for removing RenderMan variance channels from OpenEXR files"
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

option(EXR_CLEANUP_BUILD_TESTS "Build the test suite" ON)
option(EXR_CLEANUP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(OpenEXR 3.0 CONFIG REQUIRED)

configure_file(
    cmake/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/exr_cleanup/version.hpp"
    @ONLY
)

add_library(exr_cleanup_core STATIC
    src/cleanup.cpp
)

target_compile_features(exr_cleanup_core PUBLIC cxx_std_17)
target_include_directories(exr_cleanup_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(exr_cleanup_core PUBLIC OpenEXR::OpenEXR)

add_executable(exr-cleanup
    src/main.cpp
)

target_compile_features(exr-cleanup PRIVATE cxx_std_17)
target_include_directories(exr-cleanup
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(exr-cleanup PRIVATE exr_cleanup_core)

set(exr_cleanup_warning_flags)
if(MSVC)
    list(APPEND exr_cleanup_warning_flags /W4 /permissive-)
    if(EXR_CLEANUP_WARNINGS_AS_ERRORS)
        list(APPEND exr_cleanup_warning_flags /WX)
    endif()
else()
    list(APPEND exr_cleanup_warning_flags
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
    if(EXR_CLEANUP_WARNINGS_AS_ERRORS)
        list(APPEND exr_cleanup_warning_flags -Werror)
    endif()
endif()

target_compile_options(exr_cleanup_core PRIVATE ${exr_cleanup_warning_flags})
target_compile_options(exr-cleanup PRIVATE ${exr_cleanup_warning_flags})

install(
    TARGETS exr-cleanup
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(EXR_CLEANUP_BUILD_TESTS)
    include(CTest)

    add_executable(exr_cleanup_tests
        tests/cleanup_tests.cpp
    )
    target_compile_features(exr_cleanup_tests PRIVATE cxx_std_17)
    target_link_libraries(exr_cleanup_tests PRIVATE exr_cleanup_core)
    target_compile_options(exr_cleanup_tests PRIVATE ${exr_cleanup_warning_flags})

    add_test(NAME exr_cleanup_tests COMMAND exr_cleanup_tests)
endif()
