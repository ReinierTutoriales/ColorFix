cmake_minimum_required(VERSION 3.25)
project(ColorFix VERSION 0.1.0 LANGUAGES C CXX)

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

include(cmake/minhook_detours.cmake)

add_executable(mhd_smoke tests/backend/mhd_smoke.cpp)
target_link_libraries(mhd_smoke PRIVATE minhook_detours user32)

add_executable(colorfix_probe_infra tests/probe/probe_infra.cpp)
target_compile_definitions(colorfix_probe_infra PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(colorfix_probe_infra PRIVATE user32 gdi32 dwmapi)

add_executable(colorfix_probe_hooks tests/probe/probe_hooks.cpp)
target_compile_definitions(colorfix_probe_hooks PRIVATE
  COLORFIX_PROBE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(colorfix_probe_hooks PRIVATE
  "${PROJECT_SOURCE_DIR}/core" "${PROJECT_SOURCE_DIR}/hooks")
target_link_libraries(colorfix_probe_hooks PRIVATE minhook_detours user32 gdi32 dwmapi advapi32)

add_library(colorfix_core_check OBJECT tests/core_check.cpp)
target_include_directories(colorfix_core_check PRIVATE "${PROJECT_SOURCE_DIR}/core")

add_subdirectory(tests/win32)
