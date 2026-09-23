#pragma once

#include <atomic>
#include <windows.h>

#include "colorfix_mapper.hpp"

namespace colorfix::hooks {

using GetSysColor_t      = DWORD    (WINAPI*)(int);
using GetSysColorBrush_t = HBRUSH   (WINAPI*)(int);
using GetStockObject_t   = HGDIOBJ  (WINAPI*)(int);
using SetTextColor_t     = COLORREF (WINAPI*)(HDC, COLORREF);
using SetBkColor_t       = COLORREF (WINAPI*)(HDC, COLORREF);
using CreateSolidBrush_t = HBRUSH   (WINAPI*)(COLORREF);
using DeleteObject_t     = BOOL     (WINAPI*)(HGDIOBJ);

inline GetSysColor_t      GetSysColor_Original;
inline GetSysColorBrush_t GetSysColorBrush_Original;
inline GetStockObject_t   GetStockObject_Original;
inline SetTextColor_t     SetTextColor_Original;
inline SetBkColor_t       SetBkColor_Original;
inline CreateSolidBrush_t CreateSolidBrush_Original;
inline DeleteObject_t     DeleteObject_Original;

// One process-lifetime brush per system color index, derived from core roles.
// COLOR_MENUBAR (30) is the highest defined index.
inline constexpr int kSysColorCount = COLOR_MENUBAR + 1;
inline std::atomic<HBRUSH> g_brushes[kSysColorCount];

inline bool IsColorFixBrush(HGDIOBJ obj) {
    if (!obj) return false;
    for (const auto& slot : g_brushes)
        if (slot.load(std::memory_order_relaxed) == obj) return true;
    return false;
}

// Returns the ColorFix brush for a system color index, or nullptr when the
// role is not remapped (caller falls back to the original API).
inline HBRUSH SemanticBrush(int index) {
    if (index < 0 || index >= kSysColorCount) return nullptr;
    const COLORREF original = static_cast<COLORREF>(GetSysColor_Original(index));
    const COLORREF mapped = colorfix::MapSystemColor(index, original);
    if (mapped == original) return nullptr;

    auto& slot = g_brushes[index];
    if (HBRUSH brush = slot.load(std::memory_order_acquire)) return brush;

    HBRUSH created = CreateSolidBrush_Original(mapped);  // bypass literal mapping
    if (!created) return nullptr;
    HBRUSH expected = nullptr;
    if (slot.compare_exchange_strong(expected, created,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return created;
    DeleteObject_Original(created);  // lost the race; never published
    return expected;
}

inline DWORD WINAPI GetSysColor_Hook(int index) {
    const COLORREF original = static_cast<COLORREF>(GetSysColor_Original(index));
    return colorfix::MapSystemColor(index, original);
}

inline HBRUSH WINAPI GetSysColorBrush_Hook(int index) {
    if (HBRUSH brush = SemanticBrush(index)) return brush;
    return GetSysColorBrush_Original(index);
}

inline HGDIOBJ WINAPI GetStockObject_Hook(int object) {
    // Only WHITE_BRUSH is treated as a window background. BLACK_BRUSH is used
    // for frames, text and masks: leave it untouched in Phase 1.
    if (object == WHITE_BRUSH)
        if (HBRUSH brush = SemanticBrush(COLOR_WINDOW)) return brush;
    return GetStockObject_Original(object);
}

inline COLORREF WINAPI SetTextColor_Hook(HDC dc, COLORREF color) {
    return SetTextColor_Original(dc, colorfix::MapLiteralColor(color));
}

inline COLORREF WINAPI SetBkColor_Hook(HDC dc, COLORREF color) {
    return SetBkColor_Original(dc, colorfix::MapLiteralColor(color));
}

inline HBRUSH WINAPI CreateSolidBrush_Hook(COLORREF color) {
    return CreateSolidBrush_Original(colorfix::MapLiteralColor(color));
}

inline BOOL WINAPI DeleteObject_Hook(HGDIOBJ obj) {
    // System/stock brushes must survive DeleteObject; so must ours.
    if (IsColorFixBrush(obj)) return TRUE;
    return DeleteObject_Original(obj);
}

template <typename RegisterHook>
inline bool RegisterPhase1Hooks(RegisterHook&& registerHook) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    HMODULE gdi32  = GetModuleHandleW(L"gdi32.dll");
    if (!user32 || !gdi32) return false;

    bool ok = true;
    ok &= registerHook(user32, "GetSysColor",      reinterpret_cast<void*>(GetSysColor_Hook),      reinterpret_cast<void**>(&GetSysColor_Original));
    ok &= registerHook(user32, "GetSysColorBrush", reinterpret_cast<void*>(GetSysColorBrush_Hook), reinterpret_cast<void**>(&GetSysColorBrush_Original));
    ok &= registerHook(gdi32,  "GetStockObject",   reinterpret_cast<void*>(GetStockObject_Hook),   reinterpret_cast<void**>(&GetStockObject_Original));
    ok &= registerHook(gdi32,  "SetTextColor",     reinterpret_cast<void*>(SetTextColor_Hook),     reinterpret_cast<void**>(&SetTextColor_Original));
    ok &= registerHook(gdi32,  "SetBkColor",       reinterpret_cast<void*>(SetBkColor_Hook),       reinterpret_cast<void**>(&SetBkColor_Original));
    ok &= registerHook(gdi32,  "CreateSolidBrush", reinterpret_cast<void*>(CreateSolidBrush_Hook), reinterpret_cast<void**>(&CreateSolidBrush_Original));
    ok &= registerHook(gdi32,  "DeleteObject",     reinterpret_cast<void*>(DeleteObject_Hook),     reinterpret_cast<void**>(&DeleteObject_Original));
    return ok;
}

// Brushes are deliberately leaked at shutdown: callers may still hold handles.
inline void ShutdownPhase1Hooks() {}

} // namespace colorfix::hooks
