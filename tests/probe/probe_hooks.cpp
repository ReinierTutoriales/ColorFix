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
#include <cstdint>
#include <initializer_list>
#include <cstdio>
#include <vector>

#include "MinHook.h"
#include "colorfix_hooks.hpp"

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

namespace cfh = colorfix::hooks;
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
    const bool explicitPaint = tag != 'K' && tag != 'C';
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

}  // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::printf("ColorFixProbe increment 3 - Phase 1 + 1b hooks + WM_CTLCOLOR, no Common Controls\n");
#if defined(_M_ARM64)
    std::printf("arch: ARM64\n");
#elif defined(_M_X64) || defined(__x86_64__)
    std::printf("arch: x64\n");
#else
    std::printf("arch: x86\n");
#endif
    std::printf("threshold: luma<%u = DARK\n", kDarkThreshold);

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

    // Phase A: baseline. Hooks created but NOT enabled.
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK) { std::printf("setup: MH_Initialize = %s\n", MH_StatusToString(st)); return 1; }
    if (!cfh::RegisterPhase1Hooks(RegisterWithMinHook)) return 1;

    const WindowShot baseE = Shoot(winE);
    const WindowShot baseK = Shoot(winK);
    const WindowShot baseC = Shoot(winC);

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

    // Phase B: enable hooks, autotest each one with a direct call.
    st = MH_EnableHook(MH_ALL_HOOKS);
    if (st != MH_OK) { std::printf("setup: MH_EnableHook = %s\n", MH_StatusToString(st)); return 1; }

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

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    // ------------------------------------------------------------ report
    bool infraOk = true, behaviorOk = true;
    if (!ctlOk) infraOk = false;  // DeleteObject.pass has no valid control

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

    const int code = !infraOk ? 1 : !behaviorOk ? 2 : 0;
    std::printf("\nsummary: infrastructure=%s hooks=%s exit=%d\n",
                infraOk ? "VALID" : "INFRASTRUCTURE_FAILURE",
                behaviorOk ? "PASS" : "HOOK_BEHAVIOR_FAILURE", code);
    DestroyWindow(winE);
    DestroyWindow(winK);
    DestroyWindow(winC);
    return code;
}
