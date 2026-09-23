// Minimal Windhawk API stub for syntax-checking the mod outside Windhawk.
#pragma once
#include <windows.h>
inline void Wh_Log(const wchar_t*, ...) {}
inline BOOL Wh_SetFunctionHook(void*, void*, void**) { return TRUE; }
