#pragma once

// Probe increment 9a: causal characterization of the push-button face.
// Probe-only; nothing here is product code. Temporary MinHook detours on four
// classic primitives, a FillRect tap that the product FillRect hook calls
// (increment 9b: the product owns user32!FillRect), plus an intercept hook
// that the UxTheme observer calls from its existing DrawThemeBackground detour.
// All interventions apply only while a candidate is active AND the call
// belongs to the target button's paint (thread-local context set by a
// SetWindowLongPtrW subclass).
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "MinHook.h"

namespace colorfix::probe::button_face {

enum Candidate : int {
    kFillRect,
    kPatBlt,
    kExtTextOut,
    kDrawFrameControl,
    kDrawEdge,
    kDrawThemeBackground,
    kCandidates
};
inline constexpr const char* kShort[kCandidates] = {"FR", "PB", "ET", "DFC", "DE", "DTB"};
inline constexpr const char* kName[kCandidates] = {"FillRect",         "PatBlt",
                                                   "ExtTextOutW",      "DrawFrameControl",
                                                   "DrawEdge",         "DrawThemeBackground"};
// Substitution replaces the fill color with the marker. Suppression skips the
// call; its causality is judged by an exact pixel difference instead, because
// painting a marker after DrawFrameControl/DrawEdge would only prove that the
// detour ran.
inline constexpr bool kSubstitutes[kCandidates] = {true, true, true, false, false, true};
inline constexpr COLORREF kMarker = RGB(0, 255, 255);

// Paint context: which subclassed button is painting on this thread.
inline thread_local HWND t_painting = nullptr;
inline std::atomic<HWND> g_target{nullptr};
inline std::atomic<int> g_active{-1};
inline std::atomic<long> g_any[kCandidates];         // all interceptions
inline std::atomic<long> g_attributed[kCandidates];  // interceptions in the target's paint
inline std::atomic<long> g_dcMatch[kCandidates];     // ...where WindowFromDC also says target

using SetBkColor_t = COLORREF (WINAPI*)(HDC, COLORREF);
using CreateSolidBrush_t = HBRUSH (WINAPI*)(COLORREF);
// Raw GDI entry points supplied by the probe (ColorFix originals) so the
// experiment never goes through the product mapping when it changes DC state.
inline SetBkColor_t g_setBkColorRaw = &::SetBkColor;
inline CreateSolidBrush_t g_createBrushRaw = &::CreateSolidBrush;
using FillRect_t = int (WINAPI*)(HDC, const RECT*, HBRUSH);
// Unhooked FillRect for the marker paint (the probe supplies the ColorFix
// original), so the marker never re-enters the product hook or the tap.
inline FillRect_t g_fillRectRaw = &::FillRect;
inline HBRUSH g_markerBrush = nullptr;

// Returns true when the call must be intervened. Counts every interception.
inline bool Attribute(int c, HDC dc) noexcept {
    g_any[c].fetch_add(1, std::memory_order_relaxed);
    const HWND target = g_target.load(std::memory_order_relaxed);
    if (!target || t_painting != target) return false;
    g_attributed[c].fetch_add(1, std::memory_order_relaxed);
    if (dc && WindowFromDC(dc) == target) g_dcMatch[c].fetch_add(1, std::memory_order_relaxed);
    return g_active.load(std::memory_order_relaxed) == c;
}

using PatBlt_t = BOOL (WINAPI*)(HDC, int, int, int, int, DWORD);
using ExtTextOutW_t = BOOL (WINAPI*)(HDC, int, int, UINT, const RECT*, LPCWSTR, UINT,
                                     const INT*);
using DrawFrameControl_t = BOOL (WINAPI*)(HDC, LPRECT, UINT, UINT);
using DrawEdge_t = BOOL (WINAPI*)(HDC, LPRECT, UINT, UINT);

inline PatBlt_t g_patBltOrig = nullptr;
inline ExtTextOutW_t g_extTextOutOrig = nullptr;
inline DrawFrameControl_t g_drawFrameControlOrig = nullptr;
inline DrawEdge_t g_drawEdgeOrig = nullptr;
inline void* g_targets[5]{};  // [0] unused: FillRect goes through the tap

// Increment 9b measurement: the brush argument of every FillRect call that
// belongs to a target button's paint, recorded before any substitution.
struct BrushSample {
    HWND target;
    HBRUSH brush;
};
inline constexpr long kMaxBrushSamples = 1024;
inline BrushSample g_brushSamples[kMaxBrushSamples]{};
inline std::atomic<long> g_brushSampleCount{0};
inline std::atomic<long> g_brushSamplesDropped{0};

inline void RecordBrush(HWND target, HBRUSH brush) noexcept {
    const long slot = g_brushSampleCount.fetch_add(1, std::memory_order_relaxed);
    if (slot >= kMaxBrushSamples) {
        g_brushSamplesDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_brushSamples[slot] = {target, brush};
}

// Called by the product FillRect hook with the caller's brush, before the
// product decision. Returns the brush the product hook continues with.
inline HBRUSH FillRectTap(HDC dc, const RECT* /*r*/, HBRUSH brush) {
    const bool intervene = Attribute(kFillRect, dc);
    const HWND target = g_target.load(std::memory_order_relaxed);
    if (target && t_painting == target) RecordBrush(target, brush);
    return intervene ? g_markerBrush : brush;
}

inline BOOL WINAPI PatBlt_Hook(HDC dc, int x, int y, int w, int h, DWORD rop) {
    if (Attribute(kPatBlt, dc) && rop == PATCOPY) {
        const HGDIOBJ old = SelectObject(dc, g_markerBrush);
        const BOOL ok = g_patBltOrig(dc, x, y, w, h, rop);
        SelectObject(dc, old);
        return ok;
    }
    return g_patBltOrig(dc, x, y, w, h, rop);
}

inline BOOL WINAPI ExtTextOutW_Hook(HDC dc, int x, int y, UINT options, const RECT* r,
                                    LPCWSTR text, UINT count, const INT* dx) {
    if (Attribute(kExtTextOut, dc) && (options & ETO_OPAQUE)) {
        // Restore the DC background afterwards so the marker cannot leak.
        const COLORREF old = g_setBkColorRaw(dc, kMarker);
        const BOOL ok = g_extTextOutOrig(dc, x, y, options, r, text, count, dx);
        g_setBkColorRaw(dc, old);
        return ok;
    }
    return g_extTextOutOrig(dc, x, y, options, r, text, count, dx);
}

inline BOOL WINAPI DrawFrameControl_Hook(HDC dc, LPRECT r, UINT type, UINT state) {
    if (Attribute(kDrawFrameControl, dc)) return TRUE;  // suppression
    return g_drawFrameControlOrig(dc, r, type, state);
}

inline BOOL WINAPI DrawEdge_Hook(HDC dc, LPRECT r, UINT edge, UINT flags) {
    if (Attribute(kDrawEdge, dc)) return TRUE;  // suppression
    return g_drawEdgeOrig(dc, r, edge, flags);
}

// Called by the UxTheme observer's DrawThemeBackground detour. Only class
// Button, part 1 (BP_PUSHBUTTON), in the target button's paint. Returns true
// when it painted the marker instead of the original draw.
inline bool DrawThemeIntercept(const wchar_t* klass, HDC dc, int part, int /*state*/,
                               const RECT* r) {
    if (part != 1 || !r || !klass || std::wcscmp(klass, L"Button") != 0) return false;
    if (!Attribute(kDrawThemeBackground, dc)) return false;
    g_fillRectRaw(dc, r, g_markerBrush);
    return true;
}

inline bool CreateAndEnable(const wchar_t* dll, const char* name, void* hook, void** original,
                            void** targetOut) {
    void* target = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(dll), name));
    if (!target) {
        std::printf("buttonface: install %s export-not-found INFRASTRUCTURE_FAILURE\n", name);
        return false;
    }
    MH_STATUS s = MH_CreateHook(target, hook, original);
    if (s == MH_OK) {
        s = MH_EnableHook(target);
        if (s != MH_OK) MH_RemoveHook(target);
    }
    if (s != MH_OK) {
        std::printf("buttonface: install %s %s INFRASTRUCTURE_FAILURE\n", name,
                    MH_StatusToString(s));
        return false;
    }
    *targetOut = target;
    return true;
}

inline bool Install() {
    g_markerBrush = g_createBrushRaw(kMarker);
    if (!g_markerBrush) return false;
    // FillRect: no detour here; the probe wires FillRectTap into the product hook.
    return CreateAndEnable(L"gdi32.dll", "PatBlt", reinterpret_cast<void*>(&PatBlt_Hook),
                           reinterpret_cast<void**>(&g_patBltOrig), &g_targets[1]) &&
           CreateAndEnable(L"gdi32.dll", "ExtTextOutW", reinterpret_cast<void*>(&ExtTextOutW_Hook),
                           reinterpret_cast<void**>(&g_extTextOutOrig), &g_targets[2]) &&
           CreateAndEnable(L"user32.dll", "DrawFrameControl",
                           reinterpret_cast<void*>(&DrawFrameControl_Hook),
                           reinterpret_cast<void**>(&g_drawFrameControlOrig), &g_targets[3]) &&
           CreateAndEnable(L"user32.dll", "DrawEdge", reinterpret_cast<void*>(&DrawEdge_Hook),
                           reinterpret_cast<void**>(&g_drawEdgeOrig), &g_targets[4]);
}

// By target, never MH_ALL_HOOKS: product and UxTheme observer hooks stay live.
inline void Uninstall() {
    for (void*& target : g_targets) {
        if (!target) continue;
        MH_DisableHook(target);
        MH_RemoveHook(target);
        target = nullptr;
    }
    if (g_markerBrush) {
        DeleteObject(g_markerBrush);
        g_markerBrush = nullptr;
    }
}

// ------------------------------------------------------------- subclass
inline constexpr wchar_t kProp[] = L"ColorFixButtonFaceProc";

// Plain SetWindowLongPtrW subclass: SetWindowSubclass is a comctl32 export and
// would break the "comctl32 absent at start" invariant of increment 5.
inline LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const auto orig = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kProp));
    if (!orig) return DefWindowProcW(hwnd, msg, wp, lp);
    const bool paint =
        msg == WM_PAINT || msg == WM_PRINTCLIENT || msg == WM_PRINT || msg == WM_NCPAINT;
    const HWND previous = t_painting;  // nested paints restore the outer context
    if (paint) t_painting = hwnd;
    const LRESULT r = CallWindowProcW(orig, hwnd, msg, wp, lp);
    if (paint) t_painting = previous;
    return r;
}

inline bool Subclass(HWND hwnd) {
    const LONG_PTR orig = GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    if (!hwnd || !orig || !SetPropW(hwnd, kProp, reinterpret_cast<HANDLE>(orig))) return false;
    return SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SubclassProc)) != 0;
}

inline void Unsubclass(HWND hwnd) {
    if (HANDLE orig = GetPropW(hwnd, kProp)) {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
        RemovePropW(hwnd, kProp);
    }
}

// ------------------------------------------------------------- autotest
// Direct call per candidate on a 32x32 memory DIB with a synthetic target:
// interception must be counted, the control call must pass through, and the
// experiment must substitute (marker) or suppress (sentinel intact).
struct AutotestResult {
    bool ok = false;
    long calls = 0;
    COLORREF control = CLR_INVALID;
    COLORREF experiment = CLR_INVALID;
};

inline COLORREF CenterOf(const std::uint32_t* px) {
    const std::uint32_t v = px[16 * 32 + 16];
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

inline void RunPrimitive(int c, HDC dc, RECT* r) {
    HBRUSH white = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    switch (c) {
    case kFillRect:
        FillRect(dc, r, white);
        break;
    case kPatBlt: {
        const HGDIOBJ old = SelectObject(dc, white);
        PatBlt(dc, 0, 0, 32, 32, PATCOPY);
        SelectObject(dc, old);
        break;
    }
    case kExtTextOut:
        g_setBkColorRaw(dc, RGB(255, 255, 255));
        ExtTextOutW(dc, 0, 0, ETO_OPAQUE, r, L"", 0, nullptr);
        break;
    case kDrawFrameControl:
        DrawFrameControl(dc, r, DFC_BUTTON, DFCS_BUTTONPUSH);
        break;
    case kDrawEdge:
        DrawEdge(dc, r, EDGE_RAISED, BF_RECT | BF_MIDDLE);
        break;
    case kDrawThemeBackground: {
        HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
        using Open_t = HANDLE (WINAPI*)(HWND, LPCWSTR);
        using Draw_t = HRESULT (WINAPI*)(HANDLE, HDC, int, int, const RECT*, const RECT*);
        using Close_t = HRESULT (WINAPI*)(HANDLE);
        const auto open = reinterpret_cast<Open_t>(
            reinterpret_cast<void*>(GetProcAddress(ux, "OpenThemeData")));
        const auto draw = reinterpret_cast<Draw_t>(
            reinterpret_cast<void*>(GetProcAddress(ux, "DrawThemeBackground")));
        const auto close = reinterpret_cast<Close_t>(
            reinterpret_cast<void*>(GetProcAddress(ux, "CloseThemeData")));
        if (!open || !draw || !close) break;
        if (HANDLE theme = open(nullptr, L"Button")) {
            draw(theme, dc, 1, 1, r, nullptr);  // BP_PUSHBUTTON, PBS_NORMAL
            close(theme);
        }
        break;
    }
    default:
        break;
    }
}

inline AutotestResult Autotest(int c) {
    AutotestResult res;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 32;
    bi.bmiHeader.biHeight = -32;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dib || !dc || !bits) {
        if (dc) DeleteDC(dc);
        if (dib) DeleteObject(dib);
        return res;
    }
    const HGDIOBJ oldBmp = SelectObject(dc, dib);
    auto* px = static_cast<std::uint32_t*>(bits);
    constexpr std::uint32_t kSentinel = 0x00123456;  // RGB(12, 34, 56) in BGRA
    const COLORREF sentinel = RGB(0x12, 0x34, 0x56);
    RECT r{0, 0, 32, 32};

    const HWND fake = reinterpret_cast<HWND>(static_cast<INT_PTR>(0x5A5A0));
    const HWND savedPainting = t_painting;
    g_target.store(fake);
    t_painting = fake;
    const long calls0 = g_any[c].load();

    g_active.store(-1);
    for (int i = 0; i < 32 * 32; ++i) px[i] = kSentinel;
    RunPrimitive(c, dc, &r);
    GdiFlush();
    res.control = CenterOf(px);

    g_active.store(c);
    for (int i = 0; i < 32 * 32; ++i) px[i] = kSentinel;
    RunPrimitive(c, dc, &r);
    GdiFlush();
    res.experiment = CenterOf(px);
    // The ExtTextOutW marker must not leak into the DC's background color.
    const bool bkRestored = c != kExtTextOut || GetBkColor(dc) == RGB(255, 255, 255);

    g_active.store(-1);
    g_target.store(nullptr);
    t_painting = savedPainting;
    res.calls = g_any[c].load() - calls0;

    const bool effect = kSubstitutes[c]
                            ? (res.experiment == kMarker && res.control != kMarker)
                            : (res.control != sentinel && res.experiment == sentinel);
    res.ok = res.calls >= 2 && effect && bkRestored;

    SelectObject(dc, oldBmp);
    DeleteDC(dc);
    DeleteObject(dib);
    return res;
}

}  // namespace colorfix::probe::button_face
