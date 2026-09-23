from pathlib import Path

edits = [
("tests/probe/probe_hooks.cpp", "#include <dwmapi.h>\n", "#include <dwmapi.h>\n#include <tlhelp32.h>\n"),
("tests/probe/probe_hooks.cpp", "#include <cstdio>\n", "#include <cstdio>\n#include <cstring>\n"),
("tests/probe/probe_hooks.cpp", "    const bool explicitPaint = tag != 'K' && tag != 'C';\n", "    const bool explicitPaint = tag != 'K' && tag != 'C' && tag != 'V' && tag != 'L';\n"),
("tests/probe/probe_hooks.cpp", "    return h;\n}\n\n}  // namespace\n", """    return h;
}

// ------------------------------------------------------ Common Controls v6

constexpr char kV6Manifest[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\\r\\n"
    "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\\r\\n"
    "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" "
    "name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" "
    "processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
    "</dependentAssembly></dependency>\\r\\n"
    "</assembly>\\r\\n";

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
    std::printf("comctl32: %-12s modules=%d v6=%d version=%u.%u\\n", label, s.modules,
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
"""),
("tests/probe/probe_hooks.cpp", "int main() {\n", """int main(int argc, char** argv) {
    bool late = false;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--order=late") == 0) {
            late = true;
        } else if (std::strcmp(argv[a], "--order=early") != 0) {
            std::printf("setup: unknown argument %s\\n", argv[a]);
            return 3;
        }
    }
"""),
("tests/probe/probe_hooks.cpp", "increment 4 - hooks + WM_CTLCOLOR + dynamic policy, no Common Controls", "increment 5 - hooks + WM_CTLCOLOR + policy + Common Controls v6"),
("tests/probe/probe_hooks.cpp", "    std::printf(\"threshold: luma<%u = DARK\\n\", kDarkThreshold);\n", "    std::printf(\"threshold: luma<%u = DARK\\n\", kDarkThreshold);\n    std::printf(\"order: %s\\n\", late ? \"late\" : \"early\");\n"),
("tests/probe/probe_hooks.cpp", "    HWND winC = MakeWindow(wc.lpszClassName, 'C', 100, 450);\n", """    HWND winC = MakeWindow(wc.lpszClassName, 'C', 100, 450);
    HWND winV = MakeWindow(wc.lpszClassName, 'V', 350, 450);
    HWND winL = MakeWindow(wc.lpszClassName, 'L', 560, 100);
    HANDLE v6ctx = CreateV6ActCtx();
    if (!winV || !winL || v6ctx == INVALID_HANDLE_VALUE) {
        std::printf("setup: V/L windows or v6 activation context failed (%lu)\\n", GetLastError());
        return 3;
    }
    PrintComctl("at-start", QueryComctl32());
"""),
("tests/probe/probe_hooks.cpp", "    const WindowShot baseC = Shoot(winC);\n", """    const WindowShot baseC = Shoot(winC);

    // Order "late": comctl32 v6 is loaded, its controls created and painted,
    // all before any hook is enabled.
    WindowShot baseV, baseL;
    bool v6Ok = true;
    if (late) {
        v6Ok = CreateV6Children(v6ctx, winV, winL);
        Pump(200);
        baseV = Shoot(winV);
        baseL = Shoot(winL);
    }
    const ComctlState ccBeforeHooks = QueryComctl32();
"""),
("tests/probe/probe_hooks.cpp", "    if (st != MH_OK) { std::printf(\"setup: MH_EnableHook = %s\\n\", MH_StatusToString(st)); return 1; }\n", """    if (st != MH_OK) { std::printf("setup: MH_EnableHook = %s\\n", MH_StatusToString(st)); return 1; }

    // Order "early": hooks are live before comctl32 v6 is first loaded.
    if (!late) {
        v6Ok = CreateV6Children(v6ctx, winV, winL);
        Pump(200);
    }
    const ComctlState ccAfterLoad = QueryComctl32();
"""),
("tests/probe/probe_hooks.cpp", "    const WindowShot hookC = Shoot(winC);\n", """    const WindowShot hookC = Shoot(winC);
    const WindowShot hookV = Shoot(winV);
    const WindowShot hookL = Shoot(winL);
    WindowShot afterV, afterL;
    if (late) {
        SendSysColorChange(winV);
        SendSysColorChange(winL);
        Pump(150);
        afterV = Shoot(winV);
        afterL = Shoot(winL);
    }
"""),
("tests/probe/probe_hooks.cpp", "    const int code = !infraOk ? 1 : !behaviorOk ? 2 : 0;\n", """    // ------------------------------------------------ Common Controls v6
    std::printf("\\n[common controls v6] order=%s\\n", late ? "late" : "early");
    PrintComctl("before-hooks", ccBeforeHooks);
    PrintComctl("after-load", ccAfterLoad);
    // Invariants that make the order meaningful. Violating them invalidates
    // the measurement, not ColorFix.
    const bool orderOk =
        v6Ok && ccAfterLoad.v6 && (late ? ccBeforeHooks.v6 : !ccBeforeHooks.v6);
    if (!orderOk) infraOk = false;
    std::printf("v6: order-invariant children=%d v6-before-hooks=%d v6-after-load=%d %s\\n",
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
    std::printf("v6: magenta-ctl %s\\n", v6Magenta ? "VALID" : "INFRASTRUCTURE_FAILURE");

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
        std::printf("\\n");
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
        std::printf("\\n");
    }

    const int code = !infraOk ? 1 : !behaviorOk ? 2 : 0;
"""),
("tests/probe/probe_hooks.cpp", "    DestroyWindow(winC);\n", "    DestroyWindow(winC);\n    DestroyWindow(winV);\n    DestroyWindow(winL);\n    ReleaseActCtx(v6ctx);\n"),
(".github/workflows/ci.yml", """        run: |
          $out = & "build\\${{ matrix.preset }}\\colorfix_probe_hooks.exe" 2>&1
          $code = $LASTEXITCODE
          $out | Write-Output
          Add-Content $env:GITHUB_STEP_SUMMARY "### probe-hooks ${{ matrix.preset }}"
          Add-Content $env:GITHUB_STEP_SUMMARY '```text'
          $out | Add-Content $env:GITHUB_STEP_SUMMARY
          Add-Content $env:GITHUB_STEP_SUMMARY '```'
          # Annotations are truncated at about 4 KB: publish the verdict first,
          # then autotests and surfaces as separate notices.
          $groups = [ordered]@{
            'result'    = '^(arch|summary|policy):'
            'autotests' = '^(threshold|expect|autotest|detail):'
            'surfaces'  = '^(surface|scenario):'
          }
          foreach ($g in $groups.GetEnumerator()) {
            $lines = $out | Select-String $g.Value | ForEach-Object { $_.Line }
            Write-Output "::notice title=probe-hooks ${{ matrix.preset }} $($g.Key)::$(($lines -join '%0A'))"
          }
          exit $code
""", """        run: |
          # Two processes: comctl32 v6 cannot be unloaded, so each order needs
          # its own. 2 orders x 4 groups = 8 notices (GitHub allows 10 per step).
          $final = 0
          foreach ($order in @('early', 'late')) {
            $out = & "build\\${{ matrix.preset }}\\colorfix_probe_hooks.exe" "--order=$order" 2>&1
            $code = $LASTEXITCODE
            if ($code -ne 0 -and $final -eq 0) { $final = $code }
            $out | Write-Output
            Add-Content $env:GITHUB_STEP_SUMMARY "### probe-hooks ${{ matrix.preset }} $order"
            Add-Content $env:GITHUB_STEP_SUMMARY '```text'
            $out | Add-Content $env:GITHUB_STEP_SUMMARY
            Add-Content $env:GITHUB_STEP_SUMMARY '```'
            # Annotations are truncated at about 4 KB: verdict first, then groups.
            $groups = [ordered]@{
              'result'    = '^(arch|order|summary|policy):'
              'autotests' = '^(threshold|expect|autotest|detail):'
              'surfaces'  = '^(surface|scenario):'
              'v6'        = '^(comctl32|v6|v6scenario):'
            }
            foreach ($g in $groups.GetEnumerator()) {
              $lines = $out | Select-String $g.Value | ForEach-Object { $_.Line }
              Write-Output "::notice title=probe-hooks ${{ matrix.preset }} $order $($g.Key)::$(($lines -join '%0A'))"
            }
          }
          exit $final
"""),
("docs/design-premises.md", "into the existing mechanism-coverage verdicts.\n", """into the existing mechanism-coverage verdicts.

### Open debt: policy is not wired to real signals

The policy (modes, High Contrast veto, single atomic effective state) is
implemented and verified dynamically by probe increment 4, but nothing calls
`ReadWindowsSignals`/`Publish` yet and no `WM_SETTINGCHANGE` handler exists.
At runtime ColorFix therefore still behaves as a fixed `ForceDark`. Wiring the
policy to real signals is a prerequisite for any use of ColorFix outside the
test environment.
""")
]

for file, find, replace in edits:
    p = Path(file)
    text = p.read_text(encoding='utf-8')
    count = text.count(find)
    if count != 1:
        raise SystemExit(f'{file}: expected exactly one match, got {count}: {find[:80]!r}')
    p.write_text(text.replace(find, replace, 1), encoding='utf-8', newline='')

print('increment 5 patch applied')
