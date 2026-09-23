#pragma once
#include <windows.h>
#include "colorfix_color.hpp"

namespace colorfix {

constexpr COLORREF MapSystemColor(int index, COLORREF original) noexcept {
    switch (index) {
    case COLOR_WINDOW:       return Rgb(32, 32, 32);
    case COLOR_WINDOWTEXT:   return Rgb(220, 220, 220);
    case COLOR_BTNFACE:      return Rgb(45, 45, 45);
    case COLOR_BTNTEXT:      return Rgb(220, 220, 220);
    case COLOR_GRAYTEXT:     return Rgb(145, 145, 145);
    case COLOR_3DFACE:       return Rgb(45, 45, 45);
    case COLOR_3DSHADOW:     return Rgb(65, 65, 65);
    case COLOR_3DHILIGHT:    return Rgb(85, 85, 85);
    case COLOR_MENU:         return Rgb(37, 37, 37);
    case COLOR_MENUTEXT:     return Rgb(220, 220, 220);
    case COLOR_INFOBK:       return Rgb(48, 48, 48);
    case COLOR_INFOTEXT:     return Rgb(220, 220, 220);
    default:                 return original;
    }
}

} // namespace colorfix
