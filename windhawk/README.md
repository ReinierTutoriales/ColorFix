# Windhawk prototype

The Windhawk artifact is deliberately not part of the CMake/MSVC build.

Design constraints:

1. The distributable mod is a single `.wh.cpp` file compiled by Windhawk's toolchain.
2. Shared ColorFix policy lives in `../core/*.hpp` and must remain compatible with both MSVC and clang.
3. The generated mod must use Windhawk's hook API (`Wh_SetFunctionHook` and batched hook operations), not MinHook directly.
4. Do not hand-maintain duplicated mapper logic in the mod.

The next implementation step is a generator that embeds the shared headers into a Windhawk template and installs Phase 1 hooks for:
`GetSysColor`, `GetSysColorBrush`, `GetStockObject`, `SetTextColor`, `SetBkColor`, and `CreateSolidBrush`.

Native ARM64, native AMD64 and x86/WOW64 must be tested as distinct process architectures. Emulated x64/x86 on Windows ARM64 is a separate validation stage.
