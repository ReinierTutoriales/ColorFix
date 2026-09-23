cmake_minimum_required(VERSION 3.25)
project(ColorFix VERSION 0.1.0 LANGUAGES CXX)

set(COLORFIX_EXPECTED_ARCH "" CACHE STRING "Expected architecture: X64, X86 or ARM64")
if(COLORFIX_EXPECTED_ARCH)
  string(TOUPPER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" _cf_arch)
  if(NOT _cf_arch STREQUAL COLORFIX_EXPECTED_ARCH)
    message(FATAL_ERROR
      "Preset expects ${COLORFIX_EXPECTED_ARCH}, but compiler architecture is "
      "'${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}'. Use the matching compiler environment.")
  endif()
endif()

if(NOT WIN32)
  message(FATAL_ERROR "ColorFix targets Windows 11.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(colorfix_core_check OBJECT tests/core_check.cpp)
target_include_directories(colorfix_core_check PRIVATE "${PROJECT_SOURCE_DIR}/core")

add_subdirectory(tests/win32)
