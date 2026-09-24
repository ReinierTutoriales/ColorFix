#pragma once

#include <windows.h>
#include <atomic>

#include "colorfix_policy.hpp"

// Runtime policy wiring: reads the real Windows signals, publishes the
// effective state and propagates ON<->OFF transitions to the process windows.
// Nothing here runs in the rendering hot path; hooks only read Active().
namespace colorfix::runtime {

// Host configuration: mode and palette are independent dimensions, stored in
// one atomic so the listener never refreshes with the mode of one
// configuration and the palette of another.
struct Configuration {
    policy::Mode mode = policy::Mode::Disabled;
    Palette palette = Palette::Default;
};

constexpr unsigned char EncodeConfiguration(Configuration c) noexcept {
    return static_cast<unsigned char>(static_cast<unsigned char>(c.mode) |
                                      (c.palette == Palette::Amoled ? 0x10u : 0u));
}

constexpr Configuration DecodeConfiguration(unsigned char raw) noexcept {
    return Configuration{static_cast<policy::Mode>(raw & 0x0Fu),
                         (raw & 0x10u) != 0 ? Palette::Amoled : Palette::Default};
}

inline std::atomic<unsigned char> g_config{EncodeConfiguration(Configuration{})};

// Telemetry for probes and logs.
inline std::atomic<long> g_signalsSeen{0};     // policy-relevant messages received
inline std::atomic<long> g_transitions{0};     // effective ON<->OFF changes
inline std::atomic<long> g_paletteRepaints{0}; // palette changes while active
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
    Palette paletteBefore = Palette::Default;
    Palette paletteAfter = Palette::Default;
    policy::Signals signals{};
};

// Reads the real signals and publishes activity and palette together. A
// signal failure publishes OFF: it neither keeps a previous dark state nor
// invents signals.
inline RefreshResult RefreshPolicy(Configuration config) noexcept {
    RefreshResult r;
    const policy::State before = policy::Current();
    r.before = before.active;
    r.paletteBefore = before.palette;
    r.signalsOk = policy::ReadWindowsSignals(&r.signals);
    if (r.signalsOk) {
        policy::Publish(config.mode, config.palette, r.signals);
    } else {
        policy::PublishState(false, config.palette);
        g_signalFailures.fetch_add(1, std::memory_order_relaxed);
    }
    const policy::State after = policy::Current();
    r.after = after.active;
    r.paletteAfter = after.palette;
    return r;
}

// Repaint needed: activity changed, or the palette changed while ColorFix
// stays active (Default -> AMOLED alone does not change activity). A palette
// change while inactive paints nothing different and needs no repaint.
constexpr bool NeedsRepaint(const RefreshResult& r) noexcept {
    return r.before != r.after || (r.after && r.paletteBefore != r.paletteAfter);
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

// Refresh, and only on a visible change: the atomic is already updated by
// RefreshPolicy, then windows are invalidated.
inline RefreshResult ApplyRefresh(Configuration config, HWND skip) {
    const RefreshResult r = RefreshPolicy(config);
    if (r.before != r.after) g_transitions.fetch_add(1, std::memory_order_relaxed);
    else if (NeedsRepaint(r)) g_paletteRepaints.fetch_add(1, std::memory_order_relaxed);
    if (NeedsRepaint(r)) PropagateColorChange(skip);
    return r;
}

inline LRESULT CALLBACK ListenerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (IsPolicySignal(msg, wp, lp)) {
        g_signalsSeen.fetch_add(1, std::memory_order_relaxed);
        ApplyRefresh(DecodeConfiguration(g_config.load(std::memory_order_relaxed)), hwnd);
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

inline bool StartListener(Configuration config) {
    g_config.store(EncodeConfiguration(config), std::memory_order_relaxed);
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

// Configuration change from the host (e.g. Windhawk settings): mode and
// palette are read once by the host and applied together. Store, then refresh.
inline RefreshResult SetConfiguration(Configuration config) {
    g_config.store(EncodeConfiguration(config), std::memory_order_relaxed);
    return ApplyRefresh(config, g_listenerWnd.load());
}

} // namespace colorfix::runtime
