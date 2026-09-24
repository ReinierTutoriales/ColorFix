// Minimal Windhawk API stub for syntax-checking the mod outside Windhawk.
#pragma once
#include <windows.h>

inline void Wh_LogImpl(const wchar_t*, ...) {}
// Mirror Windhawk's literal-concatenating macro shape closely enough that
// non-literal format expressions fail in CI instead of only inside Windhawk.
#define Wh_Log(format, ...) Wh_LogImpl(L"" format __VA_OPT__(,) __VA_ARGS__)

inline BOOL Wh_SetFunctionHook(void*, void*, void**) { return TRUE; }
inline BOOL Wh_ApplyHookOperations() { return TRUE; }
inline PCWSTR Wh_GetStringSetting(PCWSTR, ...) { return L"followsystem"; }
inline void Wh_FreeStringSetting(PCWSTR) {}
