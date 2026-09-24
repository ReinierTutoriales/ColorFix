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
#include <cstdint>
#include <initializer_list>
#include <cstdio>
#include <cstring>
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
        tag != 'K' && tag != 'C' && tag != 'V' && tag != 'L' && tag != 'T';
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
    "DefWindowProcCtlColor",
};
constexpr int kHookCount = static_cast<int>(HookId::Count);

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

// Telemetry only: UxTheme draws of one class/part at T's control size within
// a range of observer events.
long TDraws(LONG from, LONG to, const wchar_t* klass, int part) {
    const LONG lo = from < uxo::kMaxEvents ? from : uxo::kMaxEvents;
    const LONG hi = to < uxo::kMaxEvents ? to : uxo::kMaxEvents;
    long n = 0;
    for (LONG i = lo; i < hi; ++i) {
        const auto& ev = uxo::g_events[i];
        if (ev.kind == uxo::Event::Kind::Draw && ev.part == part && ev.width == kTW &&
            ev.height == kTH && std::wcscmp(uxo::ThemeClass(ev.theme), klass) == 0)
            ++n;
    }
    return n;
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
    std::printf("ColorFixProbe increment 9a - button face causal characterization\n");
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
        v6Ok = CreateV6Children(v6ctx, winV, winL) && CreateTChildren(v6ctx, winT);
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
        v6Ok = CreateV6Children(v6ctx, winV, winL) && CreateTChildren(v6ctx, winT);
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
    const bool bfInstalled = bfo::Install();
    if (!bfInstalled) bfInfra = false;
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

    for (const auto& t : bfTargets) bfo::Unsubclass(t.button);
    uxo::g_drawIntercept = nullptr;
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

    // ------------------------------------------------------------ report
    bool infraOk = true, behaviorOk = true;
    if (!ctlOk) infraOk = false;  // DeleteObject.pass has no valid control
    if (!bfInfra) infraOk = false;  // 9a infrastructure
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
            std::printf(" | %s ", modeName[m]);
            PrintRgb(bc); std::printf("->"); PrintRgb(hc);
            std::printf(" %s", verdict);
            if (!control && bu && hu) std::printf("(%s->%s)", LumaClass(bc), LumaClass(hc));
        }
        std::printf("\n");
    }

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
    ReleaseActCtx(v6ctx);
    return code;
}
