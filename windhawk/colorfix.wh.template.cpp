// ==WindhawkMod==
// @id              colorfix
// @name            ColorFix
// @description     Phase 1 dark color compatibility for classic Win32 applications
// @version         0.1.0
// @author          ReinierTutoriales
// @include         ColorFixTest.exe
// @architecture    x86-64
// @architecture    x86
// @architecture    arm64
// ==/WindhawkMod==

// @@COLORFIX_CORE@@

#include <atomic>

using GetSysColor_t      = DWORD    (WINAPI*)(int);
using GetSysColorBrush_t = HBRUSH   (WINAPI*)(int);
using GetStockObject_t   = HGDIOBJ  (WINAPI*)(int);
using SetTextColor_t     = COLORREF (WINAPI*)(HDC, COLORREF);
using SetBkColor_t       = COLORREF (WINAPI*)(HDC, COLORREF);
using CreateSolidBrush_t = HBRUSH   (WINAPI*)(COLORREF);
using DeleteObject_t     = BOOL     (WINAPI*)(HGDIOBJ);

static GetSysColor_t      GetSysColor_Original;
static GetSysColorBrush_t GetSysColorBrush_Original;
static GetStockObject_t   GetStockObject_Original;
static SetTextColor_t     SetTextColor_Original;
static SetBkColor_t       SetBkColor_Original;
static CreateSolidBrush_t CreateSolidBrush_Original;
static DeleteObject_t     DeleteObject_Original;

constexpr int kSysColorCount = COLOR_MENUBAR + 1;
static std::atomic<HBRUSH> g_brushes[kSysColorCount];

static bool IsColorFixBrush(HGDIOBJ obj) {
    if (!obj) return false;
    for (const auto& slot : g_brushes)
        if (slot.load(std::memory_order_relaxed) == obj) return true;
    return false;
}

static HBRUSH SemanticBrush(int index) {
    if (index < 0 || index >= kSysColorCount) return nullptr;
    const COLORREF original = static_cast<COLORREF>(GetSysColor_Original(index));
    const COLORREF mapped = colorfix::MapSystemColor(index, original);
    if (mapped == original) return nullptr;

    auto& slot = g_brushes[index];
    if (HBRUSH brush = slot.load(std::memory_order_acquire)) return brush;

    HBRUSH created = CreateSolidBrush_Original(mapped);
    if (!created) return nullptr;
    HBRUSH expected = nullptr;
    if (slot.compare_exchange_strong(expected, created,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return created;
    DeleteObject_Original(created);
    return expected;
}

static DWORD WINAPI GetSysColor_Hook(int index) {
    const COLORREF original = static_cast<COLORREF>(GetSysColor_Original(index));
    return colorfix::MapSystemColor(index, original);
}

static HBRUSH WINAPI GetSysColorBrush_Hook(int index) {
    if (HBRUSH brush = SemanticBrush(index)) return brush;
    return GetSysColorBrush_Original(index);
}

static HGDIOBJ WINAPI GetStockObject_Hook(int object) {
    if (object == WHITE_BRUSH)
        if (HBRUSH brush = SemanticBrush(COLOR_WINDOW)) return brush;
    return GetStockObject_Original(object);
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

static BOOL WINAPI DeleteObject_Hook(HGDIOBJ obj) {
    if (IsColorFixBrush(obj)) return TRUE;
    return DeleteObject_Original(obj);
}

template <typename T>
static bool HookExport(HMODULE module, const char* name, void* hook, T* original) {
    auto target = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!target) { Wh_Log(L"ColorFix: export not found: %S", name); return false; }
    if (!Wh_SetFunctionHook(target, hook, reinterpret_cast<void**>(original))) {
        Wh_Log(L"ColorFix: failed to queue hook: %S", name); return false;
    }
    return true;
}

BOOL Wh_ModInit() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    HMODULE gdi32  = GetModuleHandleW(L"gdi32.dll");
    if (!user32 || !gdi32) return FALSE;

    bool ok = true;
    ok &= HookExport(user32, "GetSysColor",      reinterpret_cast<void*>(GetSysColor_Hook),      &GetSysColor_Original);
    ok &= HookExport(user32, "GetSysColorBrush", reinterpret_cast<void*>(GetSysColorBrush_Hook), &GetSysColorBrush_Original);
    ok &= HookExport(gdi32,  "GetStockObject",   reinterpret_cast<void*>(GetStockObject_Hook),   &GetStockObject_Original);
    ok &= HookExport(gdi32,  "SetTextColor",     reinterpret_cast<void*>(SetTextColor_Hook),     &SetTextColor_Original);
    ok &= HookExport(gdi32,  "SetBkColor",       reinterpret_cast<void*>(SetBkColor_Hook),       &SetBkColor_Original);
    ok &= HookExport(gdi32,  "CreateSolidBrush", reinterpret_cast<void*>(CreateSolidBrush_Hook), &CreateSolidBrush_Original);
    ok &= HookExport(gdi32,  "DeleteObject",     reinterpret_cast<void*>(DeleteObject_Hook),     &DeleteObject_Original);

    return ok ? TRUE : FALSE;
}

void Wh_ModUninit() {
}
