// ColorFixProbe - increment 1: Phase 1 hooks against three USER32/GDI paths.
// No Common Controls, no manifest, no early/late axis.
//
// Question: does the class background (hbrBackground = COLOR_WINDOW+1, erased
// by DefWindowProc) go through the hooked exports, or does it need Phase 1b?
//
// Window E (explicit): every surface is painted by our own WM_PAINT through a
//   hooked export. Positive controls for the hooks.
// Window K (class):    only DefWindowProc paints the background. The surface
//   under test.
//
// Each surface is captured with hooks disabled (baseline) and enabled (hooked),
// in both PrintWindow modes.
//
// Exit codes: 0 = infrastructure valid and all hook autotests PASS
//                 (MISS on a surface is a finding, not a failure)
//             1 = INFRASTRUCTURE_FAILURE (capture or backend not trustworthy)
//             2 = HOOK_BEHAVIOR_FAILURE (hook intercepted but produced a wrong value)
//             3 = setup failure
#include <windows.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "MinHook.h"
#include "colorfix_hooks.hpp"
#include "colorfix_runtime.hpp"
#include "uxtheme_observer.hpp"
#include "button_face_observer.hpp"

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

namespace cfh = colorfix::hooks;
namespace uxo = colorfix::probe::uxtheme_observer;
using cfh::HookId;

constexpr int kWidth = 320;
constexpr int kHeight = 200;
constexpr COLORREF kMagenta = RGB(255, 0, 255);
constexpr COLORREF kGreen = RGB(0, 128, 0);
constexpr COLORREF kWhite = RGB(255, 255, 255);
constexpr COLORREF kSentinel = RGB(1, 2, 3);
constexpr unsigned kDarkThreshold = 128;  // reported luma class threshold

// Brushes created before any hook is enabled: immune to mapping and counters.
HBRUSH g_magenta = nullptr;
HBRUSH g_green = nullptr;
constexpr COLORREF kOrange = RGB(200, 100, 0);  // luma 113: untouched by the mapper
HBRUSH g_orange = nullptr;
constexpr int kStaticId = 101, kEditId = 102, kButtonId = 103, kCustomStaticId = 104;

enum class Surface {
    LiteralBrush, BkColor, SysColorBrush, StockWhite, Magenta, ClassBg,
    CtlStatic, CtlEdit, CtlButton, CtlCustom
};

struct SurfaceDef {
    const char* name;
    char window;  // 'E', 'K' or 'C'
    RECT rect;
    Surface kind;
};

constexpr RECT kMagentaRect = {220, 110, 300, 190};

constexpr SurfaceDef kSurfaces[] = {
    {"E.literal-brush",  'E', {10, 10, 100, 90},   Surface::LiteralBrush},
    {"E.bkcolor-opaque", 'E', {110, 10, 200, 90},  Surface::BkColor},
    {"E.syscolor-brush", 'E', {210, 10, 300, 90},  Surface::SysColorBrush},
    {"E.stock-white",    'E', {10, 110, 100, 190}, Surface::StockWhite},
    {"E.magenta-ctl",    'E', kMagentaRect,        Surface::Magenta},
    {"K.class-bg",       'K', {10, 10, 300, 90},   Surface::ClassBg},
    {"K.magenta-ctl",    'K', kMagentaRect,        Surface::Magenta},
    // C: USER32 child controls without manifest. Rects are inset 8 px from
    // each 90x80 control to stay clear of borders and 3D edges.
    {"C.static-default", 'C', {18, 18, 92, 82},    Surface::CtlStatic},
    {"C.edit-default",   'C', {118, 18, 192, 82},  Surface::CtlEdit},
    {"C.button-face",    'C', {218, 18, 292, 82},  Surface::CtlButton},
    {"C.static-custom",  'C', {18, 118, 92, 182},  Surface::CtlCustom},
    {"C.magenta-ctl",    'C', kMagentaRect,        Surface::Magenta},
};

void PaintExplicit(HDC dc) {
    const RECT all{0, 0, kWidth, kHeight};
    FillRect(dc, &all, g_green);
    for (const auto& s : kSurfaces) {
        if (s.window != 'E') continue;
        switch (s.kind) {
        case Surface::LiteralBrush: {
            HBRUSH b = CreateSolidBrush(kWhite);
            FillRect(dc, &s.rect, b);
            DeleteObject(b);
            break;
        }
        case Surface::BkColor:
            SetBkColor(dc, kWhite);
            ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &s.rect, L"", 0, nullptr);
            break;
        case Surface::SysColorBrush:
            FillRect(dc, &s.rect, GetSysColorBrush(COLOR_WINDOW));
            break;
        case Surface::StockWhite:
            FillRect(dc, &s.rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            break;
        case Surface::Magenta:
            FillRect(dc, &s.rect, g_magenta);
            break;
        case Surface::ClassBg:
        case Surface::CtlStatic:
        case Surface::CtlEdit:
        case Surface::CtlButton:
        case Surface::CtlCustom:
            break;
        }
    }
}

void PaintClass(HDC dc) {
    FillRect(dc, &kMagentaRect, g_magenta);  // everything else: class brush
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const LONG_PTR tag = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    const bool explicitPaint =
        tag != 'K' && tag != 'C' && tag != 'V' && tag != 'L' && tag != 'T' && tag != 'U';
    switch (msg) {
    case WM_ERASEBKGND:
        if (explicitPaint) return 1;  // E paints its full client area itself
        break;                        // K, C: DefWindowProc erases with the class brush
    case WM_CTLCOLORSTATIC:
        // C: the custom static keeps an application-chosen color. The parent
        // answers itself, so ColorFix must neither see nor alter this reply.
        if (tag == 'C' && GetDlgCtrlID(reinterpret_cast<HWND>(lp)) == kCustomStaticId) {
            SetBkColor(reinterpret_cast<HDC>(wp), kOrange);
            return reinterpret_cast<LRESULT>(g_orange);
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        explicitPaint ? PaintExplicit(dc) : PaintClass(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        explicitPaint ? PaintExplicit(reinterpret_cast<HDC>(wp))
                      : PaintClass(reinterpret_cast<HDC>(wp));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- capture

struct Capture {
    BOOL ok = FALSE;
    std::vector<std::uint32_t> px;  // top-down BGRA
};

Capture CaptureWindow(HWND hwnd, UINT flags) {
    Capture c;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kWidth;
    bi.bmiHeader.biHeight = -kHeight;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!mem || !dib || !bits) {
        if (dib) DeleteObject(dib);
        if (mem) DeleteDC(mem);
        return c;
    }
    auto* p = static_cast<std::uint32_t*>(bits);
    const std::uint32_t sentinel = (GetRValue(kSentinel) << 16) |
                                   (GetGValue(kSentinel) << 8) | GetBValue(kSentinel);
    for (int i = 0; i < kWidth * kHeight; ++i) p[i] = sentinel;
    HGDIOBJ old = SelectObject(mem, dib);
    c.ok = PrintWindow(hwnd, mem, flags);
    GdiFlush();
    c.px.assign(p, p + kWidth * kHeight);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    return c;
}

COLORREF At(const Capture& c, int x, int y) {
    const std::uint32_t v = c.px[static_cast<size_t>(y) * kWidth + x];
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// Center + four corners inset 2 px. Returns false if the surface is not uniform.
bool SurfaceColor(const Capture& c, const RECT& r, COLORREF* out) {
    const int pts[5][2] = {
        {(r.left + r.right) / 2, (r.top + r.bottom) / 2},
        {r.left + 2, r.top + 2}, {r.right - 3, r.top + 2},
        {r.left + 2, r.bottom - 3}, {r.right - 3, r.bottom - 3},
    };
    const COLORREF first = At(c, pts[0][0], pts[0][1]);
    for (const auto& pt : pts)
        if (At(c, pt[0], pt[1]) != first) return false;
    *out = first;
    return true;
}

unsigned Luma(COLORREF c) {
    return (54u * GetRValue(c) + 183u * GetGValue(c) + 19u * GetBValue(c)) >> 8;
}

const char* LumaClass(COLORREF c) { return Luma(c) < kDarkThreshold ? "DARK" : "LIGHT"; }

void Pump(DWORD ms) {
    const DWORD until = GetTickCount() + ms;
    MSG m;
    while (GetTickCount() < until) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(10);
    }
}

// ---------------------------------------------------------------- counters

constexpr const char* kHookNames[] = {
    "GetSysColor", "GetSysColorBrush", "GetStockObject", "SetTextColor",
    "SetBkColor", "CreateSolidBrush", "DeleteObject", "DefWindowProcErase",
    "DefWindowProcCtlColor", "FillRect",
};
constexpr int kHookCount = static_cast<int>(HookId::Count);
static_assert(sizeof(kHookNames) / sizeof(kHookNames[0]) == kHookCount,
              "kHookNames must name every HookId");

struct Counts { long v[kHookCount]; };

Counts Snapshot() {
    Counts c{};
    for (int i = 0; i < kHookCount; ++i)
        c.v[i] = cfh::g_probeCalls[i].load(std::memory_order_relaxed);
    return c;
}

long Delta(const Counts& a, const Counts& b, HookId id) {
    return b.v[static_cast<int>(id)] - a.v[static_cast<int>(id)];
}

// ---------------------------------------------------------------- phases

struct WindowShot {
    Capture mode[2];  // [0] flags=0, [1] PW_RENDERFULLCONTENT
    Counts before{}, after{};
};

WindowShot Shoot(HWND hwnd) {
    WindowShot s;
    s.before = Snapshot();
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
    Pump(150);
    DwmFlush();
    s.mode[0] = CaptureWindow(hwnd, 0);
    s.mode[1] = CaptureWindow(hwnd, PW_RENDERFULLCONTENT);
    s.after = Snapshot();
    return s;
}

COLORREF BrushColor(HGDIOBJ brush) {
    LOGBRUSH lb{};
    return GetObjectW(brush, sizeof(lb), &lb) == sizeof(lb) ? lb.lbColor : CLR_INVALID;
}

struct Autotest {
    const char* name;
    long delta;
    bool valueOk;
    COLORREF expected, observed;
    bool expectNoCall = false;  // the hook must NOT see this call
};

void PrintRgb(COLORREF c) {
    if (c == CLR_INVALID) { std::printf("------"); return; }
    std::printf("%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
}

std::vector<void*> g_colorfixTargets;

bool RegisterWithMinHook(HMODULE module, const char* name, void* hook, void** original) {
    void* target = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!target) {
        std::printf("setup: export not found: %s\n", name);
        return false;
    }
    const MH_STATUS s = MH_CreateHook(target, hook, original);
    if (s != MH_OK) {
        std::printf("setup: MH_CreateHook(%s) = %s\n", name, MH_StatusToString(s));
        return false;
    }
    g_colorfixTargets.push_back(target);
    return true;
}

bool EnableColorFixHooks() {
    for (void* target : g_colorfixTargets) {
        const MH_STATUS s = MH_EnableHook(target);
        if (s != MH_OK) {
            std::printf("setup: MH_EnableHook(ColorFix target) = %s\n", MH_StatusToString(s));
            return false;
        }
    }
    return true;
}

HWND MakeWindow(const wchar_t* cls, char tag, int y, int x = 100) {
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"ColorFixProbe",
                             WS_POPUP, x, y, kWidth, kHeight, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (h) {
        SetWindowLongPtrW(h, GWLP_USERDATA, tag);
        if (tag == 'C') {
            auto child = [&](const wchar_t* cls, DWORD style, int x, int y, int id) {
                CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | style, x, y, 90, 80, h,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                GetModuleHandleW(nullptr), nullptr);
            };
            child(L"STATIC", 0, 10, 10, kStaticId);
            child(L"EDIT", WS_BORDER, 110, 10, kEditId);
            child(L"BUTTON", BS_PUSHBUTTON, 210, 10, kButtonId);
            child(L"STATIC", 0, 10, 110, kCustomStaticId);
        }
        ShowWindow(h, SW_SHOWNOACTIVATE);
        UpdateWindow(h);
    }
    return h;
}

// ------------------------------------------------------ Common Controls v6

constexpr char kV6Manifest[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
    "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n"
    "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" "
    "name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" "
    "processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
    "</dependentAssembly></dependency>\r\n"
    "</assembly>\r\n";

// Activation context for comctl32 v6 from a manifest written to %TEMP%.
// Windows E, K and C are created outside it and keep the USER32 classes.
HANDLE CreateV6ActCtx() {
    wchar_t path[MAX_PATH + 32] = {};
    const DWORD n = GetTempPathW(MAX_PATH, path);
    if (n == 0 || n >= MAX_PATH) return INVALID_HANDLE_VALUE;
    lstrcatW(path, L"colorfix_probe_v6.manifest");
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    DWORD written = 0;
    const BOOL ok = WriteFile(f, kV6Manifest, sizeof(kV6Manifest) - 1, &written, nullptr);
    CloseHandle(f);
    if (!ok || written != sizeof(kV6Manifest) - 1) return INVALID_HANDLE_VALUE;
    ACTCTXW ac{};
    ac.cbSize = sizeof(ac);
    ac.lpSource = path;
    return CreateActCtxW(&ac);
}

struct ComctlState {
    int modules = 0;  // comctl32.dll instances loaded in the process
    bool v6 = false;  // at least one reports DllGetVersion major >= 6
    unsigned major = 0, minor = 0;
};

// Module enumeration rather than GetModuleHandle, so activation-context
// redirection cannot decide what is observed.
ComctlState QueryComctl32() {
    ComctlState s;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, 0);
    if (snap == INVALID_HANDLE_VALUE) return s;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    for (BOOL more = Module32FirstW(snap, &me); more; more = Module32NextW(snap, &me)) {
        if (lstrcmpiW(me.szModule, L"comctl32.dll") != 0) continue;
        ++s.modules;
        struct DllVersion { DWORD cbSize, major, minor, build, platform; };
        using DllGetVersion_t = HRESULT (CALLBACK*)(DllVersion*);
        auto fn = reinterpret_cast<DllGetVersion_t>(
            reinterpret_cast<void*>(GetProcAddress(me.hModule, "DllGetVersion")));
        DllVersion v{sizeof(DllVersion), 0, 0, 0, 0};
        if (fn && SUCCEEDED(fn(&v)) && v.major >= 6) {
            s.v6 = true;
            s.major = v.major;
            s.minor = v.minor;
        }
    }
    CloseHandle(snap);
    return s;
}

void PrintComctl(const char* label, const ComctlState& s) {
    std::printf("comctl32: %-12s modules=%d v6=%d version=%u.%u\n", label, s.modules,
                s.v6 ? 1 : 0, s.major, s.minor);
}

constexpr int kListViewId = 105;

// Loads comctl32 v6 explicitly under the activation context and creates the
// v6 children of V (Static/Edit/Button) and L (ListView).
bool CreateV6Children(HANDLE actx, HWND v, HWND l) {
    ULONG_PTR cookie = 0;
    if (!ActivateActCtx(actx, &cookie)) return false;
    bool ok = false;
    if (HMODULE cc = LoadLibraryW(L"comctl32.dll")) {
        using InitCCEx_t = BOOL (WINAPI*)(const INITCOMMONCONTROLSEX*);
        auto init = reinterpret_cast<InitCCEx_t>(
            reinterpret_cast<void*>(GetProcAddress(cc, "InitCommonControlsEx")));
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
        if (init && init(&icc)) {
            HINSTANCE inst = GetModuleHandleW(nullptr);
            auto child = [&](HWND parent, const wchar_t* cls, DWORD style, int x, int y,
                             int w, int h, int id) {
                return CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                       parent,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                       inst, nullptr) != nullptr;
            };
            ok = child(v, L"STATIC", 0, 10, 10, 90, 80, kStaticId) &&
                 child(v, L"EDIT", WS_BORDER, 110, 10, 90, 80, kEditId) &&
                 child(v, L"BUTTON", BS_PUSHBUTTON, 210, 10, 90, 80, kButtonId) &&
                 child(l, WC_LISTVIEWW, LVS_REPORT | LVS_NOCOLUMNHEADER, 10, 10, 200, 80,
                       kListViewId);
        }
    }
    DeactivateActCtx(0, cookie);
    return ok;
}

struct V6Surface {
    const char* name;
    char window;  // 'V' or 'L'
    RECT rect;
    int role;     // system color whose mapped value means COVERED
};

constexpr V6Surface kV6Surfaces[] = {
    {"V.static-default", 'V', {18, 18, 92, 82},   COLOR_3DFACE},
    {"V.edit-default",   'V', {118, 18, 192, 82}, COLOR_WINDOW},
    {"V.button-face",    'V', {218, 18, 292, 82}, COLOR_3DFACE},
    {"L.listview-bg",    'L', {30, 30, 190, 80},  COLOR_WINDOW},
};

// Hypotheses are not assertions: a surface can legitimately be MISS.
// COVERED: uniform and equal to the mapped role color. MISS: center still
// LIGHT. INCONCLUSIVE: anything else (dark but not the mapped value, or a
// non-uniform themed surface).
const char* ClassifyV6(const Capture& c, const RECT& r, COLORREF expected, COLORREF* center) {
    *center = CLR_INVALID;
    if (c.px.empty()) return "INFRASTRUCTURE_FAILURE";
    *center = At(c, (r.left + r.right) / 2, (r.top + r.bottom) / 2);
    COLORREF uniform = CLR_INVALID;
    if (SurfaceColor(c, r, &uniform) && uniform == expected) return "COVERED";
    return Luma(*center) >= kDarkThreshold ? "MISS" : "INCONCLUSIVE";
}

// ------------------------------------------- increment 7: theme opt-out (T)

// T's controls are 86x76, not V's 90x80, so the size-based UxTheme
// attribution can tell T's draws from V's.
constexpr int kTW = 86, kTH = 76;

// Same classes and styles as V, created under the same v6 activation context.
bool CreateTChildren(HANDLE actx, HWND t) {
    ULONG_PTR cookie = 0;
    if (!ActivateActCtx(actx, &cookie)) return false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    auto child = [&](const wchar_t* cls, DWORD style, int x, int id) {
        return CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | style, x, 10, kTW, kTH, t,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst,
                               nullptr) != nullptr;
    };
    const bool ok = child(L"STATIC", 0, 10, kStaticId) &&
                    child(L"EDIT", WS_BORDER, 110, kEditId) &&
                    child(L"BUTTON", BS_PUSHBUTTON, 210, kButtonId);
    DeactivateActCtx(0, cookie);
    return ok;
}

// Documented API: L"" matches no visual-style section (theme off);
// NULL, NULL removes the association again (theme restored).
HRESULT ApplyWindowThemeToT(HWND t, LPCWSTR app, LPCWSTR ids) {
    using SetWindowTheme_t = HRESULT (WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    const auto fn = reinterpret_cast<SetWindowTheme_t>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"uxtheme.dll"), "SetWindowTheme")));
    if (!fn) return E_NOTIMPL;
    HRESULT worst = S_OK;
    for (int id : {kStaticId, kEditId, kButtonId}) {
        const HRESULT hr = fn(GetDlgItem(t, id), app, ids);
        if (FAILED(hr)) worst = hr;
    }
    return worst;
}

// Exact pixel equality over a surface rect: the reversibility oracle compares
// T against its own themed capture, so no tolerance is needed.
bool RegionEqual(const Capture& a, const Capture& b, const RECT& r) {
    if (a.px.empty() || b.px.empty()) return false;
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = r.left; x < r.right; ++x)
            if (At(a, x, y) != At(b, x, y)) return false;
    return true;
}

// Telemetry only: UxTheme draws of one class/part at a control size within a
// range of observer events.
long SizeDraws(LONG from, LONG to, const wchar_t* klass, int part, int w, int h) {
    const LONG lo = from < uxo::kMaxEvents ? from : uxo::kMaxEvents;
    const LONG hi = to < uxo::kMaxEvents ? to : uxo::kMaxEvents;
    long n = 0;
    for (LONG i = lo; i < hi; ++i) {
        const auto& ev = uxo::g_events[i];
        if (ev.kind == uxo::Event::Kind::Draw && ev.part == part && ev.width == w &&
            ev.height == h && std::wcscmp(uxo::ThemeClass(ev.theme), klass) == 0)
            ++n;
    }
    return n;
}

long TDraws(LONG from, LONG to, const wchar_t* klass, int part) {
    return SizeDraws(from, to, klass, part, kTW, kTH);
}

// ------------------------------------------- increment 9c: button text (U)

// U's controls are 82x72, distinct from V (90x80) and T (86x76), so the
// size-based UxTheme attribution stays unambiguous.
constexpr int kUW = 82, kUH = 72;
constexpr int kClassicId = 106;
constexpr RECT kUStatic{10, 10, 10 + kUW, 10 + kUH};
constexpr RECT kUEdit{110, 10, 110 + kUW, 10 + kUH};
constexpr RECT kUButton{210, 10, 210 + kUW, 10 + kUH};
constexpr RECT kUClassic{10, 110, 10 + kUW, 110 + kUH};
constexpr long kMinTextPixels = 40;  // below this the text was not captured
HFONT g_textFont = nullptr;  // bold, NONANTIALIASED_QUALITY; created before hooks

// v6 Static/Edit/Button under the activation context, like T; then a USER32
// BUTTON outside it, the same class as window C's button. Both buttons carry
// the text "MM" in g_textFont.
bool CreateUChildren(HANDLE actx, HWND u) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    auto child = [&](const wchar_t* cls, const wchar_t* text, DWORD style, const RECT& r,
                     int id) {
        HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, r.left, r.top,
                                 r.right - r.left, r.bottom - r.top, u,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst,
                                 nullptr);
        if (h && text[0])
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_textFont), FALSE);
        return h != nullptr;
    };
    ULONG_PTR cookie = 0;
    if (!ActivateActCtx(actx, &cookie)) return false;
    const bool v6 = child(L"STATIC", L"", 0, kUStatic, kStaticId) &&
                    child(L"EDIT", L"", WS_BORDER, kUEdit, kEditId) &&
                    child(L"BUTTON", L"MM", BS_PUSHBUTTON, kUButton, kButtonId);
    DeactivateActCtx(0, cookie);
    return v6 && child(L"BUTTON", L"MM", BS_PUSHBUTTON, kUClassic, kClassicId);
}

HRESULT ApplyWindowTheme(HWND h, LPCWSTR app, LPCWSTR ids) {
    using SetWindowTheme_t = HRESULT (WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    const auto fn = reinterpret_cast<SetWindowTheme_t>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"uxtheme.dll"), "SetWindowTheme")));
    return fn ? fn(h, app, ids) : E_NOTIMPL;
}

// Observable color histogram of a control interior (inset 6 px). The most
// frequent color is reported as the background; the next two are reported
// with their pixel counts. Which of them is the text is a reading of the
// output, not something this function decides.
struct TextStats {
    COLORREF bg = CLR_INVALID, t1 = CLR_INVALID, t2 = CLR_INVALID;
    long bgN = 0, t1N = 0, t2N = 0;
    int distinct = 0;
};

TextStats MeasureText(const Capture& c, const RECT& control) {
    TextStats s;
    if (c.px.empty()) return s;
    std::vector<std::pair<COLORREF, long>> h;
    for (int y = control.top + 6; y < control.bottom - 6; ++y) {
        for (int x = control.left + 6; x < control.right - 6; ++x) {
            const COLORREF v = At(c, x, y);
            auto it = std::find_if(h.begin(), h.end(),
                                   [v](const auto& e) { return e.first == v; });
            if (it == h.end())
                h.emplace_back(v, 1);
            else
                ++it->second;
        }
    }
    std::sort(h.begin(), h.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    s.distinct = static_cast<int>(h.size());
    if (h.size() > 0) { s.bg = h[0].first; s.bgN = h[0].second; }
    if (h.size() > 1) { s.t1 = h[1].first; s.t1N = h[1].second; }
    if (h.size() > 2) { s.t2 = h[2].first; s.t2N = h[2].second; }
    return s;
}

// WCAG 2 contrast ratio between two sRGB colors; 0 when either is missing.
double ContrastRatio(COLORREF a, COLORREF b) {
    if (a == CLR_INVALID || b == CLR_INVALID) return 0.0;
    auto channel = [](int v) {
        const double c = v / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    auto lum = [&](COLORREF c) {
        return 0.2126 * channel(GetRValue(c)) + 0.7152 * channel(GetGValue(c)) +
               0.0722 * channel(GetBValue(c));
    };
    const double la = lum(a), lb = lum(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

BOOL CALLBACK ForwardSysColorChange(HWND child, LPARAM) {
    SendMessageW(child, WM_SYSCOLORCHANGE, 0, 0);
    return TRUE;
}

// DefWindowProc does not forward WM_SYSCOLORCHANGE; controls need it directly.
void SendSysColorChange(HWND top) {
    SendMessageW(top, WM_SYSCOLORCHANGE, 0, 0);
    EnumChildWindows(top, ForwardSysColorChange, 0);
}

}  // namespace

int main(int argc, char** argv) {
    bool late = false;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--order=late") == 0) {
            late = true;
        } else if (std::strcmp(argv[a], "--order=early") != 0) {
            std::printf("setup: unknown argument %s\n", argv[a]);
            return 3;
        }
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::printf("ColorFixProbe increment 9c - button text and scoped theme opt-out (U)\n");
#if defined(_M_ARM64)
    std::printf("arch: ARM64\n");
#elif defined(_M_X64) || defined(__x86_64__)
    std::printf("arch: x64\n");
#else
    std::printf("arch: x86\n");
#endif
    std::printf("threshold: luma<%u = DARK\n", kDarkThreshold);
    std::printf("order: %s\n", late ? "late" : "early");

    g_magenta = CreateSolidBrush(kMagenta);
    g_green = CreateSolidBrush(kGreen);
    g_orange = CreateSolidBrush(kOrange);
    g_textFont = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ColorFixProbeHooks";
    if (!RegisterClassExW(&wc)) { std::printf("setup: RegisterClassExW failed\n"); return 3; }

    HWND winE = MakeWindow(wc.lpszClassName, 'E', 100);
    HWND winK = MakeWindow(wc.lpszClassName, 'K', 350);
    HWND winC = MakeWindow(wc.lpszClassName, 'C', 100, 450);
    HWND winV = MakeWindow(wc.lpszClassName, 'V', 350, 450);
    HWND winL = MakeWindow(wc.lpszClassName, 'L', 560, 100);
    HWND winT = MakeWindow(wc.lpszClassName, 'T', 560, 450);
    if (!winT) {
        std::printf("setup: T window failed (%lu)\n", GetLastError());
        return 3;
    }
    HWND winU = MakeWindow(wc.lpszClassName, 'U', 100, 780);
    if (!winU || !g_textFont) {
        std::printf("setup: U window or text font failed (%lu)\n", GetLastError());
        return 3;
    }
    HANDLE v6ctx = CreateV6ActCtx();
    if (!winV || !winL || v6ctx == INVALID_HANDLE_VALUE) {
        std::printf("setup: V/L windows or v6 activation context failed (%lu)\n", GetLastError());
        return 3;
    }
    PrintComctl("at-start", QueryComctl32());
    if (!winE || !winK || !winC) { std::printf("setup: CreateWindowExW failed\n"); return 3; }
    Pump(200);

    // Values needed for expectations, read before any hook is enabled.
    const COLORREF baseSysWindow = GetSysColor(COLOR_WINDOW);
    const HBRUSH baseSysBrush = GetSysColorBrush(COLOR_WINDOW);
    const COLORREF expWindow = colorfix::MapSystemColor(COLOR_WINDOW, baseSysWindow);
    const COLORREF expLiteral = colorfix::MapLiteralColor(kWhite);
    const COLORREF exp3dFace = colorfix::MapSystemColor(COLOR_3DFACE, GetSysColor(COLOR_3DFACE));
    const COLORREF expWindowText =
        colorfix::MapSystemColor(COLOR_WINDOWTEXT, GetSysColor(COLOR_WINDOWTEXT));
    std::printf("expect: COLOR_WINDOW ");
    PrintRgb(baseSysWindow); std::printf("->"); PrintRgb(expWindow);
    std::printf(", literal white ");
    PrintRgb(kWhite); std::printf("->"); PrintRgb(expLiteral);
    std::printf("\n");

    // Runtime policy: explicit ForceDark with real Windows signals keeps
    // increments 1-7 comparable. A real signal read failure invalidates the run.
    const colorfix::runtime::RefreshResult initial =
        colorfix::runtime::RefreshPolicy(colorfix::policy::Mode::ForceDark);
    std::printf("runtime: initial mode=ForceDark signals=%s hc=%d light=%d active=%d\n",
                initial.signalsOk ? "OK" : "FAILED", initial.signals.highContrast ? 1 : 0,
                initial.signals.appsUseLightTheme ? 1 : 0, initial.after ? 1 : 0);
    if (!initial.signalsOk) {
        std::printf("setup: ReadWindowsSignals failed\n");
        return 1;
    }

    // Phase A: initialize MinHook once. UxTheme observation is enabled now,
    // before any v6 child exists in either order. ColorFix hooks are only
    // created here and remain disabled until the early/late split below.
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK) { std::printf("setup: MH_Initialize = %s\n", MH_StatusToString(st)); return 1; }
    if (!uxo::Install(winV, winL)) return 1;
    if (!cfh::RegisterPhase1Hooks(RegisterWithMinHook)) return 1;

    const WindowShot baseE = Shoot(winE);
    const WindowShot baseK = Shoot(winK);
    const WindowShot baseC = Shoot(winC);

    // Order "late": comctl32 v6 is loaded, its controls created and painted,
    // all before any hook is enabled.
    WindowShot baseV, baseL;
    bool v6Ok = true;
    if (late) {
        v6Ok = CreateV6Children(v6ctx, winV, winL) && CreateTChildren(v6ctx, winT) &&
               CreateUChildren(v6ctx, winU);
        Pump(200);
        baseV = Shoot(winV);
        baseL = Shoot(winL);
    }
    const ComctlState ccBeforeHooks = QueryComctl32();

    // Control for DeleteObject.pass: the same check with hooks disabled.
    // Oracle: the process GDI object count. GetObjectType still reported
    // OBJ_BITMAP for deleted handles on all three runners, even without hooks.
    struct DeleteCheck { BOOL ret; long createdDelta; long finalDelta; };
    auto realDelete = [](DeleteCheck* c) {
        HANDLE self = GetCurrentProcess();
        const long before = static_cast<long>(GetGuiResources(self, GR_GDIOBJECTS));
        HBITMAP bmp = CreateBitmap(16, 16, 1, 32, nullptr);
        c->createdDelta = static_cast<long>(GetGuiResources(self, GR_GDIOBJECTS)) - before;
        c->ret = DeleteObject(bmp);
        c->finalDelta = static_cast<long>(GetGuiResources(self, GR_GDIOBJECTS)) - before;
        return bmp != nullptr && c->ret && c->createdDelta == 1 && c->finalDelta == 0;
    };
    DeleteCheck ctl{};
    const bool ctlOk = realDelete(&ctl);
    std::printf("detail: DeleteObject.control(no hooks) ret=%d created=%+ld final=%+ld %s\n",
                ctl.ret, ctl.createdDelta, ctl.finalDelta, ctlOk ? "OK" : "FAILED");

    // Phase B: enable only ColorFix targets. The passive UxTheme observer has
    // already been live since before V/L child creation.
    if (!EnableColorFixHooks()) return 1;

    // Order "early": hooks are live before comctl32 v6 is first loaded.
    if (!late) {
        v6Ok = CreateV6Children(v6ctx, winV, winL) && CreateTChildren(v6ctx, winT) &&
               CreateUChildren(v6ctx, winU);
        Pump(200);
    }
    const ComctlState ccAfterLoad = QueryComctl32();

    std::vector<Autotest> tests;
    auto run = [&](const char* name, HookId id, auto&& fn) {
        const Counts a = Snapshot();
        Autotest t{name, 0, false, CLR_INVALID, CLR_INVALID};
        fn(t);
        t.delta = Delta(a, Snapshot(), id);
        tests.push_back(t);
    };
    HDC testDc = CreateCompatibleDC(nullptr);

    run("GetSysColor", HookId::GetSysColor, [&](Autotest& t) {
        t.expected = expWindow;
        t.observed = GetSysColor(COLOR_WINDOW);
        t.valueOk = t.observed == t.expected;
    });
    run("GetSysColorBrush", HookId::GetSysColorBrush, [&](Autotest& t) {
        t.expected = expWindow;
        t.observed = BrushColor(GetSysColorBrush(COLOR_WINDOW));
        t.valueOk = t.observed == t.expected;
    });
    run("GetStockObject", HookId::GetStockObject, [&](Autotest& t) {
        t.expected = expWindow;
        t.observed = BrushColor(GetStockObject(WHITE_BRUSH));
        t.valueOk = t.observed == t.expected;
    });
    run("SetTextColor", HookId::SetTextColor, [&](Autotest& t) {
        SetTextColor(testDc, kWhite);
        t.expected = expLiteral;
        t.observed = GetTextColor(testDc);
        t.valueOk = t.observed == t.expected;
    });
    run("SetBkColor", HookId::SetBkColor, [&](Autotest& t) {
        SetBkColor(testDc, kWhite);
        t.expected = expLiteral;
        t.observed = GetBkColor(testDc);
        t.valueOk = t.observed == t.expected;
    });
    HBRUSH created = nullptr;
    run("CreateSolidBrush", HookId::CreateSolidBrush, [&](Autotest& t) {
        created = CreateSolidBrush(kWhite);
        t.expected = expLiteral;
        t.observed = BrushColor(created);
        t.valueOk = t.observed == t.expected;
    });
    DeleteObject(created);  // not asserted: gdi32 may cache deleted solid brushes

    // Split into independent checks so a failure names its exact condition.
    HBRUSH own = GetSysColorBrush(COLOR_WINDOW);
    BOOL ownRet = FALSE;
    run("DeleteObject.own", HookId::DeleteObject, [&](Autotest& t) {
        // A ColorFix brush must report success and stay usable.
        ownRet = DeleteObject(own);
        t.expected = expWindow;
        t.observed = BrushColor(own);
        t.valueOk = ownRet && GetObjectType(own) == OBJ_BRUSH && t.observed == t.expected;
    });
    std::printf("detail: DeleteObject.own ret=%d type=%lu\n", ownRet,
                static_cast<unsigned long>(GetObjectType(own)));
    DeleteCheck pass{};
    run("DeleteObject.pass", HookId::DeleteObject, [&](Autotest& t) {
        // Any other object must really be deleted. A bitmap is used because
        // solid brushes can be recycled by gdi32's client-side brush cache.
        t.valueOk = realDelete(&pass);
    });
    std::printf("detail: DeleteObject.pass ret=%d created=%+ld final=%+ld\n", pass.ret,
                pass.createdDelta, pass.finalDelta);
    run("DefWindowProcErase", HookId::DefWindowProcErase, [&](Autotest& t) {
        // Direct WM_ERASEBKGND on window K into a memory DIB: the detour must
        // fill it with the semantic COLOR_WINDOW brush and return 1.
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = kWidth;
        bi.bmiHeader.biHeight = -kHeight;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dib || !dc || !bits) {
            if (dc) DeleteDC(dc);
            if (dib) DeleteObject(dib);
            return;
        }
        HGDIOBJ old = SelectObject(dc, dib);
        const LRESULT r = DefWindowProcW(winK, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
        GdiFlush();
        const std::uint32_t v =
            static_cast<std::uint32_t*>(bits)[(kHeight / 2) * kWidth + kWidth / 2];
        SelectObject(dc, old);
        DeleteDC(dc);
        DeleteObject(dib);
        t.expected = expWindow;
        t.observed = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
        t.valueOk = r == 1 && t.observed == t.expected;
    });
    // WM_CTLCOLOR*: returned brush plus text and background left on the DC.
    auto rgbHex = [](COLORREF c) {
        return static_cast<unsigned long>((GetRValue(c) << 16) | (GetGValue(c) << 8) |
                                          GetBValue(c));
    };
    struct CtlCase { const char* name; UINT msg; int childId; COLORREF bg; };
    const CtlCase ctlCases[] = {
        {"CtlColor.static",  WM_CTLCOLORSTATIC,  kStaticId, exp3dFace},
        {"CtlColor.edit",    WM_CTLCOLOREDIT,    kEditId,   expWindow},
        {"CtlColor.btn",     WM_CTLCOLORBTN,     kButtonId, exp3dFace},
        {"CtlColor.listbox", WM_CTLCOLORLISTBOX, kEditId,   expWindow},
        {"CtlColor.dlg",     WM_CTLCOLORDLG,     0,         exp3dFace},
    };
    for (const auto& cc : ctlCases) {
        run(cc.name, HookId::DefWindowProcCtlColor, [&](Autotest& t) {
            HDC dc = CreateCompatibleDC(nullptr);
            HWND child = cc.childId ? GetDlgItem(winC, cc.childId) : winC;
            const LRESULT r = DefWindowProcW(winC, cc.msg, reinterpret_cast<WPARAM>(dc),
                                             reinterpret_cast<LPARAM>(child));
            const COLORREF brush = BrushColor(reinterpret_cast<HGDIOBJ>(r));
            const COLORREF bk = GetBkColor(dc);
            const COLORREF text = GetTextColor(dc);
            std::printf("detail: %s brush=%06lX bk=%06lX text=%06lX\n", cc.name,
                        rgbHex(brush), rgbHex(bk), rgbHex(text));
            t.expected = cc.bg;
            t.observed = brush;
            t.valueOk = brush == cc.bg && bk == cc.bg && text == expWindowText;
            DeleteDC(dc);
        });
    }
    {
        // Scrollbar: informational only; its default may be a pattern brush,
        // which the guard deliberately leaves alone.
        HDC dc = CreateCompatibleDC(nullptr);
        const Counts a = Snapshot();
        const LRESULT r = DefWindowProcW(winC, WM_CTLCOLORSCROLLBAR, reinterpret_cast<WPARAM>(dc),
                                         reinterpret_cast<LPARAM>(winC));
        std::printf("detail: CtlColor.scrollbar calls=%ld brush=%06lX (informational)\n",
                    Delta(a, Snapshot(), HookId::DefWindowProcCtlColor),
                    rgbHex(BrushColor(reinterpret_cast<HGDIOBJ>(r))));
        DeleteDC(dc);
    }
    {
        // Application-chosen color: the parent answers WM_CTLCOLORSTATIC itself,
        // so the hook must not be called and the reply must be preserved.
        HDC dc = CreateCompatibleDC(nullptr);
        const Counts a = Snapshot();
        const LRESULT r = SendMessageW(winC, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc),
                                       reinterpret_cast<LPARAM>(GetDlgItem(winC, kCustomStaticId)));
        Autotest t{"CtlColor.custom", Delta(a, Snapshot(), HookId::DefWindowProcCtlColor), false,
                   kOrange, BrushColor(reinterpret_cast<HGDIOBJ>(r))};
        t.expectNoCall = true;
        t.valueOk = reinterpret_cast<HBRUSH>(r) == g_orange && GetBkColor(dc) == kOrange;
        tests.push_back(t);
        DeleteDC(dc);
    }
    DeleteDC(testDc);

    // Increment 9b: product FillRect hook. Each case fills a 4x4 memory DIB
    // preset to a sentinel through the hooked FillRect and reads the center.
    // Brushes that must stay untouched are created or fetched through the
    // ColorFix originals, so no other hook decides the input.
    namespace cfp9 = colorfix::policy;
    // original=true calls the ColorFix trampoline directly: the unhooked result
    // for the same input on a fresh DC, used as a differential oracle.
    auto fillCenter = [](HBRUSH brush, bool original) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 4;
        bi.bmiHeader.biHeight = -4;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HDC dc = CreateCompatibleDC(nullptr);
        COLORREF out = CLR_INVALID;
        if (dib && dc && bits) {
            const HGDIOBJ old = SelectObject(dc, dib);
            auto* px = static_cast<std::uint32_t*>(bits);
            for (int i = 0; i < 16; ++i) px[i] = 0x00010203;  // kSentinel RGB(1, 2, 3)
            const RECT r{0, 0, 4, 4};
            if (original)
                cfh::FillRect_Original(dc, &r, brush);
            else
                FillRect(dc, &r, brush);
            GdiFlush();
            const std::uint32_t v = px[2 * 4 + 2];
            out = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
            SelectObject(dc, old);
        }
        if (dc) DeleteDC(dc);
        if (dib) DeleteObject(dib);
        return out;
    };
    const COLORREF origFace = static_cast<COLORREF>(cfh::GetSysColor_Original(COLOR_BTNFACE));
    const COLORREF origHighlight =
        static_cast<COLORREF>(cfh::GetSysColor_Original(COLOR_HIGHLIGHT));
    const HBRUSH sysFace = cfh::GetSysColorBrush_Original(COLOR_BTNFACE);
    const HBRUSH sysHighlight = cfh::GetSysColorBrush_Original(COLOR_HIGHLIGHT);
    std::printf("detail: FillRect.inputs face=%p highlight=%p unmapped-role=%d\n",
                static_cast<void*>(sysFace), static_cast<void*>(sysHighlight),
                colorfix::MapSystemColor(COLOR_HIGHLIGHT, origHighlight) == origHighlight ? 1 : 0);
    struct FillCase {
        const char* name;
        HBRUSH brush;
        cfp9::Signals signals;  // published with ForceDark for this case
        COLORREF expected;      // CLR_INVALID: the unhooked result (differential)
        long substitutions;     // expected product substitutions
        long pseudo;            // expected COLOR_x + 1 observations
    };
    HBRUSH sameColor = cfh::CreateSolidBrush_Original(origFace);
    const cfp9::Signals sigOn = initial.signals;
    // FillRect.pseudo and FillRect.null: the product must pass the value
    // through untouched. What USER32 then paints (COLOR_x + 1 resolution, NULL
    // brush) is its own behavior, so the oracle is the unhooked call on a fresh
    // DC with the same input, not a predicted color. Run 214 showed that a NULL
    // brush paints the DC's default brush (FFFFFF), not nothing.
    const FillCase fillCases[] = {
        {"FillRect.sys-on",    sysFace,      sigOn,         exp3dFace,     1, 0},
        {"FillRect.sys-off",   sysFace,      sigOn,         origFace,      0, 0},
        {"FillRect.hc-veto",   sysFace,      {true, false}, origFace,      0, 0},
        {"FillRect.samecolor", sameColor,    sigOn,         origFace,      0, 0},
        {"FillRect.unmapped",  sysHighlight, sigOn,         origHighlight, 0, 0},
        {"FillRect.stock", static_cast<HBRUSH>(cfh::GetStockObject_Original(WHITE_BRUSH)),
                                             sigOn,         kWhite,        0, 0},
        {"FillRect.semantic",  cfh::SemanticBrush(COLOR_BTNFACE),
                                             sigOn,         exp3dFace,     0, 0},
        {"FillRect.pseudo", reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1)),
                                             sigOn,         CLR_INVALID,   0, 1},
        {"FillRect.null",      nullptr,      sigOn,         CLR_INVALID,   0, 0},
    };
    for (const auto& fc : fillCases) {
        run(fc.name, HookId::FillRect, [&](Autotest& t) {
            const bool off = std::strcmp(fc.name, "FillRect.sys-off") == 0;
            cfp9::Publish(off ? cfp9::Mode::Disabled : cfp9::Mode::ForceDark, fc.signals);
            const long sub0 = cfh::g_fillRectSubstituted.load();
            const long ps0 = cfh::g_fillRectPseudo.load();
            t.expected = fc.expected != CLR_INVALID ? fc.expected : fillCenter(fc.brush, true);
            t.observed = fillCenter(fc.brush, false);
            const long sub = cfh::g_fillRectSubstituted.load() - sub0;
            const long ps = cfh::g_fillRectPseudo.load() - ps0;
            t.valueOk = t.expected != CLR_INVALID && t.observed == t.expected &&
                        sub == fc.substitutions && ps == fc.pseudo;
            if (!t.valueOk)
                std::printf("detail: %s substitutions=%ld pseudo=%ld\n", fc.name, sub, ps);
        });
    }
    cfp9::Publish(cfp9::Mode::ForceDark, initial.signals);  // back to the run's state
    DeleteObject(sameColor);

    // Identity cache: filled from the exports before any hook existed, it must
    // equal the ColorFix originals now, and a rebuild must not change it.
    HBRUSH cacheAtStart[cfh::kSysColorCount];
    int cacheMatch = 0, cacheCount = 0;
    for (int i = 0; i < cfh::kSysColorCount; ++i) {
        cacheAtStart[i] = cfh::g_sysBrushes[i].load();
        if (cacheAtStart[i]) ++cacheCount;
        if (cacheAtStart[i] == cfh::GetSysColorBrush_Original(i)) ++cacheMatch;
    }
    // Shared handles would make the first index win in SystemBrushIndex.
    int cacheDistinct = 0;
    for (int i = 0; i < cfh::kSysColorCount; ++i) {
        if (!cacheAtStart[i]) continue;
        bool seen = false;
        for (int j = 0; j < i && !seen; ++j) seen = cacheAtStart[j] == cacheAtStart[i];
        if (!seen) ++cacheDistinct;
    }
    const int rebuilt = cfh::RefreshSystemBrushCache();
    int rebuildSame = 0;
    for (int i = 0; i < cfh::kSysColorCount; ++i)
        if (cfh::g_sysBrushes[i].load() == cacheAtStart[i]) ++rebuildSame;
    {
        Autotest t{"FillRect.cache", 0, false, CLR_INVALID, CLR_INVALID};
        t.expectNoCall = true;  // identity check only: no fill
        t.valueOk = cacheCount > 0 && cacheMatch == cfh::kSysColorCount &&
                    rebuilt == cacheCount && rebuildSame == cfh::kSysColorCount;
        tests.push_back(t);
        std::printf("detail: FillRect.cache handles=%d distinct=%d original-match=%d/%d "
                    "rebuild=%d same=%d/%d\n",
                    cacheCount, cacheDistinct, cacheMatch, cfh::kSysColorCount, rebuilt,
                    rebuildSame, cfh::kSysColorCount);
    }
    // Scene telemetry starts here: autotest observations are excluded.
    const long fillSub0 = cfh::g_fillRectSubstituted.load();
    const long fillPseudo0 = cfh::g_fillRectPseudo.load();
    cfh::g_fillRectPseudoMask.store(0);

    // Phase C: hooked capture.
    const WindowShot hookE = Shoot(winE);
    const WindowShot hookK = Shoot(winK);
    const WindowShot hookC = Shoot(winC);
    const WindowShot hookV = Shoot(winV);
    const WindowShot hookL = Shoot(winL);

    // Increment 7: T themed -> SetWindowTheme(L"", L"") -> SetWindowTheme(NULL,
    // NULL). Observer event indices delimit each phase for the telemetry.
    const LONG evPre = uxo::g_eventCount;
    const WindowShot preT = Shoot(winT);
    const LONG evOff = uxo::g_eventCount;
    const HRESULT offHr = ApplyWindowThemeToT(winT, L"", L"");
    Pump(100);
    const WindowShot offT = Shoot(winT);
    const LONG evRestore = uxo::g_eventCount;
    const HRESULT restoreHr = ApplyWindowThemeToT(winT, nullptr, nullptr);
    Pump(100);
    const WindowShot restoredT = Shoot(winT);
    const LONG evEnd = uxo::g_eventCount;
    WindowShot afterV, afterL;
    if (late) {
        SendSysColorChange(winV);
        SendSysColorChange(winL);
        Pump(150);
        afterV = Shoot(winV);
        afterL = Shoot(winL);
    }

    // Increment-5 pixel oracle: observation must not change the established
    // rendering result. Exact colors intentionally make observer passivity an
    // infrastructure invariant rather than a new rendering expectation.
    auto shotHas = [](const WindowShot& shot, const RECT& r, COLORREF expected) {
        for (int m = 0; m < 2; ++m) {
            COLORREF c = CLR_INVALID;
            if (shot.mode[m].px.empty() || !SurfaceColor(shot.mode[m], r, &c) || c != expected)
                return false;
        }
        return true;
    };
    const bool observerAutotest = uxo::Autotest();
    bool observerPassive =
        shotHas(hookV, kV6Surfaces[0].rect, exp3dFace) &&
        shotHas(hookV, kV6Surfaces[1].rect, RGB(255, 255, 255)) &&
        shotHas(hookV, kV6Surfaces[2].rect, RGB(253, 253, 253));
    if (late) {
        observerPassive = observerPassive &&
            shotHas(hookL, kV6Surfaces[3].rect, RGB(255, 255, 255)) &&
            shotHas(afterL, kV6Surfaces[3].rect, expWindow);
    } else {
        observerPassive = observerPassive &&
            shotHas(hookL, kV6Surfaces[3].rect, expWindow);
    }
    if (!observerPassive) {
        // 9b: the FillRect product hook is also in this pixel chain; name the
        // surface that moved so the failure is not attributed blindly.
        std::printf("detail: v6-pixels");
        for (const auto& s : kV6Surfaces) {
            const WindowShot& h = s.window == 'V' ? hookV : hookL;
            std::printf(" %s=", s.name);
            PrintRgb(h.mode[0].px.empty()
                         ? CLR_INVALID
                         : At(h.mode[0], (s.rect.left + s.rect.right) / 2,
                              (s.rect.top + s.rect.bottom) / 2));
        }
        std::printf("\n");
    }

    // Phase D: dynamic policy. Hooks stay installed; only the effective state
    // changes. Every surface is compared with the baseline captures (policy
    // OFF) or the hooked captures (policy ON) taken above, per capture mode.
    namespace cfp = colorfix::policy;
    HBRUSH owned = GetSysColorBrush(COLOR_WINDOW);  // ColorFix brush while ON
    struct PolicyStep {
        const char* name;
        cfp::Mode mode;
        cfp::Signals signals;
        bool expectOn;
    };
    const PolicyStep steps[] = {
        {"off-disabled", cfp::Mode::Disabled,     {false, false}, false},
        {"on-forcedark", cfp::Mode::ForceDark,    {false, true},  true},
        {"hc-veto",      cfp::Mode::ForceDark,    {true,  false}, false},
        {"follow-light", cfp::Mode::FollowSystem, {false, true},  false},
        {"follow-dark",  cfp::Mode::FollowSystem, {false, false}, true},
    };
    const char* const policyMode[2] = {"flags0", "full"};
    bool policyOk = true;
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        const PolicyStep& st = steps[i];
        cfp::Publish(st.mode, st.signals);
        const bool active = cfp::Active();
        const WindowShot now[3] = {Shoot(winE), Shoot(winK), Shoot(winC)};
        const WindowShot* base[3] = {&baseE, &baseK, &baseC};
        const WindowShot* hooked[3] = {&hookE, &hookK, &hookC};
        int match = 0, total = 0;
        for (const auto& s : kSurfaces) {
            const int wi = s.window == 'E' ? 0 : s.window == 'K' ? 1 : 2;
            const WindowShot& ref = st.expectOn ? *hooked[wi] : *base[wi];
            for (int m = 0; m < 2; ++m) {
                COLORREF want = CLR_INVALID, got = CLR_INVALID;
                const bool captured = !ref.mode[m].px.empty() && !now[wi].mode[m].px.empty();
                const bool ok = captured && SurfaceColor(ref.mode[m], s.rect, &want) &&
                                SurfaceColor(now[wi].mode[m], s.rect, &got) && want == got;
                ++total;
                if (ok) {
                    ++match;
                    continue;
                }
                std::printf("detail: policy %s %s %s want=", st.name, s.name, policyMode[m]);
                PrintRgb(want);
                std::printf(" got=");
                PrintRgb(got);
                std::printf("\n");
            }
        }
        // Interception must continue while OFF: counters sit before the gate.
        const long erase = now[1].after.v[static_cast<int>(HookId::DefWindowProcErase)] -
                           now[1].before.v[static_cast<int>(HookId::DefWindowProcErase)];
        const bool stepOk = active == st.expectOn && match == total && erase > 0;
        if (!stepOk) policyOk = false;
        std::printf("policy: %-13s active=%d match=%d/%d K.erase-intercepted=%ld %s\n",
                    st.name, active ? 1 : 0, match, total, erase, stepOk ? "PASS" : "FAIL");

        if (i == 0) {
            // Ownership oracle: while OFF, deleting a ColorFix brush must not
            // destroy it. GetObjectType alone is not proof (increment 1 showed
            // it still reports deleted handles), so the process GDI object
            // count must also stay unchanged.
            HANDLE self = GetCurrentProcess();
            const long before = static_cast<long>(GetGuiResources(self, GR_GDIOBJECTS));
            const BOOL ret = DeleteObject(owned);
            const long gdiDelta = static_cast<long>(GetGuiResources(self, GR_GDIOBJECTS)) - before;
            const DWORD type = GetObjectType(owned);
            const COLORREF color = BrushColor(owned);
            const bool ok = ret && gdiDelta == 0 && type == OBJ_BRUSH && color == expWindow;
            if (!ok) policyOk = false;
            std::printf("policy: ownership-off delete=%d gdi=%+ld type=%lu color=", ret, gdiDelta,
                        static_cast<unsigned long>(type));
            PrintRgb(color);
            std::printf(" %s\n", ok ? "PASS" : "FAIL");
        } else if (i == 1) {
            // Back ON: the same persistent brush is handed out again.
            const bool same = GetSysColorBrush(COLOR_WINDOW) == owned;
            if (!same) policyOk = false;
            std::printf("policy: ownership-on same-handle=%d %s\n", same ? 1 : 0,
                        same ? "PASS" : "FAIL");
        }
    }
    cfp::Publish(cfp::Mode::ForceDark, {});

    // ------------------------------------------ Phase 9a: button face causality
    // Policy ON, product hooks live. Candidates are intervened one at a time,
    // only in the target button's paint; causality is control vs experiment of
    // the same state, never the mapped color. Verdicts are findings; only
    // infrastructure (install, autotests, captures, magenta, restoration,
    // product hooks afterwards) can fail the run.
    namespace bfo = colorfix::probe::button_face;
    std::printf("\n[button face 9a] causal characterization, policy ON\n");
    bool bfInfra = true;
    InterlockedExchange(&uxo::g_paused, 1);
    bfo::g_setBkColorRaw = cfh::SetBkColor_Original;
    bfo::g_createBrushRaw = cfh::CreateSolidBrush_Original;
    bfo::g_fillRectRaw = cfh::FillRect_Original;
    const bool bfInstalled = bfo::Install();
    if (!bfInstalled) bfInfra = false;
    // FillRect candidate: tap inside the product hook (single owner of the target).
    if (bfInstalled) cfh::g_probeFillRectTap.store(&bfo::FillRectTap);
    uxo::g_drawIntercept = &bfo::DrawThemeIntercept;
    bool bfCandidateOk[bfo::kCandidates] = {};
    for (int c = 0; bfInstalled && c < bfo::kCandidates; ++c) {
        const bfo::AutotestResult at = bfo::Autotest(c);
        bfCandidateOk[c] = at.ok;
        if (!at.ok) bfInfra = false;
        std::printf("autotest: bf.%-19s calls=%ld control=", bfo::kName[c], at.calls);
        PrintRgb(at.control);
        std::printf(" experiment=");
        PrintRgb(at.experiment);
        std::printf(" %s\n", at.ok ? "PASS" : "INFRASTRUCTURE_FAILURE");
    }

    struct BfTarget { const char* name; HWND parent; HWND button; RECT face; };
    const BfTarget bfTargets[] = {
        {"C",     winC, GetDlgItem(winC, kButtonId), {218, 18, 292, 82}},
        {"T-off", winT, GetDlgItem(winT, kButtonId), {218, 18, 288, 78}},
        {"V",     winV, GetDlgItem(winV, kButtonId), {218, 18, 292, 82}},
    };
    for (const auto& t : bfTargets)
        if (!bfo::Subclass(t.button)) bfInfra = false;

    // T is characterized opted out; it must return to its own themed capture.
    const WindowShot preT9 = Shoot(winT);
    if (FAILED(ApplyWindowThemeToT(winT, L"", L""))) bfInfra = false;
    Pump(100);

    auto bfMagentaOk = [](const WindowShot& s) {
        for (int m = 0; m < 2; ++m) {
            COLORREF c = CLR_INVALID;
            if (s.mode[m].px.empty() || !SurfaceColor(s.mode[m], kMagentaRect, &c) ||
                c != kMagenta)
                return false;
        }
        return true;
    };
    auto bfShoot = [&](const BfTarget& t) {
        RedrawWindow(t.button, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
        WindowShot s = Shoot(t.parent);
        if (!bfMagentaOk(s)) bfInfra = false;
        return s;
    };
    auto bfApplyState = [](HWND b, int state, bool on) {
        switch (state) {
        case 1: SendMessageW(b, BM_SETSTATE, on ? TRUE : FALSE, 0); break;
        case 2: EnableWindow(b, on ? FALSE : TRUE); break;
        case 3: SendMessageW(b, BM_SETSTYLE, on ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE); break;
        default: break;
        }
    };
    const char* const bfStates[4] = {"NORMAL", "PRESSED", "DISABLED", "DEFAULTED"};
    std::printf("buttonface: legend M=CAUSAL_MARKER D=CAUSAL_DIFFERENCE n=NON_CAUSAL "
                "-=NOT_OBSERVED X=INFRASTRUCTURE; two codes = flags0,full\n");
    std::printf("buttonface: HOVER EXCLUDED_NONDETERMINISTIC\n");
    for (const auto& t : bfTargets) {
        long bfAtt[bfo::kCandidates] = {}, bfDc[bfo::kCandidates] = {};
        const int cx = (t.face.left + t.face.right) / 2, cy = (t.face.top + t.face.bottom) / 2;
        for (int s = 0; s < 4; ++s) {
            bfApplyState(t.button, s, true);
            Pump(50);
            const WindowShot control = bfShoot(t);
            char codes[bfo::kCandidates][3] = {};
            for (int c = 0; c < bfo::kCandidates; ++c) {
                codes[c][0] = codes[c][1] = 'X';
                if (!bfCandidateOk[c]) continue;
                const long att0 = bfo::g_attributed[c].load();
                const long dc0 = bfo::g_dcMatch[c].load();
                bfo::g_target.store(t.button);
                bfo::g_active.store(c);
                const WindowShot exp = bfShoot(t);
                bfo::g_active.store(-1);
                bfo::g_target.store(nullptr);
                const long att = bfo::g_attributed[c].load() - att0;
                bfAtt[c] += att;
                bfDc[c] += bfo::g_dcMatch[c].load() - dc0;
                for (int m = 0; m < 2; ++m) {
                    const Capture& a = control.mode[m];
                    const Capture& b = exp.mode[m];
                    if (a.px.empty() || b.px.empty()) continue;
                    if (att == 0) { codes[c][m] = '-'; continue; }
                    long diff = 0, total = 0;
                    for (int y = t.face.top; y < t.face.bottom; ++y)
                        for (int x = t.face.left; x < t.face.right; ++x) {
                            ++total;
                            if (At(a, x, y) != At(b, x, y)) ++diff;
                        }
                    const COLORREF ca = At(a, cx, cy), cb = At(b, cx, cy);
                    if (bfo::kSubstitutes[c])
                        codes[c][m] = (cb == bfo::kMarker && ca != bfo::kMarker) ? 'M' : 'n';
                    else  // the difference must replace the face, not just its border
                        codes[c][m] = (ca != cb && diff * 2 >= total) ? 'D' : 'n';
                }
            }
            bfApplyState(t.button, s, false);
            Pump(50);
            std::printf("buttonface: %s/%-9s face=", t.name, bfStates[s]);
            PrintRgb(control.mode[0].px.empty() ? CLR_INVALID : At(control.mode[0], cx, cy));
            for (int c = 0; c < bfo::kCandidates; ++c)
                std::printf(" %s=%s", bfo::kShort[c], codes[c]);
            std::printf("\n");
        }
        // Independent evidence: calls in the target's paint whose DC is the
        // button's own window DC (a memory DC gives 0).
        std::printf("buttonface: %s windowfromdc", t.name);
        for (int c = 0; c < bfo::kCandidates; ++c)
            std::printf(" %s=%ld/%ld", bfo::kShort[c], bfDc[c], bfAtt[c]);
        std::printf("\n");
    }

    // 9b measurement: which brush identity the classic face FillRect receives.
    // Recorded before substitution, in every experiment shot of C and T-off.
    // A system brush handle, a COLOR_x+1 index or another brush lead to
    // different product rules, so this is measured before any 9b hook exists.
    auto describeBrush = [](HBRUSH b, char* out, size_t n) {
        const auto v = reinterpret_cast<ULONG_PTR>(b);
        LOGBRUSH lb{};
        const bool haveLog = GetObjectW(b, sizeof(lb), &lb) == sizeof(lb);
        const unsigned long rgb =
            haveLog ? static_cast<unsigned long>((GetRValue(lb.lbColor) << 16) |
                                                 (GetGValue(lb.lbColor) << 8) |
                                                 GetBValue(lb.lbColor))
                    : 0ul;
        if (v > 0 && v <= static_cast<ULONG_PTR>(cfh::kSysColorCount)) {
            std::snprintf(out, n, "index:COLOR+1 color-index=%lu",
                          static_cast<unsigned long>(v - 1));
            return;
        }
        for (int i = 0; i < cfh::kSysColorCount; ++i) {
            if (b == cfh::GetSysColorBrush_Original(i)) {
                std::snprintf(out, n, "sysbrush:%d color=%06lX", i, rgb);
                return;
            }
        }
        if (cfh::IsColorFixBrush(b)) {
            std::snprintf(out, n, "colorfix-semantic color=%06lX", rgb);
            return;
        }
        std::snprintf(out, n, "other type=%lu style=%u color=%06lX",
                      static_cast<unsigned long>(GetObjectType(b)),
                      haveLog ? lb.lbStyle : 0xFFFFu, rgb);
    };
    const long bfSamples = bfo::g_brushSampleCount.load() < bfo::kMaxBrushSamples
                               ? bfo::g_brushSampleCount.load()
                               : bfo::kMaxBrushSamples;
    if (bfo::g_brushSamplesDropped.load() > 0) bfInfra = false;
    for (int ti = 0; ti < 2; ++ti) {  // C and T-off: the causal classic faces
        const BfTarget& t = bfTargets[ti];
        char kinds[4][64] = {};
        long counts[4] = {};
        int nKinds = 0;
        long other = 0;
        for (long i = 0; i < bfSamples; ++i) {
            if (bfo::g_brushSamples[i].target != t.button) continue;
            char desc[64];
            describeBrush(bfo::g_brushSamples[i].brush, desc, sizeof(desc));
            int k = 0;
            while (k < nKinds && std::strcmp(kinds[k], desc) != 0) ++k;
            if (k == nKinds) {
                if (nKinds == 4) { ++other; continue; }
                std::snprintf(kinds[nKinds], sizeof(kinds[nKinds]), "%s", desc);
                ++nKinds;
            }
            ++counts[k];
        }
        for (int k = 0; k < nKinds; ++k)
            std::printf("buttonface: %s fillrect-brush %s calls=%ld\n", t.name, kinds[k],
                        counts[k]);
        std::printf("buttonface: %s fillrect-brush kinds=%d more=%ld dropped=%ld\n", t.name,
                    nKinds, other, bfo::g_brushSamplesDropped.load());
    }

    for (const auto& t : bfTargets) bfo::Unsubclass(t.button);
    uxo::g_drawIntercept = nullptr;
    cfh::g_probeFillRectTap.store(nullptr);
    bfo::Uninstall();
    InterlockedExchange(&uxo::g_paused, 0);

    // Restoration: T back to its themed capture; C and V back to the hooked
    // captures of the coverage phase.
    if (FAILED(ApplyWindowThemeToT(winT, nullptr, nullptr))) bfInfra = false;
    Pump(100);
    const WindowShot postT9 = Shoot(winT);
    const WindowShot postC9 = Shoot(winC);
    const WindowShot postV9 = Shoot(winV);
    bool bfRestored = true;
    const RECT bfTRects[3] = {{18, 18, 88, 78}, {118, 18, 188, 78}, {218, 18, 288, 78}};
    const RECT bfFace{218, 18, 292, 82};
    for (int m = 0; m < 2; ++m) {
        for (const RECT& r : bfTRects)
            if (!RegionEqual(preT9.mode[m], postT9.mode[m], r)) bfRestored = false;
        if (!RegionEqual(hookC.mode[m], postC9.mode[m], bfFace)) bfRestored = false;
        if (!RegionEqual(hookV.mode[m], postV9.mode[m], bfFace)) bfRestored = false;
    }
    if (!bfRestored) bfInfra = false;
    std::printf("buttonface: restored T/C/V %s\n", bfRestored ? "SAME" : "INFRASTRUCTURE_FAILURE");

    // Product hooks must still intercept after the 9a detours were removed.
    const long bfGs0 = cfh::g_probeCalls[static_cast<int>(HookId::GetSysColor)].load();
    const COLORREF bfLive = GetSysColor(COLOR_WINDOW);
    const bool bfProductLive =
        bfLive == expWindow &&
        cfh::g_probeCalls[static_cast<int>(HookId::GetSysColor)].load() > bfGs0;
    if (!bfProductLive) bfInfra = false;
    std::printf("buttonface: product-hooks-after GetSysColor=");
    PrintRgb(bfLive);
    std::printf(" %s\n", bfProductLive ? "LIVE" : "INFRASTRUCTURE_FAILURE");

    // ------------------------------------ Phase 9c: button text and scoped opt-out
    // Measurement only: no product change. Policy ON unless a step says OFF,
    // product hooks live, UxTheme observer live again after 9a. Gates: G1 the
    // U button alone goes themed -> opted out (COVERED face) -> exactly
    // restored; G2 its themed Static/Edit siblings do not change; G3 the 82x72
    // Button draws vanish while opted out and Edit draws continue. Text colors
    // and contrast are findings.
    std::printf("\n[button text 9c] U: scoped theme opt-out and text colors\n");
    bool uInfra = true, uBehavior = true;
    // Run 221 reported INFRASTRUCTURE_FAILURE without naming the condition:
    // each infrastructure condition is now counted separately.
    long uMagentaFail = 0, uHrFail = 0, uTextShort = 0, uEquivFail = 0;
    const HWND uButton = GetDlgItem(winU, kButtonId);
    const HWND uClassic = GetDlgItem(winU, kClassicId);
    if (!uButton || !uClassic) uInfra = false;
    auto uShoot = [&](HWND button) {
        if (button)
            RedrawWindow(button, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
        WindowShot s = Shoot(winU);
        for (int m = 0; m < 2; ++m) {
            COLORREF c = CLR_INVALID;
            if (s.mode[m].px.empty() || !SurfaceColor(s.mode[m], kMagentaRect, &c) ||
                c != kMagenta)
                ++uMagentaFail;
        }
        return s;
    };
    cfp::Publish(cfp::Mode::ForceDark, {});
    // Warm-up: in run 221 the first capture of U gave a black button interior
    // (themed=000000) in all six processes while the themed draws were
    // observed. The first capture is kept as a reported finding and is not
    // part of the gates or of the magenta infrastructure count.
    {
        RedrawWindow(uButton, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
        const WindowShot warm = Shoot(winU);
        std::printf("buttontext: detail first-capture button=");
        PrintRgb(MeasureText(warm.mode[0], kUButton).bg);
        std::printf("/");
        PrintRgb(MeasureText(warm.mode[1], kUButton).bg);
        for (int m = 0; m < 2; ++m) {
            COLORREF c = CLR_INVALID;
            const bool ok = !warm.mode[m].px.empty() &&
                            SurfaceColor(warm.mode[m], kMagentaRect, &c) && c == kMagenta;
            std::printf(" magenta%d=%s", m, ok ? "VALID" : "INVALID");
        }
        std::printf("\n");
    }
    const LONG uEv0 = uxo::g_eventCount;
    const WindowShot uThemed = uShoot(uButton);
    const LONG uEvOff = uxo::g_eventCount;
    const HRESULT uOffHr = ApplyWindowTheme(uButton, L"", L"");
    Pump(100);
    const WindowShot uOff = uShoot(uButton);
    const LONG uEvRestore = uxo::g_eventCount;
    const HRESULT uRestoreHr = ApplyWindowTheme(uButton, nullptr, nullptr);
    Pump(100);
    const WindowShot uRestored = uShoot(uButton);
    const LONG uEvEnd = uxo::g_eventCount;
    if (FAILED(uOffHr) || FAILED(uRestoreHr)) ++uHrFail;

    bool g1 = true, g2 = true;
    for (int m = 0; m < 2; ++m) {
        if (MeasureText(uOff.mode[m], kUButton).bg != exp3dFace) g1 = false;
        if (!RegionEqual(uThemed.mode[m], uRestored.mode[m], kUButton)) g1 = false;
        for (const RECT* r : {&kUStatic, &kUEdit})
            if (!RegionEqual(uThemed.mode[m], uOff.mode[m], *r) ||
                !RegionEqual(uThemed.mode[m], uRestored.mode[m], *r))
                g2 = false;
    }
    if (!g1 || !g2) uBehavior = false;
    std::printf("buttontext: gate-9c G1 button themed=");
    PrintRgb(MeasureText(uThemed.mode[0], kUButton).bg);
    std::printf(" off=");
    PrintRgb(MeasureText(uOff.mode[0], kUButton).bg);
    std::printf(" restored=%s %s\n",
                RegionEqual(uThemed.mode[0], uRestored.mode[0], kUButton) ? "SAME" : "DIFFERENT",
                g1 ? "PASS" : "FAIL");
    std::printf("buttontext: gate-9c G2 siblings static/edit unchanged %s\n",
                g2 ? "PASS" : "FAIL");

    const long uBtn[3] = {SizeDraws(uEv0, uEvOff, L"Button", 1, kUW, kUH),
                          SizeDraws(uEvOff, uEvRestore, L"Button", 1, kUW, kUH),
                          SizeDraws(uEvRestore, uEvEnd, L"Button", 1, kUW, kUH)};
    const long uEdit[3] = {SizeDraws(uEv0, uEvOff, L"Edit", 3, kUW, kUH),
                           SizeDraws(uEvOff, uEvRestore, L"Edit", 3, kUW, kUH),
                           SizeDraws(uEvRestore, uEvEnd, L"Edit", 3, kUW, kUH)};
    const bool g3Interpretable = uBtn[0] > 0 && uEdit[0] > 0;
    const bool g3 = uBtn[1] == 0 && uEdit[1] > 0 && uBtn[2] > 0;
    if (g3Interpretable && !g3) uBehavior = false;
    std::printf("buttontext: gate-9c G3 %dx%d Button pre=%ld off=%ld restored=%ld"
                " Edit pre=%ld off=%ld restored=%ld %s\n",
                kUW, kUH, uBtn[0], uBtn[1], uBtn[2], uEdit[0], uEdit[1], uEdit[2],
                !g3Interpretable ? "INCONCLUSIVE" : g3 ? "PASS" : "FAIL");

    // Text matrix: 3 configurations x policy OFF/ON x 4 deterministic states.
    // Per state: background:top1xN,top2xN,distinct,contrast(background, top1)
    // from flags0; m= counts states whose bg/top1/top2 agree across both
    // capture modes. The classic face must match window C's (OFF F0F0F0, ON
    // mapped): an equivalence requirement for the measurement, not a finding.
    auto uApplyState = [](HWND b, int state, bool on) {
        switch (state) {
        case 1: SendMessageW(b, BM_SETSTATE, on ? TRUE : FALSE, 0); break;
        case 2: EnableWindow(b, on ? FALSE : TRUE); break;
        case 3: SendMessageW(b, BM_SETSTYLE, on ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE); break;
        default: break;
        }
    };
    struct TextConfig { const char* name; HWND button; RECT rect; bool optOut; };
    const TextConfig textConfigs[] = {
        {"classic", uClassic, kUClassic, false},
        {"themed",  uButton,  kUButton,  false},
        {"optout",  uButton,  kUButton,  true},
    };
    const char* const stateTag[4] = {"N", "P", "D", "F"};
    const COLORREF classicOffFace =
        static_cast<COLORREF>(cfh::GetSysColor_Original(COLOR_BTNFACE));
    for (const auto& tc : textConfigs) {
        if (!tc.button) continue;
        if (tc.optOut) {
            if (FAILED(ApplyWindowTheme(tc.button, L"", L""))) ++uHrFail;
            Pump(100);
        }
        for (int on = 0; on < 2; ++on) {
            cfp::Publish(on ? cfp::Mode::ForceDark : cfp::Mode::Disabled, {});
            TextStats ts[4][2];
            int agree = 0;
            for (int sIdx = 0; sIdx < 4; ++sIdx) {
                uApplyState(tc.button, sIdx, true);
                Pump(50);
                const WindowShot shot = uShoot(tc.button);
                for (int m = 0; m < 2; ++m) ts[sIdx][m] = MeasureText(shot.mode[m], tc.rect);
                uApplyState(tc.button, sIdx, false);
                Pump(50);
                if (ts[sIdx][0].t1N < kMinTextPixels) ++uTextShort;
                if (ts[sIdx][0].bg == ts[sIdx][1].bg && ts[sIdx][0].t1 == ts[sIdx][1].t1 &&
                    ts[sIdx][0].t2 == ts[sIdx][1].t2)
                    ++agree;
            }
            if (std::strcmp(tc.name, "classic") == 0 &&
                ts[0][0].bg != (on ? exp3dFace : classicOffFace))
                ++uEquivFail;
            std::printf("buttontext: %s/%s m=%d/4", tc.name, on ? "on" : "off", agree);
            for (int sIdx = 0; sIdx < 4; ++sIdx) {
                const TextStats& t = ts[sIdx][0];
                std::printf(" %s=", stateTag[sIdx]);
                PrintRgb(t.bg);
                std::printf("x%ld:", t.bgN);
                PrintRgb(t.t1);
                std::printf("x%ld,", t.t1N);
                PrintRgb(t.t2);
                std::printf("x%ld,d%d,cr%.1f", t.t2N, t.distinct, ContrastRatio(t.bg, t.t1));
            }
            std::printf("\n");
        }
        if (tc.optOut) {
            if (FAILED(ApplyWindowTheme(tc.button, nullptr, nullptr))) ++uHrFail;
            Pump(100);
        }
    }
    cfp::Publish(cfp::Mode::ForceDark, {});
    if (uMagentaFail || uHrFail || uTextShort || uEquivFail) uInfra = false;
    std::printf("buttontext: infrastructure magenta-fail=%ld hr-fail=%ld text-short=%ld"
                " classic-equiv-fail=%ld %s\n",
                uMagentaFail, uHrFail, uTextShort, uEquivFail,
                uInfra ? "VALID" : "INFRASTRUCTURE_FAILURE");

    // ------------------------------------------------------------ report
    bool infraOk = true, behaviorOk = true;
    if (!ctlOk) infraOk = false;  // DeleteObject.pass has no valid control
    if (!bfInfra) infraOk = false;  // 9a infrastructure
    if (!uInfra) infraOk = false;   // 9c infrastructure
    if (!uBehavior) behaviorOk = false;  // 9c gates G1-G3
    if (!observerAutotest || !observerPassive) infraOk = false;
    if (!policyOk) behaviorOk = false;

    std::printf("\n[autotest]\n");
    for (const auto& t : tests) {
        const bool infraFail = !t.expectNoCall && t.delta == 0;
        const bool behaviorFail =
            !infraFail && (!t.valueOk || (t.expectNoCall && t.delta != 0));
        const char* verdict = infraFail    ? "INFRASTRUCTURE_FAILURE"
                            : behaviorFail ? "HOOK_BEHAVIOR_FAILURE"
                                           : "PASS";
        if (infraFail) infraOk = false;
        if (behaviorFail) behaviorOk = false;
        std::printf("autotest: %-16s calls=%ld expected=", t.name, t.delta);
        PrintRgb(t.expected);
        std::printf(" observed=");
        PrintRgb(t.observed);
        std::printf(" %s\n", verdict);
    }
    std::printf("autotest: raw GetSysColorBrush(COLOR_WINDOW) baseline brush=%p\n",
                static_cast<void*>(baseSysBrush));

    const char* modeName[2] = {"flags0", "full"};
    bool gateC = true;
    std::printf("\n[surfaces]\n");
    for (const auto& s : kSurfaces) {
        const WindowShot& b = s.window == 'E' ? baseE : s.window == 'K' ? baseK : baseC;
        const WindowShot& h = s.window == 'E' ? hookE : s.window == 'K' ? hookK : hookC;
        const bool control = s.kind == Surface::Magenta;
        const bool custom = s.kind == Surface::CtlCustom;
        COLORREF expected = expWindow;
        switch (s.kind) {
        case Surface::Magenta:      expected = kMagenta; break;
        case Surface::CtlCustom:    expected = kOrange; break;
        case Surface::LiteralBrush:
        case Surface::BkColor:      expected = expLiteral; break;
        case Surface::CtlStatic:
        case Surface::CtlButton:    expected = exp3dFace; break;
        default:                    expected = expWindow; break;
        }
        std::printf("surface: %-17s", s.name);
        for (int m = 0; m < 2; ++m) {
            COLORREF bc = CLR_INVALID, hc = CLR_INVALID;
            const bool capOk = b.mode[m].ok && h.mode[m].ok && !b.mode[m].px.empty() &&
                               !h.mode[m].px.empty();
            const bool bu = capOk && SurfaceColor(b.mode[m], s.rect, &bc);
            const bool hu = capOk && SurfaceColor(h.mode[m], s.rect, &hc);
            const char* verdict;
            if (!capOk) {
                verdict = "INFRASTRUCTURE_FAILURE";
                infraOk = false;
            } else if (!bu || !hu) {
                verdict = "NONUNIFORM";
                if (control || custom) infraOk = false;
            } else if (control) {
                const bool ok = bc == kMagenta && hc == kMagenta;
                verdict = ok ? "VALID" : "INFRASTRUCTURE_FAILURE";
                if (!ok) infraOk = false;
            } else if (custom) {
                // A baseline orange also proves child controls are captured.
                if (bc != kOrange) {
                    verdict = "INFRASTRUCTURE_FAILURE";
                    infraOk = false;
                } else if (hc == kOrange) {
                    verdict = "PRESERVED";
                } else {
                    verdict = "OVERRIDDEN";
                    behaviorOk = false;
                }
            } else if (bc == expected) {
                verdict = "NOT_APPLICABLE";
            } else if (hc == expected) {
                verdict = "COVERED";
            } else if (hc == bc) {
                verdict = "MISS";
            } else {
                verdict = "UNEXPECTED";
            }
            // 9b gate: the classic C face must be COVERED in both capture modes.
            if (s.kind == Surface::CtlButton && std::strcmp(verdict, "COVERED") != 0) {
                gateC = false;
                if (std::strcmp(verdict, "INFRASTRUCTURE_FAILURE") != 0) behaviorOk = false;
            }
            std::printf(" | %s ", modeName[m]);
            PrintRgb(bc); std::printf("->"); PrintRgb(hc);
            std::printf(" %s", verdict);
            if (!control && bu && hu) std::printf("(%s->%s)", LumaClass(bc), LumaClass(hc));
        }
        std::printf("\n");
    }
    std::printf("surface: gate-9b C.button-face %s\n", gateC ? "PASS" : "FAIL");

    std::printf("\n[scenario counters] hooked repaint+capture, per window\n");
    for (char w : {'E', 'K', 'C'}) {
        const WindowShot& h = w == 'E' ? hookE : w == 'K' ? hookK : hookC;
        std::printf("scenario: %c", w);
        for (int i = 0; i < kHookCount; ++i) {
            const long d = h.after.v[i] - h.before.v[i];
            // DeleteObject is also called by the probe's own capture code.
            if (static_cast<HookId>(i) == HookId::DeleteObject) {
                std::printf(" %s=n/a", kHookNames[i]);
                continue;
            }
            std::printf(" %s=%ld(%s)", kHookNames[i], d, d ? "OBSERVED" : "NOT_OBSERVED");
        }
        std::printf("\n");
    }

    // ------------------------------------------------ Common Controls v6
    std::printf("\n[common controls v6] order=%s\n", late ? "late" : "early");
    PrintComctl("before-hooks", ccBeforeHooks);
    PrintComctl("after-load", ccAfterLoad);
    // Invariants that make the order meaningful. Violating them invalidates
    // the measurement, not ColorFix.
    const bool orderOk =
        v6Ok && ccAfterLoad.v6 && (late ? ccBeforeHooks.v6 : !ccBeforeHooks.v6);
    if (!orderOk) infraOk = false;
    std::printf("v6: order-invariant children=%d v6-before-hooks=%d v6-after-load=%d %s\n",
                v6Ok ? 1 : 0, ccBeforeHooks.v6 ? 1 : 0, ccAfterLoad.v6 ? 1 : 0,
                orderOk ? "VALID" : "INFRASTRUCTURE_FAILURE");

    const WindowShot* v6Shots[] = {&hookV, &hookL, late ? &baseV : nullptr,
                                   late ? &baseL : nullptr, late ? &afterV : nullptr,
                                   late ? &afterL : nullptr};
    bool v6Magenta = true;
    for (const WindowShot* s : v6Shots) {
        if (!s) continue;
        for (int m = 0; m < 2; ++m) {
            COLORREF c = CLR_INVALID;
            if (s->mode[m].px.empty() || !SurfaceColor(s->mode[m], kMagentaRect, &c) ||
                c != kMagenta)
                v6Magenta = false;
        }
    }
    if (!v6Magenta) infraOk = false;
    std::printf("v6: magenta-ctl %s\n", v6Magenta ? "VALID" : "INFRASTRUCTURE_FAILURE");

    for (const auto& s : kV6Surfaces) {
        const COLORREF expected = s.role == COLOR_WINDOW ? expWindow : exp3dFace;
        const WindowShot& h = s.window == 'V' ? hookV : hookL;
        std::printf("v6: %-17s", s.name);
        for (int m = 0; m < 2; ++m) {
            std::printf(" | %s", modeName[m]);
            if (late) {
                COLORREF bc;
                ClassifyV6((s.window == 'V' ? baseV : baseL).mode[m], s.rect, expected, &bc);
                std::printf(" base=");
                PrintRgb(bc);
            }
            COLORREF hc;
            const char* hv = ClassifyV6(h.mode[m], s.rect, expected, &hc);
            if (std::strcmp(hv, "INFRASTRUCTURE_FAILURE") == 0) infraOk = false;
            std::printf(" hooked=");
            PrintRgb(hc);
            std::printf(" %s", hv);
            if (late) {
                COLORREF ac;
                const char* av =
                    ClassifyV6((s.window == 'V' ? afterV : afterL).mode[m], s.rect, expected, &ac);
                if (std::strcmp(av, "INFRASTRUCTURE_FAILURE") == 0) infraOk = false;
                const bool recovered =
                    std::strcmp(hv, "COVERED") != 0 && std::strcmp(av, "COVERED") == 0;
                std::printf(" syscolorchange=");
                PrintRgb(ac);
                std::printf(" %s", recovered ? "COVERED_AFTER_SYSCOLORCHANGE" : av);
            }
        }
        std::printf("\n");
    }
    for (char w : {'V', 'L'}) {
        const WindowShot& h = w == 'V' ? hookV : hookL;
        std::printf("v6scenario: %c", w);
        for (int i = 0; i < kHookCount; ++i) {
            const long d = h.after.v[i] - h.before.v[i];
            if (static_cast<HookId>(i) == HookId::DeleteObject) {
                std::printf(" %s=n/a", kHookNames[i]);
                continue;
            }
            std::printf(" %s=%ld(%s)", kHookNames[i], d, d ? "OBSERVED" : "NOT_OBSERVED");
        }
        std::printf("\n");
    }

    // ------------------------------------------- increment 7: theme opt-out
    std::printf("\n[theme opt-out] T: themed -> SetWindowTheme(L\"\", L\"\") -> (NULL, NULL)\n");
    const bool themeCallsOk = SUCCEEDED(offHr) && SUCCEEDED(restoreHr);
    if (!themeCallsOk) infraOk = false;
    std::printf("themeoff: set-hr=0x%08lX restore-hr=0x%08lX %s\n",
                static_cast<unsigned long>(offHr), static_cast<unsigned long>(restoreHr),
                themeCallsOk ? "VALID" : "INFRASTRUCTURE_FAILURE");

    bool tMagenta = true;
    for (const WindowShot* s : {&preT, &offT, &restoredT}) {
        for (int m = 0; m < 2; ++m) {
            COLORREF c = CLR_INVALID;
            if (s->mode[m].px.empty() || !SurfaceColor(s->mode[m], kMagentaRect, &c) ||
                c != kMagenta)
                tMagenta = false;
        }
    }
    if (!tMagenta) infraOk = false;
    std::printf("themeoff: magenta-ctl %s\n", tMagenta ? "VALID" : "INFRASTRUCTURE_FAILURE");

    struct TSurface { const char* name; RECT rect; int role; };
    const TSurface kTSurfaces[] = {
        {"T.static", {18, 18, 88, 78},   COLOR_3DFACE},
        {"T.edit",   {118, 18, 188, 78}, COLOR_WINDOW},
        {"T.button", {218, 18, 288, 78}, COLOR_3DFACE},
    };
    // Visual verdicts are findings, not assertions. Reversibility is a gate:
    // after (NULL, NULL) each surface must equal T's own themed capture.
    bool reversible = true;
    bool gateT = true;
    for (const auto& s : kTSurfaces) {
        const COLORREF expected = s.role == COLOR_WINDOW ? expWindow : exp3dFace;
        std::printf("themeoff: %-8s", s.name);
        for (int m = 0; m < 2; ++m) {
            COLORREF pc, oc, rc;
            const char* pv = ClassifyV6(preT.mode[m], s.rect, expected, &pc);
            const char* ov = ClassifyV6(offT.mode[m], s.rect, expected, &oc);
            const char* rv = ClassifyV6(restoredT.mode[m], s.rect, expected, &rc);
            for (const char* v : {pv, ov, rv})
                if (std::strcmp(v, "INFRASTRUCTURE_FAILURE") == 0) infraOk = false;
            const bool same = RegionEqual(preT.mode[m], restoredT.mode[m], s.rect);
            if (!same) reversible = false;
            // 9b gate: the opted-out (classic) T face must be COVERED.
            if (std::strcmp(s.name, "T.button") == 0 && std::strcmp(ov, "COVERED") != 0) {
                gateT = false;
                if (std::strcmp(ov, "INFRASTRUCTURE_FAILURE") != 0) behaviorOk = false;
            }
            std::printf(" | %s themed=", modeName[m]);
            PrintRgb(pc);
            std::printf(" %s off=", pv);
            PrintRgb(oc);
            std::printf(" %s restored=", ov);
            PrintRgb(rc);
            std::printf(" %s", same ? "SAME" : "DIFFERENT");
        }
        std::printf("\n");
    }
    if (!reversible) behaviorOk = false;
    std::printf("themeoff: reversibility %s\n", reversible ? "PASS" : "FAIL");
    std::printf("themeoff: gate-9b T-off.button %s\n", gateT ? "PASS" : "FAIL");

    const long preEdit = TDraws(evPre, evOff, L"Edit", 3);
    const long offEdit = TDraws(evOff, evRestore, L"Edit", 3);
    const long resEdit = TDraws(evRestore, evEnd, L"Edit", 3);
    const long preBtn = TDraws(evPre, evOff, L"Button", 1);
    const long offBtn = TDraws(evOff, evRestore, L"Button", 1);
    const long resBtn = TDraws(evRestore, evEnd, L"Button", 1);
    // Telemetry is only interpretable if T's themed draws were seen at all.
    std::printf("themeoff: telemetry %dx%d Edit/EP_BACKGROUND pre=%ld off=%ld restored=%ld"
                " Button/BP_PUSHBUTTON pre=%ld off=%ld restored=%ld %s\n",
                kTW, kTH, preEdit, offEdit, resEdit, preBtn, offBtn, resBtn,
                (preEdit > 0 && preBtn > 0) ? "INTERPRETABLE" : "INCONCLUSIVE");

    std::printf("\n[uxtheme observer]\n");
    std::printf("uxtheme: passive-pixels %s\n",
                observerPassive ? "VALID" : "INFRASTRUCTURE_FAILURE");
    uxo::PrintReport();

    // ------------------------------------------------ Phase E: runtime wiring
    // Product listener (hooks/colorfix_runtime.hpp) in FollowSystem, driven by
    // the real signal: HKCU AppsUseLightTheme plus a WM_SETTINGCHANGE
    // "ImmersiveColorSet" broadcast. Reception, policy transition and visual
    // result are separate verdicts; the ListView cache refresh is a finding.
    std::printf("\n[runtime wiring] FollowSystem listener, real signals\n");
    namespace cfr = colorfix::runtime;
    const wchar_t* const kPersonalize =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    DWORD origLight = 1;
    DWORD origSize = sizeof(origLight);
    const LSTATUS origStatus = RegGetValueW(HKEY_CURRENT_USER, kPersonalize, L"AppsUseLightTheme",
                                            RRF_RT_REG_DWORD, nullptr, &origLight, &origSize);
    const bool listenerOk = cfr::StartListener(cfp::Mode::FollowSystem);
    if (!listenerOk) infraOk = false;
    std::printf("runtime: listener %s\n", listenerOk ? "STARTED" : "INFRASTRUCTURE_FAILURE");

    struct RuntimeStep { const char* name; DWORD light; bool expectOn; };
    const RuntimeStep runtimeSteps[] = {
        {"to-light",   1, false},
        {"to-dark",    0, true},
        {"back-light", 1, false},
    };
    auto broadcastThemeChange = [] {
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                            reinterpret_cast<LPARAM>(L"ImmersiveColorSet"), SMTO_ABORTIFHUNG,
                            2000, &ignored);
    };
    for (const auto& rs : runtimeSteps) {
        if (!listenerOk) break;
        const long seen0 = cfr::g_signalsSeen.load();
        const long trans0 = cfr::g_transitions.load();
        const long fail0 = cfr::g_signalFailures.load();
        const bool before = cfp::Active();
        const LSTATUS written = RegSetKeyValueW(HKEY_CURRENT_USER, kPersonalize,
                                                L"AppsUseLightTheme", REG_DWORD, &rs.light,
                                                sizeof(rs.light));
        broadcastThemeChange();
        // Wait for the listener thread, then let the posted WM_SYSCOLORCHANGE
        // and the scheduled repaints run on this thread before capturing.
        for (int t = 0; t < 60 && cfr::g_signalsSeen.load() == seen0; ++t) Pump(50);
        Pump(200);
        const long seen = cfr::g_signalsSeen.load() - seen0;
        const long trans = cfr::g_transitions.load() - trans0;
        const long fails = cfr::g_signalFailures.load() - fail0;
        const bool after = cfp::Active();

        const WindowShot now[3] = {Shoot(winE), Shoot(winK), Shoot(winC)};
        const WindowShot* base[3] = {&baseE, &baseK, &baseC};
        const WindowShot* hooked[3] = {&hookE, &hookK, &hookC};
        int match = 0, total = 0;
        for (const auto& s : kSurfaces) {
            const int wi = s.window == 'E' ? 0 : s.window == 'K' ? 1 : 2;
            const WindowShot& ref = rs.expectOn ? *hooked[wi] : *base[wi];
            for (int m = 0; m < 2; ++m) {
                COLORREF want = CLR_INVALID, got = CLR_INVALID;
                ++total;
                if (!ref.mode[m].px.empty() && !now[wi].mode[m].px.empty() &&
                    SurfaceColor(ref.mode[m], s.rect, &want) &&
                    SurfaceColor(now[wi].mode[m], s.rect, &got) && want == got)
                    ++match;
            }
        }
        const WindowShot nowL = Shoot(winL);
        COLORREF lc = CLR_INVALID;
        ClassifyV6(nowL.mode[0], kV6Surfaces[3].rect, expWindow, &lc);
        const bool listFollows = lc == (rs.expectOn ? expWindow : baseSysWindow);

        const bool received = written == ERROR_SUCCESS && seen > 0;
        const bool semanticOk =
            fails == 0 && after == rs.expectOn && trans == (before != rs.expectOn ? 1 : 0);
        const bool visualOk = match == total;
        if (!received) infraOk = false;
        else if (!semanticOk || !visualOk) behaviorOk = false;
        std::printf("runtime: %-10s light=%lu received=%ld transitions=%ld signal-failures=%ld "
                    "active=%d->%d surfaces=%d/%d listview=",
                    rs.name, static_cast<unsigned long>(rs.light), seen, trans, fails,
                    before ? 1 : 0, after ? 1 : 0, match, total);
        PrintRgb(lc);
        std::printf(" %s %s\n", listFollows ? "FOLLOWS" : "STALE",
                    !received                  ? "INFRASTRUCTURE_FAILURE"
                    : (semanticOk && visualOk) ? "PASS"
                                               : "FAIL");
    }
    cfr::StopListener();

    // Restore the runner's original setting whatever happened above.
    const LSTATUS restored =
        origStatus == ERROR_SUCCESS
            ? RegSetKeyValueW(HKEY_CURRENT_USER, kPersonalize, L"AppsUseLightTheme", REG_DWORD,
                              &origLight, sizeof(origLight))
            : RegDeleteKeyValueW(HKEY_CURRENT_USER, kPersonalize, L"AppsUseLightTheme");
    broadcastThemeChange();
    if (restored != ERROR_SUCCESS) infraOk = false;
    std::printf("runtime: restore AppsUseLightTheme=%s %s\n",
                origStatus == ERROR_SUCCESS ? (origLight ? "1" : "0") : "absent",
                restored == ERROR_SUCCESS ? "OK" : "INFRASTRUCTURE_FAILURE");

    // 9b premise: system brush handles are stable, so the identity cache needs
    // no runtime refresh. A finding, not a gate: a change invalidates the
    // premise and is handled in a later increment.
    int stableCache = 0, stableOrig = 0;
    for (int i = 0; i < cfh::kSysColorCount; ++i) {
        if (cfh::g_sysBrushes[i].load() == cacheAtStart[i]) ++stableCache;
        if (cfh::GetSysColorBrush_Original(i) == cacheAtStart[i]) ++stableOrig;
    }
    std::printf("runtime: sysbrush-identity cache=%d/%d original=%d/%d %s\n", stableCache,
                cfh::kSysColorCount, stableOrig, cfh::kSysColorCount,
                stableOrig == cfh::kSysColorCount ? "PREMISE_HOLDS" : "PREMISE_BROKEN");
    std::printf("runtime: fillrect scenes substitutions=%ld pseudo=%ld pseudo-mask=%08lX\n",
                cfh::g_fillRectSubstituted.load() - fillSub0,
                cfh::g_fillRectPseudo.load() - fillPseudo0, cfh::g_fillRectPseudoMask.load());

    // Phase E must run with the same installed hooks it is validating. Tear
    // MinHook down only after the listener has stopped and the runner setting
    // has been restored.
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    const int code = !infraOk ? 1 : !behaviorOk ? 2 : 0;
    std::printf("\nsummary: infrastructure=%s hooks=%s exit=%d\n",
                infraOk ? "VALID" : "INFRASTRUCTURE_FAILURE",
                behaviorOk ? "PASS" : "HOOK_BEHAVIOR_FAILURE", code);
    DestroyWindow(winE);
    DestroyWindow(winK);
    DestroyWindow(winC);
    DestroyWindow(winV);
    DestroyWindow(winL);
    DestroyWindow(winT);
    DestroyWindow(winU);
    ReleaseActCtx(v6ctx);
    return code;
}
