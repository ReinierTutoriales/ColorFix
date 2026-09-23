#pragma once
#include <windows.h>
#include "colorfix_color.hpp"
#include "colorfix_roles.hpp"

namespace colorfix {

struct ColorMapper final {
    static constexpr COLORREF Literal(COLORREF value) noexcept {
        return MapLiteralColor(value);
    }

    static constexpr COLORREF System(int index, COLORREF original) noexcept {
        return MapSystemColor(index, original);
    }
};

} // namespace colorfix
