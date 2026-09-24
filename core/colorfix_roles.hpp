#pragma once
#include <windows.h>
#include "colorfix_color.hpp"
#include "colorfix_palette.hpp"

namespace colorfix {

// Semantic system-color mapping. Every index returned mapped here is a
// "semantic color" of that palette (see kMappedSysColors).
constexpr COLORREF MapSystemColorDefault(int index, COLORREF original) noexcept {
    switch (index) {
    case COLOR_WINDOW:       return Rgb(32, 32, 32);
    case COLOR_WINDOWTEXT:   return Rgb(220, 220, 220);
    case COLOR_BTNFACE:      return Rgb(45, 45, 45);
    case COLOR_BTNTEXT:      return Rgb(220, 220, 220);
    case COLOR_GRAYTEXT:     return Rgb(145, 145, 145);
    case COLOR_3DSHADOW:     return Rgb(65, 65, 65);
    case COLOR_3DHILIGHT:    return Rgb(85, 85, 85);
    case COLOR_3DLIGHT:      return Rgb(72, 72, 72);
    case COLOR_3DDKSHADOW:   return Rgb(24, 24, 24);
    case COLOR_WINDOWFRAME:  return Rgb(64, 64, 64);
    case COLOR_APPWORKSPACE: return Rgb(28, 28, 28);
    case COLOR_SCROLLBAR:    return Rgb(52, 52, 52);
    case COLOR_MENU:         return Rgb(37, 37, 37);
    case COLOR_MENUTEXT:     return Rgb(220, 220, 220);
    case COLOR_MENUBAR:      return Rgb(37, 37, 37);
    case COLOR_INFOBK:       return Rgb(48, 48, 48);
    case COLOR_INFOTEXT:     return Rgb(220, 220, 220);
    default:                 return original;
    }
}

// AMOLED: primary surfaces are pure black; borders, scroll track and tooltip
// stay non-black so classic edges remain distinguishable on #000000.
constexpr COLORREF MapSystemColorAmoled(int index, COLORREF original) noexcept {
    switch (index) {
    case COLOR_WINDOW:       return Rgb(0, 0, 0);
    case COLOR_WINDOWTEXT:   return Rgb(220, 220, 220);
    case COLOR_BTNFACE:      return Rgb(0, 0, 0);
    case COLOR_BTNTEXT:      return Rgb(220, 220, 220);
    case COLOR_GRAYTEXT:     return Rgb(145, 145, 145);
    case COLOR_3DSHADOW:     return Rgb(64, 64, 64);
    case COLOR_3DHILIGHT:    return Rgb(80, 80, 80);
    case COLOR_3DLIGHT:      return Rgb(56, 56, 56);
    case COLOR_3DDKSHADOW:   return Rgb(30, 30, 30);
    case COLOR_WINDOWFRAME:  return Rgb(64, 64, 64);
    case COLOR_APPWORKSPACE: return Rgb(0, 0, 0);
    case COLOR_SCROLLBAR:    return Rgb(26, 26, 26);
    case COLOR_MENU:         return Rgb(0, 0, 0);
    case COLOR_MENUTEXT:     return Rgb(220, 220, 220);
    case COLOR_MENUBAR:      return Rgb(0, 0, 0);
    case COLOR_INFOBK:       return Rgb(26, 26, 26);
    case COLOR_INFOTEXT:     return Rgb(220, 220, 220);
    default:                 return original;
    }
}

constexpr COLORREF MapSystemColor(Palette palette, int index, COLORREF original) noexcept {
    return palette == Palette::Amoled ? MapSystemColorAmoled(index, original)
                                      : MapSystemColorDefault(index, original);
}

// Indices remapped by both palettes. Keep in sync with the switches above;
// tests/core_check.cpp asserts that no other index in [0, COLOR_MENUBAR] is
// remapped.
inline constexpr int kMappedSysColors[] = {
    COLOR_WINDOW, COLOR_WINDOWTEXT, COLOR_BTNFACE, COLOR_BTNTEXT, COLOR_GRAYTEXT,
    COLOR_3DSHADOW, COLOR_3DHILIGHT, COLOR_3DLIGHT, COLOR_3DDKSHADOW,
    COLOR_WINDOWFRAME, COLOR_APPWORKSPACE, COLOR_SCROLLBAR, COLOR_MENU,
    COLOR_MENUTEXT, COLOR_MENUBAR, COLOR_INFOBK, COLOR_INFOTEXT,
};

// True when `c` is a semantic color produced by `palette`.
constexpr bool IsSemanticColor(Palette palette, COLORREF c) noexcept {
    for (int index : kMappedSysColors)
        if (MapSystemColor(palette, index, CLR_INVALID) == c) return true;
    return false;
}

} // namespace colorfix
