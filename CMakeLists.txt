cmake_minimum_required(VERSION 3.22)
project(blaze_c LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    blaze
    GIT_REPOSITORY https://github.com/sourcemeta/blaze.git
    GIT_TAG        main
)

# ----------------------------------------------------------------------
# Prevent upstream dependencies from building their POSIX test/CLI harnesses
# ----------------------------------------------------------------------
set(BUILD_TESTING_SAVED ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(SOURCEMETA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SOURCEMETA_BUILD_CLI OFF CACHE BOOL "" FORCE)

FetchContent_GetProperties(blaze)
if(NOT blaze_POPULATED)
    FetchContent_Populate(blaze)
    # EXCLUDE_FROM_ALL ensures only targets needed by blaze_c are built
    add_subdirectory(${blaze_SOURCE_DIR} ${blaze_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Restore BUILD_TESTING for our own test suite
set(BUILD_TESTING ${BUILD_TESTING_SAVED} CACHE BOOL "" FORCE)

# ----------------------------------------------------------------------
# Header Search Paths from Fetched Submodules
# ----------------------------------------------------------------------
file(GLOB BLAZE_INTERNAL_INCLUDES "${blaze_SOURCE_DIR}/src/*/include")

# ----------------------------------------------------------------------
# C Wrapper Shared Library
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
        ${blaze_SOURCE_DIR}/vendor/core/include
)

target_compile_definitions(blaze_c PRIVATE BLAZE_BUILD_DLL)

# Link only the validation submodules
target_link_libraries(blaze_c PRIVATE
    sourcemeta_blaze_compiler
    sourcemeta_blaze_evaluator
    sourcemeta_blaze_foundation
    sourcemeta_core
)

if(MINGW)
    target_link_options(blaze_c PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ----------------------------------------------------------------------
# Installation Rules
# ----------------------------------------------------------------------
install(TARGETS blaze_c
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(FILES blaze_c.h DESTINATION include)

# ----------------------------------------------------------------------
# Wrapper Test Executable
# ----------------------------------------------------------------------
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