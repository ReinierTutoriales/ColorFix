#pragma once
#include <windows.h>
#include <commctrl.h>
#include <cstdint>

namespace colorfix {

constexpr bool IsPaletteEncoded(COLORREF c) noexcept {
    const auto high = static_cast<std::uint32_t>(c) & 0xFF000000u;
    return high == 0x01000000u || high == 0x02000000u;
}

constexpr bool IsSpecialColor(COLORREF c) noexcept {
    return c == CLR_INVALID || c == CLR_DEFAULT || IsPaletteEncoded(c);
}

constexpr COLORREF Rgb(BYTE r, BYTE g, BYTE b) noexcept {
    return static_cast<COLORREF>(r | (static_cast<DWORD>(g) << 8) |
                                 (static_cast<DWORD>(b) << 16));
}

constexpr unsigned Luma(COLORREF c) noexcept {
    return (54u * GetRValue(c) + 183u * GetGValue(c) + 19u * GetBValue(c)) >> 8;
}

inline constexpr unsigned kLightLuma = 235u;  // literal considered "white"
inline constexpr unsigned kDarkLuma  = 20u;   // literal considered "black"

} // namespace colorfix
