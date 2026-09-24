#pragma once

#include <windows.h>
#include <uxtheme.h>
#include <cstdio>
#include <cwchar>
#include <cstring>

#include "MinHook.h"
#include "colorfix_hooks.hpp"

namespace colorfix::probe::uxtheme_observer {

struct Stats {
    long open = 0;
    long openForDpi = 0;
    long draw = 0;
    long drawEx = 0;
    long color = 0;
    long dropped = 0;
};

struct Event {
    enum class Kind { Draw, DrawEx, Color } kind{};
    HTHEME theme = nullptr;
    int part = 0;
    int state = 0;
    int prop = 0;
    RECT rect{};
    int width = 0;
    int height = 0;
    char attribution[48]{};
};

struct ThemeMap {
    HTHEME theme = nullptr;
    wchar_t klass[48]{};
};

inline constexpr int kMaxThemes = 64;
inline constexpr int kMaxEvents = 512;
inline ThemeMap g_themes[kMaxThemes]{};
inline Event g_events[kMaxEvents]{};
inline volatile LONG g_themeCount = 0;
inline volatile LONG g_eventCount = 0;
inline volatile LONG g_open = 0;
inline volatile LONG g_openForDpi = 0;
inline volatile LONG g_draw = 0;
inline volatile LONG g_drawEx = 0;
inline volatile LONG g_color = 0;
inline volatile LONG g_dropped = 0;
inline HWND g_v = nullptr;
inline HWND g_l = nullptr;

// Increment 9a. While paused, the observer keeps the HTHEME -> class map but
// neither counts nor records events, so the historical report and the
// 512-event buffer are untouched. g_drawIntercept (null by default: fully
// passive) lets the causal experiment replace one DrawThemeBackground call.
inline volatile LONG g_paused = 0;
using DrawIntercept_t = bool (*)(const wchar_t* klass, HDC dc, int part, int state,
                                 const RECT* rect);
inline DrawIntercept_t g_drawIntercept = nullptr;
inline thread_local int t_drawThemeTextDepth = 0;
inline volatile LONG g_text = 0;
inline volatile LONG g_textEx = 0;

using OpenThemeData_t = HTHEME (WINAPI*)(HWND, LPCWSTR);
using OpenThemeDataForDpi_t = HTHEME (WINAPI*)(HWND, LPCWSTR, UINT);
using DrawThemeBackground_t = HRESULT (WINAPI*)(HTHEME, HDC, int, int, const RECT*, const RECT*);
using DrawThemeBackgroundEx_t = HRESULT (WINAPI*)(HTHEME, HDC, int, int, const RECT*, const DTBGOPTS*);
using GetThemeColor_t = HRESULT (WINAPI*)(HTHEME, int, int, int, COLORREF*);
using DrawThemeText_t = HRESULT (WINAPI*)(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, DWORD, const RECT*);
using DrawThemeTextEx_t = HRESULT (WINAPI*)(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, LPRECT, const DTTOPTS*);

inline OpenThemeData_t g_openOrig = nullptr;
inline OpenThemeDataForDpi_t g_openDpiOrig = nullptr;
inline DrawThemeBackground_t g_drawOrig = nullptr;
inline DrawThemeBackgroundEx_t g_drawExOrig = nullptr;
inline GetThemeColor_t g_colorOrig = nullptr;
inline DrawThemeText_t g_textOrig = nullptr;
inline DrawThemeTextEx_t g_textExOrig = nullptr;
inline void* g_targets[3]{};

inline void CopyClass(wchar_t* dst, size_t n, LPCWSTR src) noexcept {
    if (!dst || n == 0) return;
    dst[0] = L'\0';
    if (!src) return;
    wcsncpy_s(dst, n, src, _TRUNCATE);
}

inline void RecordTheme(HTHEME theme, LPCWSTR klass) noexcept {
    if (!theme) return;
    LONG slot = InterlockedIncrement(&g_themeCount) - 1;
    if (slot < 0 || slot >= kMaxThemes) {
        InterlockedIncrement(&g_dropped);
        return;
    }
    g_themes[slot].theme = theme;
    CopyClass(g_themes[slot].klass, _countof(g_themes[slot].klass), klass);
}

inline const wchar_t* ThemeClass(HTHEME theme) noexcept {
    const LONG n = g_themeCount < kMaxThemes ? g_themeCount : kMaxThemes;
    for (LONG i = n - 1; i >= 0; --i)
        if (g_themes[i].theme == theme) return g_themes[i].klass;
    return L"?";
}

inline bool Intersects(const RECT& a, const RECT& b) noexcept {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

inline void AttributionFor(HDC dc, const RECT* r, char* out, size_t n) noexcept {
    if (!out || n == 0) return;
    strcpy_s(out, n, "none");
    if (!r) return;

    HWND w = WindowFromDC(dc);
    if (w) {
        HWND root = nullptr;
        char rootTag = '?';
        for (HWND p = w; p; p = GetParent(p)) {
            if (p == g_v) { root = g_v; rootTag = 'V'; break; }
            if (p == g_l) { root = g_l; rootTag = 'L'; break; }
        }
        if (root) {
            RECT mapped = *r;
            POINT pts[2] = {{mapped.left, mapped.top}, {mapped.right, mapped.bottom}};
            MapWindowPoints(w, root, pts, 2);
            mapped = {pts[0].x, pts[0].y, pts[1].x, pts[1].y};
            constexpr RECT vStatic{18,18,92,82};
            constexpr RECT vEdit{118,18,192,82};
            constexpr RECT vButton{218,18,292,82};
            constexpr RECT lList{30,30,190,80};
            const char* name = nullptr;
            if (rootTag == 'V' && Intersects(mapped, vStatic)) name = "coord:V.static-default";
            if (rootTag == 'V' && Intersects(mapped, vEdit)) name = "coord:V.edit-default";
            if (rootTag == 'V' && Intersects(mapped, vButton)) name = "coord:V.button-face";
            if (rootTag == 'L' && Intersects(mapped, lList)) name = "coord:L.listview-bg";
            if (name) {
                strcpy_s(out, n, name);
                return;
            }
        }
    }

    const int width = r->right - r->left;
    const int height = r->bottom - r->top;
    if (width == 90 && height == 80) strcpy_s(out, n, "size:90x80");
    else if (width == 200 && height == 80) strcpy_s(out, n, "size:200x80");
}

inline void Record(Event::Kind kind, HTHEME theme, int part, int state, int prop,
                   HDC dc, const RECT* r) noexcept {
    LONG slot = InterlockedIncrement(&g_eventCount) - 1;
    if (slot < 0 || slot >= kMaxEvents) {
        InterlockedIncrement(&g_dropped);
        return;
    }
    Event& e = g_events[slot];
    e.kind = kind;
    e.theme = theme;
    e.part = part;
    e.state = state;
    e.prop = prop;
    if (r) {
        e.rect = *r;
        e.width = r->right - r->left;
        e.height = r->bottom - r->top;
        AttributionFor(dc, r, e.attribution, sizeof(e.attribution));
    } else {
        strcpy_s(e.attribution, sizeof(e.attribution), "none");
    }
}

inline void ProductThemeOpenTap(HTHEME h, LPCWSTR klass) {
    if (!g_paused) InterlockedIncrement(&g_open);
    RecordTheme(h, klass);
}

inline void ProductThemeTextTap(HTHEME, int, bool) {
    ++t_drawThemeTextDepth;
    --t_drawThemeTextDepth;
    InterlockedIncrement(&g_text);
}

inline HRESULT WINAPI DrawThemeBackground_Hook(HTHEME theme, HDC dc, int part, int state,
                                                const RECT* rect, const RECT* clip) {
    const DrawIntercept_t intercept = g_drawIntercept;
    if (intercept && intercept(ThemeClass(theme), dc, part, state, rect)) return S_OK;
    HRESULT hr = g_drawOrig(theme, dc, part, state, rect, clip);
    if (g_paused) return hr;
    InterlockedIncrement(&g_draw);
    Record(Event::Kind::Draw, theme, part, state, 0, dc, rect);
    return hr;
}

inline HRESULT WINAPI DrawThemeBackgroundEx_Hook(HTHEME theme, HDC dc, int part, int state,
                                                  const RECT* rect, const DTBGOPTS* opts) {
    HRESULT hr = g_drawExOrig(theme, dc, part, state, rect, opts);
    if (g_paused) return hr;
    InterlockedIncrement(&g_drawEx);
    Record(Event::Kind::DrawEx, theme, part, state, 0, dc, rect);
    return hr;
}

inline HRESULT WINAPI GetThemeColor_Hook(HTHEME theme, int part, int state, int prop,
                                         COLORREF* color) {
    HRESULT hr = g_colorOrig(theme, part, state, prop, color);
    if (g_paused) return hr;
    InterlockedIncrement(&g_color);
    Record(Event::Kind::Color, theme, part, state, prop, nullptr, nullptr);
    return hr;
}


inline bool CreateAndEnable(HMODULE ux, const char* name, void* hook, void** original,
                            void** targetOut) {
    void* target = reinterpret_cast<void*>(GetProcAddress(ux, name));
    if (!target) {
        std::printf("setup: UxTheme export not found: %s\n", name);
        return false;
    }
    MH_STATUS s = MH_CreateHook(target, hook, original);
    if (s != MH_OK) {
        std::printf("setup: UxTheme MH_CreateHook(%s) = %s\n", name, MH_StatusToString(s));
        return false;
    }
    s = MH_EnableHook(target);
    if (s != MH_OK) {
        std::printf("setup: UxTheme MH_EnableHook(%s) = %s\n", name, MH_StatusToString(s));
        return false;
    }
    *targetOut = target;
    return true;
}

inline bool Install(HWND v, HWND l) {
    g_v = v;
    g_l = l;
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) {
        std::printf("setup: LoadLibraryW(uxtheme.dll) failed (%lu)\n", GetLastError());
        return false;
    }
    colorfix::hooks::g_probeThemeOpenTap.store(&ProductThemeOpenTap);
    colorfix::hooks::g_probeThemeTextTap.store(&ProductThemeTextTap);
    return CreateAndEnable(ux, "DrawThemeBackground", reinterpret_cast<void*>(&DrawThemeBackground_Hook),
                           reinterpret_cast<void**>(&g_drawOrig), &g_targets[0]) &&
           CreateAndEnable(ux, "DrawThemeBackgroundEx", reinterpret_cast<void*>(&DrawThemeBackgroundEx_Hook),
                           reinterpret_cast<void**>(&g_drawExOrig), &g_targets[1]) &&
           CreateAndEnable(ux, "GetThemeColor", reinterpret_cast<void*>(&GetThemeColor_Hook),
                           reinterpret_cast<void**>(&g_colorOrig), &g_targets[2]);
}

inline Stats GetStats() noexcept {
    return {g_open, g_openForDpi, g_draw, g_drawEx, g_color, g_dropped};
}

inline bool Autotest() noexcept {
    return (g_open + g_openForDpi) > 0 && g_themeCount > 0 && g_dropped == 0;
}

inline bool SameReportedEvent(const Event& a, const Event& b) noexcept {
    return a.kind == b.kind && a.part == b.part && a.state == b.state &&
           a.prop == b.prop && a.width == b.width && a.height == b.height &&
           std::strcmp(a.attribution, b.attribution) == 0 &&
           std::wcscmp(ThemeClass(a.theme), ThemeClass(b.theme)) == 0;
}

inline void PrintReport() {
    const Stats s = GetStats();
    std::printf("uxtheme: observer open=%ld open-dpi=%ld draw=%ld draw-ex=%ld color=%ld dropped=%ld %s\n",
                s.open, s.openForDpi, s.draw, s.drawEx, s.color, s.dropped,
                Autotest() ? "PASS" : "INFRASTRUCTURE_FAILURE");

    // Keep the CI notice below the ~4 KB annotation limit while preserving
    // distinct evidence. The fixed buffer still contains every observed event.
    const LONG themes = g_themeCount < kMaxThemes ? g_themeCount : kMaxThemes;
    int printedThemes = 0;
    for (LONG i = 0; i < themes && printedThemes < 8; ++i) {
        bool duplicate = false;
        for (LONG j = 0; j < i; ++j)
            if (std::wcscmp(g_themes[i].klass, g_themes[j].klass) == 0) duplicate = true;
        if (duplicate) continue;
        std::printf("uxtheme: theme class=%ls\n", g_themes[i].klass);
        ++printedThemes;
    }

    const LONG n = g_eventCount < kMaxEvents ? g_eventCount : kMaxEvents;
    LONG printed[24]{};
    int printedCount = 0;
    int uniqueSkipped = 0;
    for (LONG i = 0; i < n; ++i) {
        const Event& e = g_events[i];
        bool duplicate = false;
        for (int j = 0; j < printedCount; ++j)
            if (SameReportedEvent(e, g_events[printed[j]])) duplicate = true;
        if (duplicate) continue;
        if (printedCount == 24) { ++uniqueSkipped; continue; }
        printed[printedCount++] = i;
        const wchar_t* klass = ThemeClass(e.theme);
        if (e.kind == Event::Kind::Color) {
            std::printf("uxtheme: color class=%ls part=%d state=%d prop=%d\n",
                        klass, e.part, e.state, e.prop);
        } else {
            std::printf("uxtheme: %s class=%ls part=%d state=%d rect=%dx%d attr=%s\n",
                        e.kind == Event::Kind::Draw ? "draw" : "draw-ex",
                        klass, e.part, e.state, e.width, e.height, e.attribution);
        }
    }
    std::printf("uxtheme: report unique=%d skipped=%d\n", printedCount, uniqueSkipped);
}

}  // namespace colorfix::probe::uxtheme_observer
