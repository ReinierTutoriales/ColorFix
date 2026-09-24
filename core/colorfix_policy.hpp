#pragma once

#include <windows.h>
#include <atomic>

#include "colorfix_palette.hpp"

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

// Published state: effective activity and palette in ONE atomic byte, so a
// hook that loads it once never combines the activity of one configuration
// with the palette of another. Bit 0 = active, bit 1 = AMOLED palette.
// Fail-safe default: inactive (pass-through), Default palette, until a host
// adapter publishes an explicit mode with real signals.
struct State {
    bool active = false;
    Palette palette = Palette::Default;
};

constexpr unsigned char EncodeState(bool active, Palette palette) noexcept {
    return static_cast<unsigned char>((active ? 1u : 0u) |
                                      (palette == Palette::Amoled ? 2u : 0u));
}

constexpr State DecodeState(unsigned char raw) noexcept {
    return State{(raw & 1u) != 0, (raw & 2u) != 0 ? Palette::Amoled : Palette::Default};
}

inline std::atomic<unsigned char> g_state{EncodeState(false, Palette::Default)};

// Hooks: one relaxed load per call, then use the returned copy only.
inline State Current() noexcept {
    return DecodeState(g_state.load(std::memory_order_relaxed));
}

inline bool Active() noexcept {
    return Current().active;
}

inline void PublishState(bool active, Palette palette) noexcept {
    g_state.store(EncodeState(active, palette), std::memory_order_relaxed);
}

inline void Publish(Mode mode, Palette palette, Signals signals) noexcept {
    PublishState(EffectiveDark(mode, signals), palette);
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
