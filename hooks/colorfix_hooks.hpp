#pragma once

#include <atomic>
#include <cwchar>
#include <windows.h>
#include <uxtheme.h>

#include "colorfix_mapper.hpp"
#include "colorfix_policy.hpp"

namespace colorfix::hooks {

using GetSysColor_t      = DWORD    (WINAPI*)(int);
using GetSysColorBrush_t = HBRUSH   (WINAPI*)(int);
using GetStockObject_t   = HGDIOBJ  (WINAPI*)(int);
using SetTextColor_t     = COLORREF (WINAPI*)(HDC, COLORREF);
using SetBkColor_t       = COLORREF (WINAPI*)(HDC, COLORREF);
using CreateSolidBrush_t = HBRUSH   (WINAPI*)(COLORREF);
using DeleteObject_t     = BOOL     (WINAPI*)(HGDIOBJ);
using DefWindowProc_t    = LRESULT  (WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using FillRect_t         = int      (WINAPI*)(HDC, const RECT*, HBRUSH);
using OpenThemeData_t     = HTHEME   (WINAPI*)(HWND, LPCWSTR);
using OpenThemeDataForDpi_t = HTHEME (WINAPI*)(HWND, LPCWSTR, UINT);
using OpenThemeDataEx_t   = HTHEME   (WINAPI*)(HWND, LPCWSTR, DWORD);
using CloseThemeData_t    = HRESULT  (WINAPI*)(HTHEME);
using DrawThemeText_t     = HRESULT  (WINAPI*)(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, DWORD, const RECT*);
using DrawThemeTextEx_t   = HRESULT  (WINAPI*)(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, RECT*, const DTTOPTS*);

// Probe-only call counters. Compiled out unless COLORFIX_PROBE is defined, so
// the Windhawk mod and future ColorFix.dll carry no instrumentation.
#if defined(COLORFIX_PROBE)
enum class HookId : int {
    GetSysColor, GetSysColorBrush, GetStockObject, SetTextColor,
    SetBkColor, CreateSolidBrush, DeleteObject, DefWindowProcErase, DefWindowProcCtlColor,
    FillRect, Count
};
inline std::atomic<long> g_probeCalls[static_cast<int>(HookId::Count)];
#define COLORFIX_PROBE_HIT(id) \
    ::colorfix::hooks::g_probeCalls[static_cast<int>(::colorfix::hooks::HookId::id)] \
        .fetch_add(1, std::memory_order_relaxed)
#else
#define COLORFIX_PROBE_HIT(id) ((void)0)
#endif

#if defined(COLORFIX_PROBE)
using ThemeOpenTap_t = void (*)(HTHEME, LPCWSTR);
using ThemeCloseTap_t = void (*)(HTHEME, HRESULT);
using ThemeTextTap_t = void (*)(HTHEME, int, bool);
inline std::atomic<ThemeOpenTap_t> g_probeThemeOpenTap{nullptr};
inline std::atomic<ThemeCloseTap_t> g_probeThemeCloseTap{nullptr};
inline std::atomic<ThemeTextTap_t> g_probeThemeTextTap{nullptr};
// Probe-only FillRect telemetry and tap. The tap sees the caller's brush
// before the product decision and returns the brush to continue with; the
// 9a/9b button-face observer uses it instead of a second MinHook detour on
// user32!FillRect, which the product hook already owns.
using FillRectTap_t = HBRUSH (*)(HDC, const RECT*, HBRUSH);
using SetTextColorTap_t = COLORREF (*)(HDC, COLORREF, COLORREF);
inline std::atomic<FillRectTap_t> g_probeFillRectTap{nullptr};
inline std::atomic<SetTextColorTap_t> g_probeSetTextColorTap{nullptr};
inline std::atomic<long> g_fillRectSubstituted{0};   // product substitutions
inline std::atomic<long> g_fillRectPseudo{0};        // COLOR_x + 1 values seen
inline std::atomic<unsigned long> g_fillRectPseudoMask{0};  // bit x = COLOR_x
#endif

inline GetSysColor_t      GetSysColor_Original;
inline GetSysColorBrush_t GetSysColorBrush_Original;
inline GetStockObject_t   GetStockObject_Original;
inline SetTextColor_t     SetTextColor_Original;
inline SetBkColor_t       SetBkColor_Original;
inline CreateSolidBrush_t CreateSolidBrush_Original;
inline DeleteObject_t     DeleteObject_Original;
inline DefWindowProc_t    DefWindowProcW_Original;
inline DefWindowProc_t    DefWindowProcA_Original;
inline FillRect_t         FillRect_Original;
inline OpenThemeData_t     OpenThemeData_Original;
inline OpenThemeDataForDpi_t OpenThemeDataForDpi_Original;
inline OpenThemeDataEx_t   OpenThemeDataEx_Original;
inline CloseThemeData_t    CloseThemeData_Original;
inline DrawThemeText_t     DrawThemeText_Original;
inline DrawThemeTextEx_t   DrawThemeTextEx_Original;

struct ThemeMapEntry {
    std::atomic<HTHEME> theme{nullptr};
    wchar_t klass[48]{};
    std::atomic<long> refs{0};
};
inline constexpr int kThemeMapCapacity = 64;
inline ThemeMapEntry g_themeMap[kThemeMapCapacity];
inline std::atomic_flag g_themeMapLock = ATOMIC_FLAG_INIT;

struct ThemeMapGuard {
    ThemeMapGuard() { while (g_themeMapLock.test_and_set(std::memory_order_acquire)) YieldProcessor(); }
    ~ThemeMapGuard() { g_themeMapLock.clear(std::memory_order_release); }
};

inline const wchar_t* SingleThemeClass(LPCWSTR klass) {
    if (!klass || !*klass || wcschr(klass, L';')) return nullptr;
    const wchar_t* prefix = wcsstr(klass, L"::");
    if (prefix && wcsstr(prefix + 2, L"::")) return nullptr;
    return prefix ? prefix + 2 : klass;
}

inline bool IsButtonThemeClass(LPCWSTR klass) {
    const wchar_t* single = SingleThemeClass(klass);
    return single && _wcsicmp(single, L"Button") == 0;
}

inline bool RememberTheme(HTHEME theme, LPCWSTR klass) {
    if (!theme || !IsButtonThemeClass(klass)) return false;
    ThemeMapGuard guard;
    ThemeMapEntry* empty = nullptr;
    for (auto& entry : g_themeMap) {
        if (entry.theme.load(std::memory_order_relaxed) == theme) {
            entry.refs.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        if (!empty && !entry.theme.load(std::memory_order_relaxed)) empty = &entry;
    }
    if (!empty) return false;
    lstrcpynW(empty->klass, L"Button", static_cast<int>(_countof(empty->klass)));
    empty->refs.store(1, std::memory_order_relaxed);
    empty->theme.store(theme, std::memory_order_release);
    return true;
}

inline bool ReleaseTheme(HTHEME theme) {
    if (!theme) return false;
    ThemeMapGuard guard;
    for (auto& entry : g_themeMap) {
        if (entry.theme.load(std::memory_order_relaxed) != theme) continue;
        const long refs = entry.refs.load(std::memory_order_relaxed);
        if (refs <= 0) {
            entry.theme.store(nullptr, std::memory_order_release);
            entry.klass[0] = L'\0';
            return false;
        }
        if (refs == 1) {
            entry.refs.store(0, std::memory_order_relaxed);
            entry.theme.store(nullptr, std::memory_order_release);
            entry.klass[0] = L'\0';
        } else {
            entry.refs.store(refs - 1, std::memory_order_relaxed);
        }
        return true;
    }
    return false;
}

inline bool KnownButtonTheme(HTHEME theme) {
    if (!theme) return false;
    ThemeMapGuard guard;
    for (auto& entry : g_themeMap)
        if (entry.theme.load(std::memory_order_relaxed) == theme)
            return _wcsicmp(entry.klass, L"Button") == 0;
    return false;
}

struct ThemeTextContext { HTHEME theme = nullptr; int part = 0; };
inline constexpr int kThemeTextDepth = 8;
inline thread_local ThemeTextContext t_themeText[kThemeTextDepth];
inline thread_local int t_themeTextDepth = 0;

inline ThemeTextContext CurrentThemeText() {
    if (t_themeTextDepth <= 0 || t_themeTextDepth > kThemeTextDepth) return {};
    return t_themeText[t_themeTextDepth - 1];
}

struct ThemeTextScope {
    bool stored = false;
    ThemeTextScope(HTHEME theme, int part) {
        if (t_themeTextDepth < kThemeTextDepth) {
            t_themeText[t_themeTextDepth] = {theme, part};
            stored = true;
        }
        ++t_themeTextDepth;
    }
    ~ThemeTextScope() {
        --t_themeTextDepth;
        if (stored) t_themeText[t_themeTextDepth] = {};
    }
};

inline HTHEME WINAPI OpenThemeData_Hook(HWND hwnd, LPCWSTR klass) {
    HTHEME theme = OpenThemeData_Original(hwnd, klass);
    RememberTheme(theme, klass);
#if defined(COLORFIX_PROBE)
    if (const auto tap = g_probeThemeOpenTap.load(std::memory_order_acquire)) tap(theme, klass);
#endif
    return theme;
}
inline HTHEME WINAPI OpenThemeDataForDpi_Hook(HWND hwnd, LPCWSTR klass, UINT dpi) {
    HTHEME theme = OpenThemeDataForDpi_Original(hwnd, klass, dpi);
    RememberTheme(theme, klass);
#if defined(COLORFIX_PROBE)
    if (const auto tap = g_probeThemeOpenTap.load(std::memory_order_acquire)) tap(theme, klass);
#endif
    return theme;
}
inline HTHEME WINAPI OpenThemeDataEx_Hook(HWND hwnd, LPCWSTR klass, DWORD flags) {
    HTHEME theme = OpenThemeDataEx_Original(hwnd, klass, flags);
    RememberTheme(theme, klass);
#if defined(COLORFIX_PROBE)
    if (const auto tap = g_probeThemeOpenTap.load(std::memory_order_acquire)) tap(theme, klass);
#endif
    return theme;
}
inline HRESULT WINAPI CloseThemeData_Hook(HTHEME theme) {
    const HRESULT hr = CloseThemeData_Original(theme);
    if (SUCCEEDED(hr)) ReleaseTheme(theme);
#if defined(COLORFIX_PROBE)
    if (const auto tap = g_probeThemeCloseTap.load(std::memory_order_acquire)) tap(theme, hr);
#endif
    return hr;
}
inline HRESULT WINAPI DrawThemeText_Hook(HTHEME theme, HDC dc, int part, int state,
                                         LPCWSTR text, int count, DWORD flags,
                                         DWORD flags2, const RECT* rc) {
    ThemeTextScope scope(theme, part);
#if defined(COLORFIX_PROBE)
    if (const auto tap = g_probeThemeTextTap.load(std::memory_order_acquire))
        tap(theme, part, KnownButtonTheme(theme));
#endif
    return DrawThemeText_Original(theme, dc, part, state, text, count, flags, flags2, rc);
}
inline HRESULT WINAPI DrawThemeTextEx_Hook(HTHEME theme, HDC dc, int part, int state,
                                           LPCWSTR text, int count, DWORD flags,
                                           RECT* rc, const DTTOPTS* opts) {
    ThemeTextScope scope(theme, part);
#if defined(COLORFIX_PROBE)
    if (const auto tap = g_probeThemeTextTap.load(std::memory_order_acquire))
        tap(theme, part, KnownButtonTheme(theme));
#endif
    return DrawThemeTextEx_Original(theme, dc, part, state, text, count, flags, rc, opts);
}

// One process-lifetime brush per system color index, derived from core roles.
// COLOR_MENUBAR (30) is the highest defined index.
inline constexpr int kSysColorCount = COLOR_MENUBAR + 1;
inline std::atomic<HBRUSH> g_brushes[kSysColorCount];

// Identity cache of the system color brush handles, one per index. Filled
// before any hook exists (RegisterPhase1Hooks), never inside a detour. Whether
// an index is remapped is decided by SemanticBrush at the call, so the cache
// holds identities only and does not depend on the current colors.
inline std::atomic<HBRUSH> g_sysBrushes[kSysColorCount];

// Returns the number of non-null handles cached.
inline int FillSystemBrushCache(GetSysColorBrush_t source) {
    int cached = 0;
    for (int i = 0; i < kSysColorCount; ++i) {
        HBRUSH brush = source ? source(i) : nullptr;
        g_sysBrushes[i].store(brush, std::memory_order_release);
        if (brush) ++cached;
    }
    return cached;
}

// Rebuild from the unhooked GetSysColorBrush. Not called by the runtime:
// system brush handles are expected to be stable (measured by the probe).
inline int RefreshSystemBrushCache() {
    return FillSystemBrushCache(GetSysColorBrush_Original);
}

// Index whose original system brush handle is exactly `brush`, or -1.
inline int SystemBrushIndex(HBRUSH brush) {
    if (!brush) return -1;
    for (int i = 0; i < kSysColorCount; ++i)
        if (g_sysBrushes[i].load(std::memory_order_acquire) == brush) return i;
    return -1;
}

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
    COLORFIX_PROBE_HIT(GetSysColor);
    if (!colorfix::policy::Active()) return GetSysColor_Original(index);
    const COLORREF original = static_cast<COLORREF>(GetSysColor_Original(index));
    return colorfix::MapSystemColor(index, original);
}

inline HBRUSH WINAPI GetSysColorBrush_Hook(int index) {
    COLORFIX_PROBE_HIT(GetSysColorBrush);
    if (!colorfix::policy::Active()) return GetSysColorBrush_Original(index);
    if (HBRUSH brush = SemanticBrush(index)) return brush;
    return GetSysColorBrush_Original(index);
}

inline HGDIOBJ WINAPI GetStockObject_Hook(int object) {
    COLORFIX_PROBE_HIT(GetStockObject);
    if (!colorfix::policy::Active()) return GetStockObject_Original(object);
    // Only WHITE_BRUSH is treated as a window background. BLACK_BRUSH is used
    // for frames, text and masks: leave it untouched in Phase 1.
    if (object == WHITE_BRUSH)
        if (HBRUSH brush = SemanticBrush(COLOR_WINDOW)) return brush;
    return GetStockObject_Original(object);
}

inline COLORREF WINAPI SetTextColor_Hook(HDC dc, COLORREF color) {
    COLORFIX_PROBE_HIT(SetTextColor);
    if (!colorfix::policy::Active()) return SetTextColor_Original(dc, color);
    COLORREF mapped = colorfix::MapLiteralColor(color);
    const ThemeTextContext text = CurrentThemeText();
    if (text.part == 1 && KnownButtonTheme(text.theme)) mapped = color;
#if defined(COLORFIX_PROBE)
    if (const SetTextColorTap_t tap = g_probeSetTextColorTap.load(std::memory_order_acquire))
        mapped = tap(dc, color, mapped);
#endif
    return SetTextColor_Original(dc, mapped);
}

inline COLORREF WINAPI SetBkColor_Hook(HDC dc, COLORREF color) {
    COLORFIX_PROBE_HIT(SetBkColor);
    if (!colorfix::policy::Active()) return SetBkColor_Original(dc, color);
    return SetBkColor_Original(dc, colorfix::MapLiteralColor(color));
}

inline HBRUSH WINAPI CreateSolidBrush_Hook(COLORREF color) {
    COLORFIX_PROBE_HIT(CreateSolidBrush);
    if (!colorfix::policy::Active()) return CreateSolidBrush_Original(color);
    return CreateSolidBrush_Original(colorfix::MapLiteralColor(color));
}

inline BOOL WINAPI DeleteObject_Hook(HGDIOBJ obj) {
    COLORFIX_PROBE_HIT(DeleteObject);
    // Ownership, not policy: ColorFix brushes must survive even while the
    // policy is inactive, or a later reactivation would hand out deleted (or
    // recycled) handles. System/stock brushes behave the same way.
    if (IsColorFixBrush(obj)) return TRUE;
    return DeleteObject_Original(obj);
}

// Phase 1b: class background erase. DefWindowProc erases with the class brush
// through an internal path that never calls the hooked exports (probe
// increment 1), so WM_ERASEBKGND is intercepted at DefWindowProc itself.
// Only system-color class brushes (COLOR_x + 1) are remapped; real brush
// handles and anything else fall through to the original.
inline bool EraseWithSemanticBrush(HWND hwnd, HDC dc) {
    const ULONG_PTR cls = GetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND);
    if (cls == 0 || cls > static_cast<ULONG_PTR>(kSysColorCount)) return false;
    HBRUSH brush = SemanticBrush(static_cast<int>(cls) - 1);
    if (!brush) return false;
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return false;
    return FillRect(dc, &rc, brush) != 0;
}

// Phase 1b: WM_CTLCOLOR* defaults. The original runs first; its answer is
// replaced only when it is exactly DefWindowProc's system default for that
// message (the system brush). Any other answer is returned untouched.
struct CtlColorDefault { int brush; int bk; int text; };

inline bool CtlColorDefaultFor(UINT msg, CtlColorDefault* d) {
    switch (msg) {
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        *d = {COLOR_WINDOW, COLOR_WINDOW, COLOR_WINDOWTEXT};
        return true;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
        *d = {COLOR_3DFACE, COLOR_3DFACE, COLOR_WINDOWTEXT};
        return true;
    case WM_CTLCOLORSCROLLBAR:
        *d = {COLOR_SCROLLBAR, COLOR_3DHILIGHT, COLOR_3DFACE};
        return true;
    }
    return false;
}

inline LRESULT AdjustCtlColor(UINT msg, WPARAM wp, LRESULT result) {
    CtlColorDefault d;
    if (!CtlColorDefaultFor(msg, &d)) return result;
    COLORFIX_PROBE_HIT(DefWindowProcCtlColor);
    if (!colorfix::policy::Active()) return result;
    if (result != reinterpret_cast<LRESULT>(GetSysColorBrush_Original(d.brush))) return result;
    HBRUSH brush = SemanticBrush(d.brush);
    if (!brush) return result;
    HDC dc = reinterpret_cast<HDC>(wp);
    SetTextColor_Original(dc, colorfix::MapSystemColor(
        d.text, static_cast<COLORREF>(GetSysColor_Original(d.text))));
    SetBkColor_Original(dc, colorfix::MapSystemColor(
        d.bk, static_cast<COLORREF>(GetSysColor_Original(d.bk))));
    return reinterpret_cast<LRESULT>(brush);
}

inline LRESULT WINAPI DefWindowProcW_Hook(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Counters record interception before the policy gate, as in every hook.
    if (msg == WM_ERASEBKGND) {
        COLORFIX_PROBE_HIT(DefWindowProcErase);
        if (colorfix::policy::Active() && EraseWithSemanticBrush(hwnd, reinterpret_cast<HDC>(wp)))
            return 1;
    }
    return AdjustCtlColor(msg, wp, DefWindowProcW_Original(hwnd, msg, wp, lp));
}

inline LRESULT WINAPI DefWindowProcA_Hook(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) {
        COLORFIX_PROBE_HIT(DefWindowProcErase);
        if (colorfix::policy::Active() && EraseWithSemanticBrush(hwnd, reinterpret_cast<HDC>(wp)))
            return 1;
    }
    return AdjustCtlColor(msg, wp, DefWindowProcA_Original(hwnd, msg, wp, lp));
}

// Phase 1c: classic fills with an original system brush handle. USER32 paints
// the classic button face with FillRect(GetSysColorBrush(COLOR_BTNFACE)) from
// an internal path (probe 9a/9b), so the hooked GetSysColorBrush never runs.
// Only handles in the identity cache whose role is remapped are replaced.
// COLOR_x + 1 values and any other brush pass through unchanged.
inline int WINAPI FillRect_Hook(HDC dc, const RECT* rc, HBRUSH brush) {
    COLORFIX_PROBE_HIT(FillRect);
    const ULONG_PTR value = reinterpret_cast<ULONG_PTR>(brush);
    const bool pseudo = value > 0 && value <= static_cast<ULONG_PTR>(kSysColorCount);
#if defined(COLORFIX_PROBE)
    if (pseudo) {
        g_fillRectPseudo.fetch_add(1, std::memory_order_relaxed);
        g_fillRectPseudoMask.fetch_or(1ul << (value - 1), std::memory_order_relaxed);
    }
    if (const FillRectTap_t tap = g_probeFillRectTap.load(std::memory_order_acquire))
        brush = tap(dc, rc, brush);
#endif
    if (!brush || pseudo || !colorfix::policy::Active()) return FillRect_Original(dc, rc, brush);
    const int index = SystemBrushIndex(brush);
    if (index >= 0) {
        if (HBRUSH semantic = SemanticBrush(index)) {
#if defined(COLORFIX_PROBE)
            g_fillRectSubstituted.fetch_add(1, std::memory_order_relaxed);
#endif
            return FillRect_Original(dc, rc, semantic);
        }
    }
    return FillRect_Original(dc, rc, brush);
}

template <typename RegisterHook>
inline bool RegisterPhase1Hooks(RegisterHook&& registerHook) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    HMODULE gdi32  = GetModuleHandleW(L"gdi32.dll");
    HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (!user32 || !gdi32) return false;

    // Identity cache from the exports before any hook is queued or applied.
    if (FillSystemBrushCache(&::GetSysColorBrush) == 0) return false;

    bool ok = true;
    ok &= registerHook(user32, "GetSysColor",      reinterpret_cast<void*>(GetSysColor_Hook),      reinterpret_cast<void**>(&GetSysColor_Original));
    ok &= registerHook(user32, "GetSysColorBrush", reinterpret_cast<void*>(GetSysColorBrush_Hook), reinterpret_cast<void**>(&GetSysColorBrush_Original));
    ok &= registerHook(gdi32,  "GetStockObject",   reinterpret_cast<void*>(GetStockObject_Hook),   reinterpret_cast<void**>(&GetStockObject_Original));
    ok &= registerHook(gdi32,  "SetTextColor",     reinterpret_cast<void*>(SetTextColor_Hook),     reinterpret_cast<void**>(&SetTextColor_Original));
    ok &= registerHook(gdi32,  "SetBkColor",       reinterpret_cast<void*>(SetBkColor_Hook),       reinterpret_cast<void**>(&SetBkColor_Original));
    ok &= registerHook(gdi32,  "CreateSolidBrush", reinterpret_cast<void*>(CreateSolidBrush_Hook), reinterpret_cast<void**>(&CreateSolidBrush_Original));
    ok &= registerHook(gdi32,  "DeleteObject",     reinterpret_cast<void*>(DeleteObject_Hook),     reinterpret_cast<void**>(&DeleteObject_Original));
    ok &= registerHook(user32, "DefWindowProcW",   reinterpret_cast<void*>(DefWindowProcW_Hook),   reinterpret_cast<void**>(&DefWindowProcW_Original));
    ok &= registerHook(user32, "DefWindowProcA",   reinterpret_cast<void*>(DefWindowProcA_Hook),   reinterpret_cast<void**>(&DefWindowProcA_Original));
    ok &= registerHook(user32, "FillRect",         reinterpret_cast<void*>(FillRect_Hook),         reinterpret_cast<void**>(&FillRect_Original));
    if (uxtheme) {
        bool uxOk = true;
        uxOk &= registerHook(uxtheme, "OpenThemeData", reinterpret_cast<void*>(OpenThemeData_Hook), reinterpret_cast<void**>(&OpenThemeData_Original));
        uxOk &= registerHook(uxtheme, "OpenThemeDataForDpi", reinterpret_cast<void*>(OpenThemeDataForDpi_Hook), reinterpret_cast<void**>(&OpenThemeDataForDpi_Original));
        uxOk &= registerHook(uxtheme, "OpenThemeDataEx", reinterpret_cast<void*>(OpenThemeDataEx_Hook), reinterpret_cast<void**>(&OpenThemeDataEx_Original));
        uxOk &= registerHook(uxtheme, "CloseThemeData", reinterpret_cast<void*>(CloseThemeData_Hook), reinterpret_cast<void**>(&CloseThemeData_Original));
        uxOk &= registerHook(uxtheme, "DrawThemeText", reinterpret_cast<void*>(DrawThemeText_Hook), reinterpret_cast<void**>(&DrawThemeText_Original));
        uxOk &= registerHook(uxtheme, "DrawThemeTextEx", reinterpret_cast<void*>(DrawThemeTextEx_Hook), reinterpret_cast<void**>(&DrawThemeTextEx_Original));
        (void)uxOk;  // UxTheme enhancement is fail-closed and independent of Phase 1.
    }
    return ok;
}

// Brushes are deliberately leaked at shutdown: callers may still hold handles.
inline void ShutdownPhase1Hooks() {}

} // namespace colorfix::hooks
