#include <windows.h>
#include <commctrl.h>
#include "colorfix_mapper.hpp"
#include "colorfix_policy.hpp"
#include "colorfix_hooks.hpp"

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

// Candidate (c) classifier decision table: only a single Button class,
// optionally with one X:: prefix, is eligible. Multi-class and ambiguous
// names remain fail-closed.
static_assert(colorfix::hooks::SingleThemeClass(L"Button") != nullptr);
static_assert(colorfix::hooks::SingleThemeClass(L"Explorer::Button") != nullptr);
static_assert(colorfix::hooks::SingleThemeClass(L"Button;Edit") == nullptr);
static_assert(colorfix::hooks::SingleThemeClass(nullptr) == nullptr);

bool ThemeRefcountAutotest() {
    using namespace colorfix::hooks;
    const HTHEME h = reinterpret_cast<HTHEME>(static_cast<UINT_PTR>(0xCF01));
    const HTHEME unknown = reinterpret_cast<HTHEME>(static_cast<UINT_PTR>(0xCF02));
    if (!RememberTheme(h, L"Button")) return false;
    if (!RememberTheme(h, L"Button")) return false;
    if (!KnownButtonTheme(h)) return false;
    if (!ReleaseTheme(h) || !KnownButtonTheme(h)) return false;
    if (!ReleaseTheme(h) || KnownButtonTheme(h)) return false;
    if (ReleaseTheme(unknown) || KnownButtonTheme(unknown)) return false;
    return true;
}

struct ThemeRefcountCheck {
    ThemeRefcountCheck() {
        if (!ThemeRefcountAutotest()) ExitProcess(2);
    }
} g_themeRefcountCheck;
