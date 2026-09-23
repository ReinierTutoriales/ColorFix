from pathlib import Path

EDITS = [
    (r"""    const bool explicitPaint = tag != 'K' && tag != 'C' && tag != 'V' && tag != 'L';
""", r"""    const bool explicitPaint =
        tag != 'K' && tag != 'C' && tag != 'V' && tag != 'L' && tag != 'T';
"""),
    (r"""    if (SurfaceColor(c, r, &uniform) && uniform == expected) return "COVERED";
    return Luma(*center) >= kDarkThreshold ? "MISS" : "INCONCLUSIVE";
}
""", r"""    if (SurfaceColor(c, r, &uniform) && uniform == expected) return "COVERED";
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
"""),
    (r"""ColorFixProbe increment 6 - passive UxTheme observation""", r"""ColorFixProbe increment 7 - UxTheme observation + SetWindowTheme opt-out"""),
    (r"""    HWND winL = MakeWindow(wc.lpszClassName, 'L', 560, 100);
""", r"""    HWND winL = MakeWindow(wc.lpszClassName, 'L', 560, 100);
    HWND winT = MakeWindow(wc.lpszClassName, 'T', 560, 450);
    if (!winT) {
        std::printf("setup: T window failed (%lu)\n", GetLastError());
        return 3;
    }
"""),
    (r"""        v6Ok = CreateV6Children(v6ctx, winV, winL);
        Pump(200);
        baseV = Shoot(winV);
""", r"""        v6Ok = CreateV6Children(v6ctx, winV, winL) && CreateTChildren(v6ctx, winT);
        Pump(200);
        baseV = Shoot(winV);
"""),
    (r"""        v6Ok = CreateV6Children(v6ctx, winV, winL);
        Pump(200);
    }
    const ComctlState ccAfterLoad""", r"""        v6Ok = CreateV6Children(v6ctx, winV, winL) && CreateTChildren(v6ctx, winT);
        Pump(200);
    }
    const ComctlState ccAfterLoad"""),
    (r"""    const WindowShot hookL = Shoot(winL);
""", r"""    const WindowShot hookL = Shoot(winL);

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
"""),
    (r"""    std::printf("\n[uxtheme observer]\n");
""", r"""    // ------------------------------------------- increment 7: theme opt-out
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
"""),
    (r"""    DestroyWindow(winL);
""", r"""    DestroyWindow(winL);
    DestroyWindow(winT);
"""),
]

path = Path('tests/probe/probe_hooks.cpp')
text = path.read_text(encoding='utf-8')
for i, (old, new) in enumerate(EDITS, 1):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'edit {i}: expected exactly one match, found {count}; old={old!r}')
    text = text.replace(old, new, 1)
path.write_text(text, encoding='utf-8')
print(f'applied {len(EDITS)} edits to {path}')
