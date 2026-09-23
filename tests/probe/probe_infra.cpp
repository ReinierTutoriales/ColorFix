// ColorFixProbe - increment 0: capture infrastructure only.
// No hooks, no Common Controls, no ColorFix policy. Answers one question:
// does PrintWindow return trustworthy pixels on this runner/architecture?
//
// Exit codes: 0 = both capture modes VALID
//             1 = at least one mode INFRASTRUCTURE_FAILURE
//             3 = setup failure (window could not be created/validated)
#include <windows.h>
#include <dwmapi.h>
#include <cstdint>
#include <cstdio>
#include <vector>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 200;
constexpr RECT kControl = {100, 60, 180, 140};    // positive control (magenta)
constexpr COLORREF kMagenta = RGB(255, 0, 255);
constexpr COLORREF kBackground = RGB(0, 128, 0);  // must differ from magenta
constexpr COLORREF kSentinel = RGB(1, 2, 3);      // DIB prefill: "untouched"

struct Sample {
    const char* name;
    int x, y;
    COLORREF expected;
};

// Inside points are inset 2 px from the control edges; outside points are
// 3 px beyond them, plus two far background points.
constexpr Sample kSamples[] = {
    {"control.center",   140, 100, kMagenta},
    {"control.topleft",  102,  62, kMagenta},
    {"control.topright", 177,  62, kMagenta},
    {"control.botleft",  102, 137, kMagenta},
    {"control.botright", 177, 137, kMagenta},
    {"bg.left-of-ctl",    97, 100, kBackground},
    {"bg.right-of-ctl",  183, 100, kBackground},
    {"bg.near-origin",    10,  10, kBackground},
    {"bg.far-corner",    310, 190, kBackground},
};

void Paint(HDC dc) {
    const RECT all{0, 0, kWidth, kHeight};
    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    HBRUSH mg = CreateSolidBrush(kMagenta);
    FillRect(dc, &kControl, mg);
    DeleteObject(mg);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        Paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        Paint(reinterpret_cast<HDC>(wp));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

const char* ArchName() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "ARM64";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

void ReportEnvironment() {
    std::printf("ColorFixProbe increment 0 — capture infrastructure\n");
    std::printf("arch: %s\n", ArchName());

    using RtlGetVersion_t = LONG (WINAPI*)(OSVERSIONINFOW*);
    auto rtlGetVersion = reinterpret_cast<RtlGetVersion_t>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (rtlGetVersion && rtlGetVersion(&v) == 0)
        std::printf("os: %lu.%lu.%lu\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);

    wchar_t name[128] = L"?";
    USEROBJECTFLAGS flags{};
    HWINSTA ws = GetProcessWindowStation();
    GetUserObjectInformationW(ws, UOI_NAME, name, sizeof(name), nullptr);
    const bool haveFlags = GetUserObjectInformationW(ws, UOI_FLAGS, &flags, sizeof(flags), nullptr);
    std::printf("winstation: %ls visible=%s\n", name,
                haveFlags ? ((flags.dwFlags & WSF_VISIBLE) ? "yes" : "no") : "?");

    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    std::printf("session: %lu\n", session);

    BOOL composition = FALSE;
    const HRESULT hr = DwmIsCompositionEnabled(&composition);
    std::printf("dwm composition: %s (hr=0x%08lX)\n", composition ? "on" : "off",
                static_cast<unsigned long>(hr));
}

struct CaptureResult {
    BOOL printOk = FALSE;
    DWORD lastError = 0;
    std::vector<std::uint32_t> pixels;
};

CaptureResult Capture(HWND hwnd, UINT flags) {
    CaptureResult r;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kWidth;
    bi.bmiHeader.biHeight = -kHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!mem || !dib || !bits) {
        r.lastError = GetLastError();
        if (dib) DeleteObject(dib);
        if (mem) DeleteDC(mem);
        return r;
    }

    auto* px = static_cast<std::uint32_t*>(bits);
    const std::uint32_t sentinel = (GetRValue(kSentinel) << 16) |
                                   (GetGValue(kSentinel) << 8) | GetBValue(kSentinel);
    for (int i = 0; i < kWidth * kHeight; ++i) px[i] = sentinel;

    HGDIOBJ old = SelectObject(mem, dib);
    SetLastError(0);
    r.printOk = PrintWindow(hwnd, mem, flags);
    r.lastError = GetLastError();
    GdiFlush();
    r.pixels.assign(px, px + kWidth * kHeight);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    return r;
}

COLORREF PixelAt(const CaptureResult& c, int x, int y) {
    const std::uint32_t p = c.pixels[static_cast<size_t>(y) * kWidth + x];
    return RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
}

void PrintRgb(COLORREF c) {
    std::printf("%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
}

bool Evaluate(const char* label, const CaptureResult& c) {
    std::printf("\n[%s]\n", label);
    std::printf("  PrintWindow: %s lastError=%lu\n", c.printOk ? "TRUE" : "FALSE", c.lastError);
    if (c.pixels.empty()) {
        std::printf("  result: INFRASTRUCTURE_FAILURE (DIB allocation failed)\n");
        return false;
    }

    int untouched = 0;
    for (auto p : c.pixels)
        if ((p & 0x00FFFFFF) == ((GetRValue(kSentinel) << 16) |
                                 (GetGValue(kSentinel) << 8) | GetBValue(kSentinel)))
            ++untouched;
    std::printf("  capture: %dx%d untouched=%d/%d\n", kWidth, kHeight, untouched,
                kWidth * kHeight);
    std::printf("  control rect: (%ld,%ld)-(%ld,%ld) expected ", kControl.left, kControl.top,
                kControl.right, kControl.bottom);
    PrintRgb(kMagenta);
    std::printf("\n");

    bool ok = c.printOk != FALSE;
    for (const auto& s : kSamples) {
        const COLORREF got = PixelAt(c, s.x, s.y);
        const bool match = got == s.expected;
        ok &= match;
        std::printf("  %-17s (%3d,%3d) expected ", s.name, s.x, s.y);
        PrintRgb(s.expected);
        std::printf(" observed ");
        PrintRgb(got);
        std::printf(" %s\n", match ? "OK" : "MISMATCH");
    }
    std::printf("  result: %s\n", ok ? "VALID" : "INFRASTRUCTURE_FAILURE");
    return ok;
}

}  // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ReportEnvironment();

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"ColorFixProbeInfra";
    if (!RegisterClassExW(&wc)) {
        std::printf("setup: RegisterClassExW failed (%lu)\n", GetLastError());
        return 3;
    }

    // WS_POPUP without border: window rect == client rect, so capture\n    // coordinates map 1:1 to client coordinates.\n    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                                L"ColorFixProbe", WS_POPUP, 100, 100, kWidth, kHeight,
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        std::printf("setup: CreateWindowExW failed (%lu)\n", GetLastError());
        return 3;
    }

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    const DWORD until = GetTickCount() + 300;
    MSG msg;
    while (GetTickCount() < until) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
    const HRESULT flush = DwmFlush();

    RECT wr{}, cr{};
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    std::printf("dpi: %u\n", GetDpiForWindow(hwnd));
    std::printf("window: (%ld,%ld)-(%ld,%ld) client: %ldx%ld visible=%s dwmflush=0x%08lX\n",
                wr.left, wr.top, wr.right, wr.bottom, cr.right, cr.bottom,
                IsWindowVisible(hwnd) ? "yes" : "no", static_cast<unsigned long>(flush));
    if (cr.right != kWidth || cr.bottom != kHeight ||
        wr.right - wr.left != kWidth || wr.bottom - wr.top != kHeight) {
        std::printf("setup: window geometry does not match %dx%d\n", kWidth, kHeight);
        DestroyWindow(hwnd);
        return 3;
    }

    const bool okDefault = Evaluate("flags=0", Capture(hwnd, 0));
    const bool okFull = Evaluate("PW_RENDERFULLCONTENT", Capture(hwnd, PW_RENDERFULLCONTENT));

    std::printf("\nsummary: flags=0 %s, PW_RENDERFULLCONTENT %s\n",
                okDefault ? "VALID" : "INFRASTRUCTURE_FAILURE",
                okFull ? "VALID" : "INFRASTRUCTURE_FAILURE");
    DestroyWindow(hwnd);
    return (okDefault && okFull) ? 0 : 1;
}
