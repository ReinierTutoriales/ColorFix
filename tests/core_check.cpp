#include <windows.h>
#include <commctrl.h>
#include "colorfix_mapper.hpp"

using namespace colorfix;

constexpr bool FixedPoint(int index) {
    const COLORREF c = MapSystemColor(index, 0);
    return MapLiteralColor(c) == c;
}

static_assert(FixedPoint(COLOR_WINDOW));
static_assert(FixedPoint(COLOR_WINDOWTEXT));
static_assert(FixedPoint(COLOR_BTNFACE));
static_assert(FixedPoint(COLOR_GRAYTEXT));
static_assert(FixedPoint(COLOR_3DSHADOW));
static_assert(FixedPoint(COLOR_3DHILIGHT));
static_assert(IsSpecialColor(CLR_INVALID));
static_assert(IsSpecialColor(CLR_DEFAULT));
static_assert(IsPaletteEncoded(PALETTEINDEX(1)));
static_assert(IsPaletteEncoded(PALETTERGB(1, 2, 3)));
