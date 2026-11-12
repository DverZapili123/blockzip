cmake_minimum_required(VERSION 3.20)

project(blockzip VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BLOCKZIP_WERROR "Promote compiler warnings to errors" OFF)
option(BLOCKZIP_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Warnings shared by every target: the project is warning-clean by contract.
add_library(blockzip_warnings INTERFACE)
if(MSVC)
    target_compile_options(blockzip_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(blockzip_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(BLOCKZIP_WERROR)
        target_compile_options(blockzip_warnings INTERFACE -Werror)
    endif()
endif()

if(BLOCKZIP_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "BLOCKZIP_SANITIZE requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
    # Prefer an installed GoogleTest, otherwise fetch a pinned release.
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.15.2
            GIT_SHALLOW TRUE
            SYSTEM
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_subdirectory(tests)
endif()
