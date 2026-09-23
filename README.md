# ColorFix

Experimental dark-mode compatibility layer for legacy Windows applications.

## Scope

- Windows 11 x64: native x64 and x86/WOW64 targets.
- Windows 11 ARM64: native ARM64 first; emulated x64/x86 are a later validation stage.
- Phase 1 focuses on semantic system colors and classic GDI.
- Windhawk is the validation host; a standalone native host comes later.

## Phase 1 hooks

`GetSysColor`, `GetSysColorBrush`, `GetStockObject`, `SetTextColor`, `SetBkColor`, and `CreateSolidBrush`.

The core color policy is header-only so the same implementation can be embedded in a generated Windhawk `.wh.cpp` and used by native builds.

## Build the Win32 test app

Use Visual Studio 2022 with CMake support:

```powershell
cmake --preset x64
cmake --build --preset x64-debug
```

For ARM64, run from a VS developer environment with the ARM64 toolchain installed:

```powershell
cmake --preset arm64
cmake --build --preset arm64-debug
```

The Windhawk mod is intentionally kept separate from the CMake build. See `windhawk/README.md`.

## Status

Initial scaffold. The first milestone is deterministic validation of classic Win32 color paths before adding UxTheme, GDI+, DirectWrite, or Direct2D.
