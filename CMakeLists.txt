cmake_minimum_required(VERSION 3.22)
project(blaze_c LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------------------
# 1. Target Windows 10+ and Inject Missing MinGW HKDF Definitions
# ----------------------------------------------------------------------
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
        NTDDI_VERSION=0x0A000007
    )
    
#        NOMINMAX
#        WIN32_LEAN_AND_MEAN
endif()

if(MSVC)
    add_compile_options(
        /std:c++latest
        /EHsc
        /Zc:__cplusplus
        /permissive-
        /utf-8
    )
endif()

# Fix for Ubuntu 22.04 MinGW missing Windows 10 RS4 HKDF definitions in <bcrypt.h>
if(MINGW)
    set(MINGW_BCRYPT_COMPAT "${CMAKE_CURRENT_BINARY_DIR}/mingw_bcrypt_compat.h")
    file(WRITE "${MINGW_BCRYPT_COMPAT}" [==[
#pragma once
#ifndef BCRYPT_HKDF_ALGORITHM
#define BCRYPT_HKDF_ALGORITHM L"HKDF"
#endif
#ifndef BCRYPT_HKDF_HASH_ALGORITHM
#define BCRYPT_HKDF_HASH_ALGORITHM L"HkdfHashAlgorithm"
#endif
#ifndef BCRYPT_HKDF_PRK_AND_FINALIZE
#define BCRYPT_HKDF_PRK_AND_FINALIZE L"HkdfPrkAndFinalize"
#endif
#ifndef BCRYPT_HKDF_SALT_AND_FINALIZE
#define BCRYPT_HKDF_SALT_AND_FINALIZE L"HkdfSaltAndFinalize"
#endif
#ifndef KDF_HKDF_INFO
#define KDF_HKDF_INFO 0x14
#endif
#ifndef KDF_HKDF_SALT
#define KDF_HKDF_SALT 0x13
#endif
]==])

    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-include${MINGW_BCRYPT_COMPAT}>
        $<$<COMPILE_LANGUAGE:C>:-include${MINGW_BCRYPT_COMPAT}>
    )
endif()

# ----------------------------------------------------------------------
# 2. Fetch Sourcemeta Blaze & Apply Patches
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

        # ------------------------------------------------------------------
    # Automatically apply all patches from the ${CMAKE_CURRENT_SOURCE_DIR}/patches directory
    # ------------------------------------------------------------------
    file(GLOB BLAZE_PATCH_FILES 
        "${CMAKE_CURRENT_SOURCE_DIR}/patches/*.diff" 
        "${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch"
    )

    if(BLAZE_PATCH_FILES)
        message(STATUS "==> Found ${BLAZE_PATCH_FILES} patch(es) in patches/")
        message(STATUS "==> [Patch] Applying: blaze_SOURCE_DIR:${blaze_SOURCE_DIR}...")
        foreach(PATCH_FILE IN LISTS BLAZE_PATCH_FILES)
            get_filename_component(PATCH_NAME "${PATCH_FILE}" NAME)

            # Check if patch is already applied (exit code 0 = already present)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${PATCH_FILE}"
                WORKING_DIRECTORY "${blaze_SOURCE_DIR}"
                RESULT_VARIABLE PATCH_ALREADY_APPLIED
                OUTPUT_QUIET
                ERROR_QUIET
            )

            if(PATCH_ALREADY_APPLIED EQUAL 0)
                message(STATUS "==> [Patch] Already applied: ${PATCH_NAME} (skipping)")
            else()
                message(STATUS "==> [Patch] Applying: ${PATCH_NAME}...")
                execute_process(
                    COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace --whitespace=nowarn "${PATCH_FILE}"
                    WORKING_DIRECTORY "${blaze_SOURCE_DIR}"
                    RESULT_VARIABLE PATCH_RESULT
                    OUTPUT_VARIABLE PATCH_OUT
                    ERROR_VARIABLE PATCH_ERR
                )

                if(NOT PATCH_RESULT EQUAL 0)
                    message(FATAL_ERROR "==> [Patch] Failed to apply ${PATCH_NAME}!\nError:\n${PATCH_ERR}")
                else()
                    message(STATUS "==> [Patch] Successfully applied ${PATCH_NAME}")
                endif()
            endif()
        endforeach()
    else()
        message(STATUS "==> No patch files found in patches/ directory.")
    endif()

    # Patch numeric_uint128.h for ARM64 if provided
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
# 3. Include Directories
# ----------------------------------------------------------------------
file(GLOB BLAZE_INTERNAL_INCLUDES "${blaze_SOURCE_DIR}/src/*/include")

# ----------------------------------------------------------------------
# 4. Target Library
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
# 5. Installation Rules & Test Target
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