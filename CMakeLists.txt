cmake_minimum_required(VERSION 3.22)
project(blaze_c LANGUAGES C CXX)

# Require C++23 for std::unreachable, std::to_underlying, and if consteval
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC specific standard and conformance flags
if(MSVC)
    add_compile_options(
        /std:c++latest
        /EHsc
        /Zc:__cplusplus
        /permissive-
        /utf-8
    )
endif()

if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
        NTDDI_VERSION=0x0A000007
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# ----------------------------------------------------------------------
# Fetch Sourcemeta Blaze & Apply Patches
# ----------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    blaze
    GIT_REPOSITORY https://github.com/sourcemeta/blaze.git
    GIT_TAG        main
)

set(BUILD_TESTING_SAVED ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(SOURCEMETA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SOURCEMETA_BUILD_CLI OFF CACHE BOOL "" FORCE)

FetchContent_GetProperties(blaze)
if(NOT blaze_POPULATED)
    FetchContent_Populate(blaze)

    # Patch numeric_uint128.h if present
    set(PATCHED_FILE "${CMAKE_CURRENT_SOURCE_DIR}/patched-numeric_uint128.h")
    set(TARGET_FILE  "${blaze_SOURCE_DIR}/vendor/core/src/lang/numeric/include/sourcemeta/core/numeric_uint128.h")
    if(EXISTS "${PATCHED_FILE}")
        message(STATUS "Applying patch: replacing numeric_uint128.h with patched-numeric_uint128.h")
        configure_file("${PATCHED_FILE}" "${TARGET_FILE}" COPYONLY)
    endif()

    add_subdirectory(${blaze_SOURCE_DIR} ${blaze_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

set(BUILD_TESTING ${BUILD_TESTING_SAVED} CACHE BOOL "" FORCE)

# ----------------------------------------------------------------------
# Include Paths
# ----------------------------------------------------------------------
file(GLOB BLAZE_INTERNAL_INCLUDES "${blaze_SOURCE_DIR}/src/*/include")

# ----------------------------------------------------------------------
# Target Library
# ----------------------------------------------------------------------
add_library(blaze_c SHARED
    blaze_c.cpp
    blaze_c.h
)

target_include_directories(blaze_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${BLAZE_INTERNAL_INCLUDES}
        ${blaze_SOURCE_DIR}/include
        ${blaze_SOURCE_DIR}/vendor/core/src/json/include
        ${blaze_SOURCE_DIR}/vendor/core/src/schema/include
        ${blaze_SOURCE_DIR}/vendor/core/include
)

target_compile_definitions(blaze_c PRIVATE BLAZE_BUILD_DLL)

target_link_libraries(blaze_c PRIVATE
    sourcemeta_blaze_compiler
    sourcemeta_blaze_evaluator
    sourcemeta_blaze_foundation
    sourcemeta_core
)

if(WIN32)
    target_link_libraries(blaze_c PRIVATE bcrypt ws2_32)
endif()

if(MINGW)
    target_link_options(blaze_c PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ----------------------------------------------------------------------
# Installation Rules & Tests
# ----------------------------------------------------------------------
install(TARGETS blaze_c
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(FILES blaze_c.h DESTINATION include)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_blaze_c examples/main.c)
    target_link_libraries(test_blaze_c PRIVATE blaze_c)
    target_include_directories(test_blaze_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    if(WIN32)
        set_target_properties(test_blaze_c PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        )
    endif()

    add_test(NAME test_blaze_c COMMAND test_blaze_c)
endif()