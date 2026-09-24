#pragma once
#include <windows.h>
#include "colorfix_color.hpp"
#include "colorfix_literal.hpp"
#include "colorfix_palette.hpp"
#include "colorfix_roles.hpp"

namespace colorfix {

// Aggregation point for hooks. There is intentionally no palette-less or
// role-less literal mapping: callers state the palette and text vs fill.
struct ColorMapper final {
    static constexpr COLORREF Text(Palette p, COLORREF value) noexcept {
        return MapLiteralText(p, value);
    }
    static constexpr COLORREF Fill(Palette p, COLORREF value) noexcept {
        return MapLiteralFill(p, value);
    }
    static constexpr COLORREF System(Palette p, int index, COLORREF original) noexcept {
        return MapSystemColor(p, index, original);
    }
};

} // namespace colorfix
