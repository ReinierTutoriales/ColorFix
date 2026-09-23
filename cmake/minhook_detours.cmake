# MinHook-Detours (MIT) pinned by commit. Upstream ships only a .vcxproj
# (DLL + MinHook.def), so ColorFix builds its own static target.
include(FetchContent)

FetchContent_Declare(minhook_detours
  GIT_REPOSITORY https://github.com/m417z/minhook-detours.git
  GIT_TAG        e7d5427ede9e658f1810e742822fab2936e058da
)
# No CMakeLists.txt at the upstream root: MakeAvailable only populates.
FetchContent_MakeAvailable(minhook_detours)

set(_mhd "${minhook_detours_SOURCE_DIR}/src")

add_library(minhook_detours STATIC
  "${_mhd}/MinHook.c"
  "${_mhd}/SlimDetours/Disassembler.c"
  "${_mhd}/SlimDetours/InlineHook.c"
  "${_mhd}/SlimDetours/Instruction.c"
  "${_mhd}/SlimDetours/Memory.c"
  "${_mhd}/SlimDetours/Thread.c"
  "${_mhd}/SlimDetours/Trampoline.c"
  "${_mhd}/SlimDetours/Transaction.c"
)
target_include_directories(minhook_detours
  PUBLIC  "${_mhd}"
  PRIVATE "${_mhd}/phnt"
)
target_compile_definitions(minhook_detours PRIVATE UNICODE _UNICODE)
target_link_libraries(minhook_detours PUBLIC ntdll)
