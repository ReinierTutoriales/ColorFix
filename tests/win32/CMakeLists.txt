add_executable(ColorFixTest WIN32 main.cpp)

target_include_directories(ColorFixTest PRIVATE
  "${PROJECT_SOURCE_DIR}/core"
)

target_compile_definitions(ColorFixTest PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_link_libraries(ColorFixTest PRIVATE
  comctl32
  gdi32
  user32
)
