#pragma once

#include <windows.h>
#include <atomic>

namespace colorfix::policy {

enum class Mode : unsigned char {
    Disabled,
    ForceDark,
    FollowSystem,
};

struct Signals {
    bool highContrast = false;
    bool appsUseLightTheme = true;
};

constexpr bool EffectiveDark(Mode mode, Signals signals) noexcept {
    if (signals.highContrast) return false;
    switch (mode) {
    case Mode::Disabled:     return false;
    case Mode::ForceDark:    return true;
    case Mode::FollowSystem: return !signals.appsUseLightTheme;
    }
    return false;
}

// ForceDark preserves the current probe/product behavior until configuration
// explicitly selects another policy. Hooks only pay one relaxed atomic load.
inline std::atomic<bool> g_effectiveDark{true};

inline bool Active() noexcept {
    return g_effectiveDark.load(std::memory_order_relaxed);
}

inline void Publish(Mode mode, Signals signals) noexcept {
    g_effectiveDark.store(EffectiveDark(mode, signals), std::memory_order_relaxed);
}

inline bool ReadWindowsSignals(Signals* out) noexcept {
    if (!out) return false;

    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0))
        return false;

    DWORD appsUseLightTheme = 1;
    DWORD size = sizeof(appsUseLightTheme);
    const LSTATUS reg = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &appsUseLightTheme,
        &size);

    // Missing policy value follows Windows' light-theme default. Other registry
    // failures are surfaced instead of silently changing policy.
    if (reg != ERROR_SUCCESS && reg != ERROR_FILE_NOT_FOUND)
        return false;

    out->highContrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    out->appsUseLightTheme = reg == ERROR_FILE_NOT_FOUND || appsUseLightTheme != 0;
    return true;
}

} // namespace colorfix::policy
