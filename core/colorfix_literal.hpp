#pragma once
#include <windows.h>
#include "colorfix_color.hpp"
#include "colorfix_palette.hpp"
#include "colorfix_roles.hpp"

// Literal (application-chosen) color mapping, split by what the color paints.
// Increment 11a: with the AMOLED palette #000000 is both a semantic surface
// color and the most common literal text color, so one shared mapping cannot
// be correct for both. Each hook declares whether it maps text or a fill.
namespace colorfix {

// Default palette: exactly the pre-11a behavior for text and fills alike
// (tests/core_check.cpp compares it against a frozen copy of the old mapper).
// Semantic colors are fixed points; white -> COLOR_WINDOW, black -> text.
constexpr COLORREF MapLiteralDefault(COLORREF c) noexcept {
    if (IsSpecialColor(c) || IsSemanticColor(Palette::Default, c)) return c;
    const unsigned luma = Luma(c);
    if (luma >= kLightLuma) return MapSystemColorDefault(COLOR_WINDOW, c);
    if (luma <= kDarkLuma) return MapSystemColorDefault(COLOR_WINDOWTEXT, c);
    return c;
}

// Text (SetTextColor). AMOLED: dark text is never kept, including #000000,
// which is deliberately NOT a fixed point on this path. Light text is kept:
// AMOLED keeps black literal fills black, so white-on-black stays readable.
constexpr COLORREF MapLiteralText(Palette palette, COLORREF c) noexcept {
    if (palette == Palette::Default) return MapLiteralDefault(c);
    if (IsSpecialColor(c)) return c;
    if (Luma(c) <= kDarkLuma) return MapSystemColorAmoled(COLOR_WINDOWTEXT, c);
    return c;
}

// Fills (SetBkColor, CreateSolidBrush). AMOLED: semantic colors are fixed
// points, light fills become the black surface, dark fills stay dark.
constexpr COLORREF MapLiteralFill(Palette palette, COLORREF c) noexcept {
    if (palette == Palette::Default) return MapLiteralDefault(c);
    if (IsSpecialColor(c) || IsSemanticColor(Palette::Amoled, c)) return c;
    if (Luma(c) >= kLightLuma) return MapSystemColorAmoled(COLOR_WINDOW, c);
    return c;
}

} // namespace colorfix
