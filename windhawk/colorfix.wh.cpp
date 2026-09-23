// ==WindhawkMod==
// @id              colorfix
// @name            ColorFix
// @description     Phase 1 dark color compatibility for classic Win32 applications
// @version         0.1.0
// @author          ReinierTutoriales
// @include         *
// @architecture    x86-64
// @architecture    x86
// @architecture    arm64
// ==/WindhawkMod==
//
// Generated prototype. Shared policy must stay in sync with core/ until the
// generator lands. Do not add UxTheme/Direct2D hooks to Phase 1.

#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace colorfix {

constexpr bool IsPaletteEncoded(COLORREF c) noexcept {
    const auto high = static_cast<std::uint32_t>(c) & 0xFF000000u;
    return high == 0x01000000u || high == 0x02000000u;
}

constexpr bool IsSpecialColor(COLORREF c) noexcept {
    return c == CLR_INVALID || c == CLR_NONE || c == CLR_DEFAULT ||
           IsPaletteEncoded(c);
}

constexpr COLORREF Rgb(BYTE r, BYTE g, BYTE b) noexcept {
    return static_cast<COLORREF>(r | (static_cast<DWORD>(g) << 8) |
                                 (static_cast<DWORD>(b) << 16));
}

constexpr COLORREF MapLiteralColor(COLORREF c) noexcept {
    if (IsSpecialColor(c)) return c;
    const auto r = GetRValue(c);
    const auto g = GetGValue(c);
    const auto b = GetBValue(c);
    const unsigned luma = (54u * r + 183u * g + 19u * b) >> 8;
    if (luma >= 235u) return Rgb(32, 32, 32);
    if (luma <= 20u) return Rgb(220, 220, 220);
    return c;
}

constexpr COLORREF MapSystemColor(int index, COLORREF original) noexcept {
    switch (index) {
    case COLOR_WINDOW:     return Rgb(32, 32, 32);
    case COLOR_WINDOWTEXT: return Rgb(220, 220, 220);
    case COLOR_BTNFACE:    return Rgb(45, 45, 45);
    case COLOR_BTNTEXT:    return Rgb(220, 220, 220);
    case COLOR_GRAYTEXT:   return Rgb(145, 145, 145);
    case COLOR_3DSHADOW:   return Rgb(65, 65, 65);
    case COLOR_3DHILIGHT:  return Rgb(85, 85, 85);
    case COLOR_MENU:       return Rgb(37, 37, 37);
    case COLOR_MENUTEXT:   return Rgb(220, 220, 220);
    case COLOR_INFOBK:     return Rgb(48, 48, 48);
    case COLOR_INFOTEXT:   return Rgb(220, 220, 220);
    default:               return original;
    }
}

} // namespace colorfix

using GetSysColor_t = DWORD (WINAPI*)(int);
using GetSysColorBrush_t = HBRUSH (WINAPI*)(int);
using GetStockObject_t = HGDIOBJ (WINAPI*)(int);
using SetTextColor_t = COLORREF (WINAPI*)(HDC, COLORREF);
using SetBkColor_t = COLORREF (WINAPI*)(HDC, COLORREF);
using CreateSolidBrush_t = HBRUSH (WINAPI*)(COLORREF);

static GetSysColor_t GetSysColor_Original;
static GetSysColorBrush_t GetSysColorBrush_Original;
static GetStockObject_t GetStockObject_Original;
static SetTextColor_t SetTextColor_Original;
static SetBkColor_t SetBkColor_Original;
static CreateSolidBrush_t CreateSolidBrush_Original;

// Brushes returned from GetSysColorBrush must remain valid and must not be
// deleted by callers. Keep process-lifetime brushes for the semantic roles.
struct BrushSlot {
    int index;
    std::atomic<HBRUSH> brush;
};

static BrushSlot g_brushes[] = {
    {COLOR_WINDOW, nullptr}, {COLOR_BTNFACE, nullptr}, {COLOR_MENU, nullptr},
    {COLOR_INFOBK, nullptr}, {COLOR_3DSHADOW, nullptr}, {COLOR_3DHILIGHT, nullptr},
};

static HBRUSH SemanticBrush(int index, COLORREF mapped) {
    for (auto& slot : g_brushes) {
        if (slot.index != index) continue;
        HBRUSH brush = slot.brush.load(std::memory_order_acquire);
        if (brush) return brush;

        // Call the original function to avoid re-entering our hook.
        HBRUSH created = CreateSolidBrush_Original(mapped);
        HBRUSH expected = nullptr;
        if (slot.brush.compare_exchange_strong(expected, created,
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
            return created;
        }
        DeleteObject(created);
        return expected;
    }
    return nullptr;
}

static DWORD WINAPI GetSysColor_Hook(int index) {
    const COLORREF original = static_cast<COLORREF>(GetSysColor_Original(index));
    return colorfix::MapSystemColor(index, original);
}

static HBRUSH WINAPI GetSysColorBrush_Hook(int index) {
    const COLORREF original = static_cast<COLORREF>(GetSysColor_Original(index));
    const COLORREF mapped = colorfix::MapSystemColor(index, original);
    if (mapped == original) return GetSysColorBrush_Original(index);
    if (HBRUSH brush = SemanticBrush(index, mapped)) return brush;
    return GetSysColorBrush_Original(index);
}

static HGDIOBJ WINAPI GetStockObject_Hook(int object) {
    // Only remap stock color objects that defeat CreateSolidBrush hooks.
    switch (object) {
    case WHITE_BRUSH:
        return SemanticBrush(COLOR_WINDOW, colorfix::Rgb(32, 32, 32));
    case BLACK_BRUSH:
        return SemanticBrush(COLOR_WINDOW, colorfix::Rgb(32, 32, 32));
    default:
        return GetStockObject_Original(object);
    }
}

static COLORREF WINAPI SetTextColor_Hook(HDC dc, COLORREF color) {
    return SetTextColor_Original(dc, colorfix::MapLiteralColor(color));
}

static COLORREF WINAPI SetBkColor_Hook(HDC dc, COLORREF color) {
    return SetBkColor_Original(dc, colorfix::MapLiteralColor(color));
}

static HBRUSH WINAPI CreateSolidBrush_Hook(COLORREF color) {
    return CreateSolidBrush_Original(colorfix::MapLiteralColor(color));
}

template <typename T>
static bool HookExport(HMODULE module, const char* name, void* hook, T* original) {
    auto target = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!target) {
        Wh_Log(L"ColorFix: export not found: %S", name);
        return false;
    }
    if (!Wh_SetFunctionHook(target, hook, reinterpret_cast<void**>(original))) {
        Wh_Log(L"ColorFix: failed to queue hook: %S", name);
        return false;
    }
    return true;
}

BOOL Wh_ModInit() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
    if (!user32 || !gdi32) return FALSE;

    bool ok = true;
    ok &= HookExport(user32, "GetSysColor", reinterpret_cast<void*>(GetSysColor_Hook), &GetSysColor_Original);
    ok &= HookExport(user32, "GetSysColorBrush", reinterpret_cast<void*>(GetSysColorBrush_Hook), &GetSysColorBrush_Original);
    ok &= HookExport(gdi32, "GetStockObject", reinterpret_cast<void*>(GetStockObject_Hook), &GetStockObject_Original);
    ok &= HookExport(gdi32, "SetTextColor", reinterpret_cast<void*>(SetTextColor_Hook), &SetTextColor_Original);
    ok &= HookExport(gdi32, "SetBkColor", reinterpret_cast<void*>(SetBkColor_Hook), &SetBkColor_Original);
    ok &= HookExport(gdi32, "CreateSolidBrush", reinterpret_cast<void*>(CreateSolidBrush_Hook), &CreateSolidBrush_Original);

    if (!ok) return FALSE;

    // Commit all queued operations together.
    if (!Wh_ApplyHookOperations()) {
        Wh_Log(L"ColorFix: failed to apply hook batch");
        return FALSE;
    }

    Wh_Log(L"ColorFix Phase 1 initialized");
    return TRUE;
}

void Wh_ModUninit() {
    // Windhawk removes function hooks. Process-lifetime semantic brushes are
    // deliberately retained: callers may still hold handles during teardown.
    Wh_Log(L"ColorFix Phase 1 uninitialized");
}
