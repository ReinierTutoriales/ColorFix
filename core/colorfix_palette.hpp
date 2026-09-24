#pragma once

namespace colorfix {

// Dark palette selected by the host. Independent of policy::Mode: the mode
// decides whether ColorFix is active, the palette decides which dark colors.
enum class Palette : unsigned char {
    Default = 0,  // phase 1 palette (#202020 surfaces), unchanged since 9f
    Amoled  = 1,  // pure black (#000000) surfaces
};

inline constexpr int kPaletteCount = 2;

constexpr int PaletteIndex(Palette p) noexcept {
    return p == Palette::Amoled ? 1 : 0;
}

} // namespace colorfix
