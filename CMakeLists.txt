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
FetchContent_MakeAvailable(blaze)

# Shared library
add_library(blaze_c SHARED
    blaze_c.cpp
    blaze_c.h
)

target_include_directories(blaze_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(blaze_c PRIVATE BLAZE_BUILD_DLL)
target_link_libraries(blaze_c PRIVATE sourcemeta::blaze)

if(MINGW)
    target_link_options(blaze_c PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Installation rules
install(TARGETS blaze_c
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(FILES blaze_c.h DESTINATION include)

# --- Test Target ---
include(CTest)
if(BUILD_TESTING)
    add_executable(test_blaze_c examples/main.c)
    target_link_libraries(test_blaze_c PRIVATE blaze_c)
    target_include_directories(test_blaze_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    if(WIN32)
        # Ensure executable finds the DLL in the same folder during tests
        set_target_properties(test_blaze_c PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        )
    endif()

    add_test(NAME test_blaze_c COMMAND test_blaze_c)
endif()