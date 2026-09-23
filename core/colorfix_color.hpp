#pragma once
#include <windows.h>
#include <cstdint>

namespace colorfix {

constexpr bool IsPaletteEncoded(COLORREF c) noexcept {
    const auto high = static_cast<std::uint32_t>(c) & 0xFF000000u;
    return high == 0x01000000u || high == 0x02000000u;
}

constexpr bool IsSpecialColor(COLORREF c) noexcept {
    // Sentinel values used by several Win32/common-control APIs.
    return c == CLR_INVALID || c == CLR_NONE || c == CLR_DEFAULT || IsPaletteEncoded(c);
}

constexpr COLORREF Rgb(BYTE r, BYTE g, BYTE b) noexcept {
    return static_cast<COLORREF>(r | (static_cast<DWORD>(g) << 8) |
                                 (static_cast<DWORD>(b) << 16));
}

constexpr COLORREF MapLiteralColor(COLORREF c) noexcept {
    if (IsSpecialColor(c)) return c;

    const auto r = GetRValue(c);
    const auto g = GetGValue(c);
    const auto b = GetBValue(c);

    // Conservative fallback. Semantic system-color mapping takes precedence.
    const unsigned luma = (54u * r + 183u * g + 19u * b) >> 8;
    if (luma >= 235u) return Rgb(32, 32, 32);
    if (luma <= 20u)  return Rgb(220, 220, 220);
    return c;
}

} // namespace colorfix
