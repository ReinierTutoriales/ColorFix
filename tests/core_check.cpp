#include <windows.h>
#include <commctrl.h>
#include "colorfix_mapper.hpp"
#include "colorfix_policy.hpp"

using namespace colorfix;

// ---------------------------------------------------------------- palettes
constexpr COLORREF kBlack = RGB(0, 0, 0);
constexpr COLORREF kWhite = RGB(255, 255, 255);
constexpr COLORREF kText  = RGB(220, 220, 220);

// Semantic colors of the active palette are fixed points of the fill path.
constexpr bool FillFixedPoints(Palette p) {
    for (int index : kMappedSysColors) {
        const COLORREF c = MapSystemColor(p, index, CLR_INVALID);
        if (MapLiteralFill(p, c) != c) return false;
    }
    return true;
}
static_assert(FillFixedPoints(Palette::Default));
static_assert(FillFixedPoints(Palette::Amoled));

// kMappedSysColors lists exactly the remapped indices of both palettes.
constexpr bool MappedListExact(Palette p) {
    for (int i = 0; i <= COLOR_MENUBAR; ++i) {
        bool listed = false;
        for (int index : kMappedSysColors) listed |= index == i;
        const bool remapped = MapSystemColor(p, i, CLR_INVALID) != CLR_INVALID;
        if (listed != remapped) return false;
    }
    return true;
}
static_assert(MappedListExact(Palette::Default));
static_assert(MappedListExact(Palette::Amoled));

// ------------------------------------------------------ 11a AMOLED invariants
static_assert(MapSystemColor(Palette::Amoled, COLOR_WINDOW, kWhite) == kBlack);
static_assert(MapSystemColor(Palette::Amoled, COLOR_BTNFACE, kWhite) == kBlack);
static_assert(MapLiteralText(Palette::Amoled, kBlack) == kText);   // never black text
static_assert(MapLiteralText(Palette::Default, kBlack) == kText);
static_assert(MapLiteralFill(Palette::Amoled, kBlack) == kBlack);  // black surface kept
static_assert(MapLiteralFill(Palette::Amoled, kWhite) == kBlack);  // white surface -> black
static_assert(MapLiteralText(Palette::Amoled, kWhite) == kWhite);  // white text on black
static_assert(MapLiteralText(Palette::Amoled, kText) == kText);
static_assert(MapLiteralFill(Palette::Amoled, RGB(128, 0, 0)) == RGB(128, 0, 0));
static_assert(MapLiteralText(Palette::Amoled, RGB(128, 0, 0)) == RGB(128, 0, 0));

// Special colors pass through both paths in both palettes.
static_assert(IsSpecialColor(CLR_INVALID));
static_assert(IsSpecialColor(CLR_DEFAULT));
static_assert(IsPaletteEncoded(PALETTEINDEX(1)));
static_assert(IsPaletteEncoded(PALETTERGB(1, 2, 3)));
static_assert(MapLiteralText(Palette::Amoled, CLR_DEFAULT) == CLR_DEFAULT);
static_assert(MapLiteralFill(Palette::Amoled, PALETTEINDEX(1)) == PALETTEINDEX(1));

// ------------------------------------- Default palette == pre-11a behavior
// Frozen copy of MapLiteralColor at 44e3b32. Test oracle only.
constexpr COLORREF LegacyMapLiteralColor(COLORREF c) {
    if (IsSpecialColor(c)) return c;
    const auto r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    const unsigned luma = (54u * r + 183u * g + 19u * b) >> 8;
    if (c == RGB(32,32,32) || c == RGB(45,45,45) || c == RGB(37,37,37) ||
        c == RGB(48,48,48) || c == RGB(65,65,65) || c == RGB(85,85,85) ||
        c == RGB(145,145,145) || c == RGB(220,220,220)) return c;
    if (luma >= 235u) return RGB(32, 32, 32);
    if (luma <= 20u) return RGB(220, 220, 220);
    return c;
}

constexpr bool DefaultMatchesLegacy() {
    for (int v = 0; v < 256; ++v) {  // every grey
        const COLORREF c = RGB(v, v, v);
        if (MapLiteralText(Palette::Default, c) != LegacyMapLiteralColor(c)) return false;
        if (MapLiteralFill(Palette::Default, c) != LegacyMapLiteralColor(c)) return false;
    }
    // 6^3 lattice incl. 0 and 255. Kept small for MSVC's default
    // /constexpr:steps budget; the grey sweep covers both luma thresholds.
    for (int r = 0; r < 256; r += 51)
        for (int g = 0; g < 256; g += 51)
            for (int b = 0; b < 256; b += 51) {
                const COLORREF c = RGB(r, g, b);
                if (MapLiteralText(Palette::Default, c) != LegacyMapLiteralColor(c)) return false;
                if (MapLiteralFill(Palette::Default, c) != LegacyMapLiteralColor(c)) return false;
            }
    return true;
}
static_assert(DefaultMatchesLegacy());

// ------------------------------------------------------------------ policy
using colorfix::policy::EffectiveDark;
using colorfix::policy::Mode;
using colorfix::policy::Signals;

static_assert(!EffectiveDark(Mode::Disabled,     Signals{false, true}));
static_assert(!EffectiveDark(Mode::Disabled,     Signals{false, false}));
static_assert( EffectiveDark(Mode::ForceDark,    Signals{false, true}));
static_assert( EffectiveDark(Mode::ForceDark,    Signals{false, false}));
static_assert(!EffectiveDark(Mode::FollowSystem, Signals{false, true}));
static_assert( EffectiveDark(Mode::FollowSystem, Signals{false, false}));

// Accessibility override is absolute, including ForceDark.
static_assert(!EffectiveDark(Mode::Disabled,     Signals{true, true}));
static_assert(!EffectiveDark(Mode::Disabled,     Signals{true, false}));
static_assert(!EffectiveDark(Mode::ForceDark,    Signals{true, false}));
static_assert(!EffectiveDark(Mode::ForceDark,    Signals{true, true}));
static_assert(!EffectiveDark(Mode::FollowSystem, Signals{true, false}));
static_assert(!EffectiveDark(Mode::FollowSystem, Signals{true, true}));

// Packed state round-trips: activity and palette are published together.
using colorfix::policy::DecodeState;
using colorfix::policy::EncodeState;
constexpr bool StateRoundTrip(bool a, Palette p) {
    const auto s = DecodeState(EncodeState(a, p));
    return s.active == a && s.palette == p;
}
static_assert(StateRoundTrip(false, Palette::Default));
static_assert(StateRoundTrip(true,  Palette::Default));
static_assert(StateRoundTrip(false, Palette::Amoled));
static_assert(StateRoundTrip(true,  Palette::Amoled));
static_assert(EncodeState(false, Palette::Default) == 0);  // fail-safe default
