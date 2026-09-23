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

constexpr COLORREF MapLiteralColor(COLORREF c) noexcept {
    if (IsSpecialColor(c)) return c;
    const auto r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    const unsigned luma = (54u * r + 183u * g + 19u * b) >> 8;

    // Keep ColorFix's own palette as fixed points. Hooks can observe colors
    // already returned by semantic mappings.
    if (c == Rgb(32,32,32) || c == Rgb(45,45,45) || c == Rgb(37,37,37) ||
        c == Rgb(48,48,48) || c == Rgb(65,65,65) || c == Rgb(85,85,85) ||
        c == Rgb(145,145,145) || c == Rgb(220,220,220)) return c;

    if (luma >= 235u) return Rgb(32, 32, 32);
    if (luma <= 20u) return Rgb(220, 220, 220);
    return c;
}

} // namespace colorfix
