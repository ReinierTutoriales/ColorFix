from pathlib import Path
import hashlib
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path, old, new):
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def blob_sha(path):
    data = (ROOT / path).read_bytes()
    return hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()

replace_once(
    "core/colorfix_policy.hpp",
    "// ForceDark preserves the current probe/product behavior until configuration\n// explicitly selects another policy. Hooks only pay one relaxed atomic load.\ninline std::atomic<bool> g_effectiveDark{true};\n",
    "// Fail-safe default: inactive (pass-through) until a host adapter publishes an\n// explicit mode with real signals. Hooks only pay one relaxed atomic load.\ninline std::atomic<bool> g_effectiveDark{false};\n",
)

replace_once(
    "tests/probe/probe_hooks.cpp",
    '#include "colorfix_hooks.hpp"\n',
    '#include "colorfix_hooks.hpp"\n#include "colorfix_runtime.hpp"\n',
)

replace_once(
    "tests/probe/probe_hooks.cpp",
    "    // Phase A: initialize MinHook once.",
    "    // Runtime policy: explicit ForceDark with real Windows signals keeps\n"
    "    // increments 1-7 comparable. A real signal read failure invalidates the run.\n"
    "    const colorfix::runtime::RefreshResult initial =\n"
    "        colorfix::runtime::RefreshPolicy(colorfix::policy::Mode::ForceDark);\n"
    "    std::printf(\"runtime: initial mode=ForceDark signals=%s hc=%d light=%d active=%d\\n\",\n"
    "                initial.signalsOk ? \"OK\" : \"FAILED\", initial.signals.highContrast ? 1 : 0,\n"
    "                initial.signals.appsUseLightTheme ? 1 : 0, initial.after ? 1 : 0);\n"
    "    if (!initial.signalsOk) {\n"
    "        std::printf(\"setup: ReadWindowsSignals failed\\n\");\n"
    "        return 1;\n"
    "    }\n\n"
    "    // Phase A: initialize MinHook once.",
)

replace_once(
    "tests/probe/probe_hooks.cpp",
    "    uxo::PrintReport();\n\n    const int code = ",
    "    uxo::PrintReport();\n\n"
    "    // ------------------------------------------------ Phase E: runtime wiring\n"
    "    // Product listener (hooks/colorfix_runtime.hpp) in FollowSystem, driven by\n"
    "    // the real signal: HKCU AppsUseLightTheme plus a WM_SETTINGCHANGE\n"
    "    // \"ImmersiveColorSet\" broadcast. Reception, policy transition and visual\n"
    "    // result are separate verdicts; the ListView cache refresh is a finding.\n"
    "    std::printf(\"\\n[runtime wiring] FollowSystem listener, real signals\\n\");\n"
    "    namespace cfr = colorfix::runtime;\n"
    "    const wchar_t* const kPersonalize =\n"
    "        L\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Themes\\\\Personalize\";\n"
    "    DWORD origLight = 1;\n"
    "    DWORD origSize = sizeof(origLight);\n"
    "    const LSTATUS origStatus = RegGetValueW(HKEY_CURRENT_USER, kPersonalize, L\"AppsUseLightTheme\",\n"
    "                                            RRF_RT_REG_DWORD, nullptr, &origLight, &origSize);\n"
    "    const bool listenerOk = cfr::StartListener(cfp::Mode::FollowSystem);\n"
    "    if (!listenerOk) infraOk = false;\n"
    "    std::printf(\"runtime: listener %s\\n\", listenerOk ? \"STARTED\" : \"INFRASTRUCTURE_FAILURE\");\n\n"
    "    struct RuntimeStep { const char* name; DWORD light; bool expectOn; };\n"
    "    const RuntimeStep runtimeSteps[] = {\n"
    "        {\"to-light\",   1, false},\n"
    "        {\"to-dark\",    0, true},\n"
    "        {\"back-light\", 1, false},\n"
    "    };\n"
    "    auto broadcastThemeChange = [] {\n"
    "        DWORD_PTR ignored = 0;\n"
    "        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,\n"
    "                            reinterpret_cast<LPARAM>(L\"ImmersiveColorSet\"), SMTO_ABORTIFHUNG,\n"
    "                            2000, &ignored);\n"
    "    };\n"
    "    for (const auto& rs : runtimeSteps) {\n"
    "        if (!listenerOk) break;\n"
    "        const long seen0 = cfr::g_signalsSeen.load();\n"
    "        const long trans0 = cfr::g_transitions.load();\n"
    "        const long fail0 = cfr::g_signalFailures.load();\n"
    "        const bool before = cfp::Active();\n"
    "        const LSTATUS written = RegSetKeyValueW(HKEY_CURRENT_USER, kPersonalize,\n"
    "                                                L\"AppsUseLightTheme\", REG_DWORD, &rs.light,\n"
    "                                                sizeof(rs.light));\n"
    "        broadcastThemeChange();\n"
    "        // Wait for the listener thread, then let the posted WM_SYSCOLORCHANGE\n"
    "        // and the scheduled repaints run on this thread before capturing.\n"
    "        for (int t = 0; t < 60 && cfr::g_signalsSeen.load() == seen0; ++t) Pump(50);\n"
    "        Pump(200);\n"
    "        const long seen = cfr::g_signalsSeen.load() - seen0;\n"
    "        const long trans = cfr::g_transitions.load() - trans0;\n"
    "        const long fails = cfr::g_signalFailures.load() - fail0;\n"
    "        const bool after = cfp::Active();\n\n"
    "        const WindowShot now[3] = {Shoot(winE), Shoot(winK), Shoot(winC)};\n"
    "        const WindowShot* base[3] = {&baseE, &baseK, &baseC};\n"
    "        const WindowShot* hooked[3] = {&hookE, &hookK, &hookC};\n"
    "        int match = 0, total = 0;\n"
    "        for (const auto& s : kSurfaces) {\n"
    "            const int wi = s.window == 'E' ? 0 : s.window == 'K' ? 1 : 2;\n"
    "            const WindowShot& ref = rs.expectOn ? *hooked[wi] : *base[wi];\n"
    "            for (int m = 0; m < 2; ++m) {\n"
    "                COLORREF want = CLR_INVALID, got = CLR_INVALID;\n"
    "                ++total;\n"
    "                if (!ref.mode[m].px.empty() && !now[wi].mode[m].px.empty() &&\n"
    "                    SurfaceColor(ref.mode[m], s.rect, &want) &&\n"
    "                    SurfaceColor(now[wi].mode[m], s.rect, &got) && want == got)\n"
    "                    ++match;\n"
    "            }\n"
    "        }\n"
    "        const WindowShot nowL = Shoot(winL);\n"
    "        COLORREF lc = CLR_INVALID;\n"
    "        ClassifyV6(nowL.mode[0], kV6Surfaces[3].rect, expWindow, &lc);\n"
    "        const bool listFollows = lc == (rs.expectOn ? expWindow : baseSysWindow);\n\n"
    "        const bool received = written == ERROR_SUCCESS && seen > 0;\n"
    "        const bool semanticOk =\n"
    "            fails == 0 && after == rs.expectOn && trans == (before != rs.expectOn ? 1 : 0);\n"
    "        const bool visualOk = match == total;\n"
    "        if (!received) infraOk = false;\n"
    "        else if (!semanticOk || !visualOk) behaviorOk = false;\n"
    "        std::printf(\"runtime: %-10s light=%lu received=%ld transitions=%ld signal-failures=%ld \"\n"
    "                    \"active=%d->%d surfaces=%d/%d listview=\",\n"
    "                    rs.name, static_cast<unsigned long>(rs.light), seen, trans, fails,\n"
    "                    before ? 1 : 0, after ? 1 : 0, match, total);\n"
    "        PrintRgb(lc);\n"
    "        std::printf(\" %s %s\\n\", listFollows ? \"FOLLOWS\" : \"STALE\",\n"
    "                    !received                  ? \"INFRASTRUCTURE_FAILURE\"\n"
    "                    : (semanticOk && visualOk) ? \"PASS\"\n"
    "                                               : \"FAIL\");\n"
    "    }\n"
    "    cfr::StopListener();\n\n"
    "    // Restore the runner's original setting whatever happened above.\n"
    "    const LSTATUS restored =\n"
    "        origStatus == ERROR_SUCCESS\n"
    "            ? RegSetKeyValueW(HKEY_CURRENT_USER, kPersonalize, L\"AppsUseLightTheme\", REG_DWORD,\n"
    "                              &origLight, sizeof(origLight))\n"
    "            : RegDeleteKeyValueW(HKEY_CURRENT_USER, kPersonalize, L\"AppsUseLightTheme\");\n"
    "    broadcastThemeChange();\n"
    "    if (restored != ERROR_SUCCESS) infraOk = false;\n"
    "    std::printf(\"runtime: restore AppsUseLightTheme=%s %s\\n\",\n"
    "                origStatus == ERROR_SUCCESS ? (origLight ? \"1\" : \"0\") : \"absent\",\n"
    "                restored == ERROR_SUCCESS ? \"OK\" : \"INFRASTRUCTURE_FAILURE\");\n\n"
    "    const int code = ",
)

replace_once(
    "tests/probe/probe_hooks.cpp",
    "ColorFixProbe increment 7 - UxTheme observation + SetWindowTheme opt-out",
    "ColorFixProbe increment 8 - runtime policy wiring",
)

replace_once(
    "CMakeLists.txt",
    "target_link_libraries(colorfix_probe_hooks PRIVATE minhook_detours user32 gdi32 dwmapi)",
    "target_link_libraries(colorfix_probe_hooks PRIVATE minhook_detours user32 gdi32 dwmapi advapi32)",
)

replace_once(
    ".github/workflows/ci.yml",
    "              'result'    = '^(arch|order|summary|policy):'\n",
    "              'result'    = '^(arch|order|summary|policy|runtime):'\n",
)

replace_once(
    "windhawk/generate_mod.py",
    'HOOKS = ROOT / "hooks" / "colorfix_hooks.hpp"\n',
    'HOOKS = ROOT / "hooks" / "colorfix_hooks.hpp"\nRUNTIME = ROOT / "hooks" / "colorfix_runtime.hpp"\n',
)

replace_once(
    "windhawk/generate_mod.py",
    '    hooks_body = ["// ---- hooks/colorfix_hooks.hpp ----"]\n    hooks_body.extend(flatten(HOOKS, sys_includes))\n',
    '    hooks_body = ["// ---- hooks/colorfix_hooks.hpp ----"]\n    hooks_body.extend(flatten(HOOKS, sys_includes))\n    hooks_body.append("// ---- hooks/colorfix_runtime.hpp ----")\n    hooks_body.extend(flatten(RUNTIME, sys_includes))\n',
)

replace_once(
    "windhawk/generate_mod.py",
    'f"core headers: {\', \'.join(order)}, hooks: {HOOKS.relative_to(ROOT)}"',
    'f"core headers: {\', \'.join(order)}, hooks: {HOOKS.relative_to(ROOT)}, "\n        f"{RUNTIME.relative_to(ROOT)}"',
)

replace_once(
    "tests/windhawk/windhawk_api_stub.h",
    "inline BOOL Wh_ApplyHookOperations() { return TRUE; }\n",
    "inline BOOL Wh_ApplyHookOperations() { return TRUE; }\n"
    "inline PCWSTR Wh_GetStringSetting(PCWSTR, ...) { return L\"followsystem\"; }\n"
    "inline void Wh_FreeStringSetting(PCWSTR) {}\n",
)

replace_once(
    "windhawk/colorfix.wh.template.cpp",
    "// @architecture    arm64\n// ==/WindhawkMod==\n",
    "// @architecture    arm64\n// @compilerOptions -ladvapi32\n// ==/WindhawkMod==\n\n"
    "// ==WindhawkModSettings==\n"
    "/*\n"
    "- mode: followsystem\n"
    "  $name: Mode\n"
    "  $description: When ColorFix applies dark colors. High Contrast always disables it.\n"
    "  $options:\n"
    "  - followsystem: Follow the Windows app theme\n"
    "  - forcedark: Always dark\n"
    "  - disabled: Disabled\n"
    "*/\n"
    "// ==/WindhawkModSettings==\n",
)

replace_once(
    "windhawk/colorfix.wh.template.cpp",
    "BOOL Wh_ModInit() {\n"
    "    // Hooks queued during Wh_ModInit are applied by Windhawk after it returns.\n"
    "    // Wh_ApplyHookOperations is only for hooks queued after initialization.\n"
    "    return colorfix::hooks::RegisterPhase1Hooks(WindhawkRegisterHook) ? TRUE : FALSE;\n"
    "}\n\n"
    "void Wh_ModUninit() {\n"
    "    colorfix::hooks::ShutdownPhase1Hooks();\n"
    "}",
    "// An unrecognized setting value is treated as Disabled: fail safe, never an\n"
    "// invented preference.\n"
    "static colorfix::policy::Mode ReadModeSetting() {\n"
    "    using colorfix::policy::Mode;\n"
    "    Mode mode = Mode::Disabled;\n"
    "    if (PCWSTR value = Wh_GetStringSetting(L\"mode\")) {\n"
    "        if (lstrcmpiW(value, L\"followsystem\") == 0) mode = Mode::FollowSystem;\n"
    "        else if (lstrcmpiW(value, L\"forcedark\") == 0) mode = Mode::ForceDark;\n"
    "        else if (lstrcmpiW(value, L\"disabled\") != 0)\n"
    "            Wh_Log(L\"ColorFix: unknown mode setting, using disabled\");\n"
    "        Wh_FreeStringSetting(value);\n"
    "    }\n"
    "    return mode;\n"
    "}\n\n"
    "BOOL Wh_ModInit() {\n"
    "    const colorfix::policy::Mode mode = ReadModeSetting();\n"
    "    // Publish before any hook is applied: nothing turns dark unless the\n"
    "    // explicit mode and the real Windows signals say so.\n"
    "    if (!colorfix::runtime::RefreshPolicy(mode).signalsOk)\n"
    "        Wh_Log(L\"ColorFix: reading Windows theme signals failed; staying inactive\");\n"
    "    if (!colorfix::runtime::StartListener(mode))\n"
    "        Wh_Log(L\"ColorFix: policy listener failed; theme changes need a restart\");\n"
    "    // Hooks queued during Wh_ModInit are applied by Windhawk after it returns.\n"
    "    // Wh_ApplyHookOperations is only for hooks queued after initialization.\n"
    "    return colorfix::hooks::RegisterPhase1Hooks(WindhawkRegisterHook) ? TRUE : FALSE;\n"
    "}\n\n"
    "void Wh_ModSettingsChanged() {\n"
    "    colorfix::runtime::SetMode(ReadModeSetting());\n"
    "}\n\n"
    "void Wh_ModUninit() {\n"
    "    colorfix::runtime::StopListener();\n"
    "    colorfix::hooks::ShutdownPhase1Hooks();\n"
    "}",
)

runtime = r'''#pragma once

#include <windows.h>
#include <atomic>

#include "colorfix_policy.hpp"

// Runtime policy wiring: reads the real Windows signals, publishes the
// effective state and propagates ON<->OFF transitions to the process windows.
// Nothing here runs in the rendering hot path; hooks only read Active().
namespace colorfix::runtime {

// Mode chosen explicitly by the host adapter (Windhawk setting, probe).
inline std::atomic<unsigned char> g_mode{static_cast<unsigned char>(policy::Mode::Disabled)};

// Telemetry for probes and logs.
inline std::atomic<long> g_signalsSeen{0};     // policy-relevant messages received
inline std::atomic<long> g_transitions{0};     // effective ON<->OFF changes
inline std::atomic<long> g_signalFailures{0};  // ReadWindowsSignals failures

inline std::atomic<HWND> g_listenerWnd{nullptr};
inline HANDLE g_listenerThread = nullptr;
inline constexpr wchar_t kListenerClass[] = L"ColorFixPolicyListener";

// Messages that can change the effective policy: the app theme
// (WM_SETTINGCHANGE "ImmersiveColorSet"), High Contrast
// (WM_SETTINGCHANGE SPI_SETHIGHCONTRAST) and system color changes.
inline bool IsPolicySignal(UINT msg, WPARAM wp, LPARAM lp) noexcept {
    if (msg == WM_SYSCOLORCHANGE) return true;
    if (msg != WM_SETTINGCHANGE) return false;
    if (wp == SPI_SETHIGHCONTRAST) return true;
    const auto* area = reinterpret_cast<const wchar_t*>(lp);
    return area != nullptr && lstrcmpiW(area, L"ImmersiveColorSet") == 0;
}

struct RefreshResult {
    bool signalsOk = false;  // ReadWindowsSignals succeeded
    bool before = false;     // effective state before the refresh
    bool after = false;      // effective state after the refresh
    policy::Signals signals{};
};

// Reads the real signals and publishes. A signal failure publishes OFF: it
// neither keeps a previous dark state nor invents signals.
inline RefreshResult RefreshPolicy(policy::Mode mode) noexcept {
    RefreshResult r;
    r.before = policy::Active();
    r.signalsOk = policy::ReadWindowsSignals(&r.signals);
    if (r.signalsOk) {
        policy::Publish(mode, r.signals);
    } else {
        policy::g_effectiveDark.store(false, std::memory_order_relaxed);
        g_signalFailures.fetch_add(1, std::memory_order_relaxed);
    }
    r.after = policy::Active();
    return r;
}

struct EnumContext {
    DWORD pid;
    HWND skip;
};

inline BOOL CALLBACK PostSysColorChangeToChild(HWND child, LPARAM) {
    PostMessageW(child, WM_SYSCOLORCHANGE, 0, 0);
    return TRUE;
}

// Posted, never sent: the caller may be the listener thread, and a blocking
// cross-thread SendMessage could deadlock against a busy UI thread. Posted
// WM_SYSCOLORCHANGE is processed before the repaint RedrawWindow schedules.
inline BOOL CALLBACK PropagateToTopLevel(HWND hwnd, LPARAM lp) {
    const auto* ctx = reinterpret_cast<const EnumContext*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid || hwnd == ctx->skip) return TRUE;
    PostMessageW(hwnd, WM_SYSCOLORCHANGE, 0, 0);
    EnumChildWindows(hwnd, PostSysColorChangeToChild, 0);
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    return TRUE;
}

inline void PropagateColorChange(HWND skip) {
    EnumContext ctx{GetCurrentProcessId(), skip};
    EnumWindows(PropagateToTopLevel, reinterpret_cast<LPARAM>(&ctx));
}

// Refresh, and only on an effective transition: the atomic is already
// updated by RefreshPolicy, then windows are invalidated.
inline RefreshResult ApplyRefresh(policy::Mode mode, HWND skip) {
    const RefreshResult r = RefreshPolicy(mode);
    if (r.before != r.after) {
        g_transitions.fetch_add(1, std::memory_order_relaxed);
        PropagateColorChange(skip);
    }
    return r;
}

inline LRESULT CALLBACK ListenerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (IsPolicySignal(msg, wp, lp)) {
        g_signalsSeen.fetch_add(1, std::memory_order_relaxed);
        ApplyRefresh(static_cast<policy::Mode>(g_mode.load(std::memory_order_relaxed)), hwnd);
    }
    if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

inline HINSTANCE ListenerModule() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ListenerProc), &module);
    return module;
}

// Dedicated thread with a hidden top-level window. Not HWND_MESSAGE:
// message-only windows do not receive broadcasts such as WM_SETTINGCHANGE.
inline DWORD WINAPI ListenerThread(LPVOID ready) {
    const HINSTANCE inst = ListenerModule();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ListenerProc;
    wc.hInstance = inst;
    wc.lpszClassName = kListenerClass;
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kListenerClass, L"",
                                WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    g_listenerWnd.store(hwnd);
    SetEvent(static_cast<HANDLE>(ready));
    if (!hwnd) return 1;
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    UnregisterClassW(kListenerClass, inst);
    return 0;
}

inline bool StartListener(policy::Mode mode) {
    g_mode.store(static_cast<unsigned char>(mode), std::memory_order_relaxed);
    if (g_listenerThread) return g_listenerWnd.load() != nullptr;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) return false;
    g_listenerThread = CreateThread(nullptr, 0, ListenerThread, ready, 0, nullptr);
    if (g_listenerThread) WaitForSingleObject(ready, 5000);
    CloseHandle(ready);
    return g_listenerWnd.load() != nullptr;
}

inline void StopListener() {
    if (HWND hwnd = g_listenerWnd.exchange(nullptr)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    if (g_listenerThread) {
        WaitForSingleObject(g_listenerThread, 5000);
        CloseHandle(g_listenerThread);
        g_listenerThread = nullptr;
    }
}

// Mode change from the host (e.g. Windhawk settings): store, then refresh.
inline void SetMode(policy::Mode mode) {
    g_mode.store(static_cast<unsigned char>(mode), std::memory_order_relaxed);
    ApplyRefresh(mode, g_listenerWnd.load());
}

} // namespace colorfix::runtime
'''
new_path = ROOT / "hooks/colorfix_runtime.hpp"
if new_path.exists():
    raise SystemExit("hooks/colorfix_runtime.hpp already exists")
new_path.write_text(runtime, encoding="utf-8", newline="\n")

expected = {
    "core/colorfix_policy.hpp": "1d0b5d09451bbf8d1eb6d6efd4fad6bf2e0a4fe1",
    "hooks/colorfix_runtime.hpp": "273db541bb274050e7f3a3cc46001e2270d8cba8",
    "tests/probe/probe_hooks.cpp": "e6d4a072a77dcadec68380ff830b39f8580c1236",
    "windhawk/colorfix.wh.template.cpp": "2d9310dde5e304065b28c33156073483dee6374d",
    "windhawk/generate_mod.py": "6333a5e4a26ef608cdc915700305ac88164c11a2",
    "tests/windhawk/windhawk_api_stub.h": "6074b6e754db6d3659b2e3c2c4db2df4f8aa1909",
    ".github/workflows/ci.yml": "82c0b1c59389d85efc139614358e700476afd351",
    "CMakeLists.txt": "a87a3b583a366f13efae77351b5f5617bef5f006",
}
for path, want in expected.items():
    got = blob_sha(path)
    print(f"{path}: {got}")
    if got != want:
        raise SystemExit(f"blob mismatch for {path}: expected {want}, got {got}")

subprocess.run(["git", "add", *expected.keys()], cwd=ROOT, check=True)
subprocess.run(["git", "commit", "-m", "test(policy): wire runtime signals (increment 8)"], cwd=ROOT, check=True)
