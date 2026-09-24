# Increment 11b: probe-first text safety

Increment 11b is a measurement increment. It must not add another visual fix for
real applications before the probe attributes the failing text path.

The motivating failure is the AMOLED test against Notepad-like real
applications: ColorFix can map an application/client surface to black while a
bar/status/menu surface remains light, then text on that light surface is mapped
to a light color. This reproduces the increment-9c class of failure: text and
background were decided by different rendering paths.

## Scope

11b is limited to causal measurement and minimal product logging.

In scope:

- probe windows that reproduce a real top-level menu bar and a v6 status bar;
- attribution of text color paths through `SetTextColor`, `GetSysColor`, and
  UxTheme text/background drawing;
- explicit contrast reporting for the measured surfaces;
- minimal Windhawk startup logging for core hook and UxTheme hook status.

Out of scope:

- adding `explorer.exe` to the product include list;
- shipping the experimental `DrawThemeBackground(Menu)` repaint as a fix;
- forcing `LoadLibraryW("uxtheme.dll")` in the Windhawk/product path;
- using undocumented dark-mode UxTheme ordinals;
- using `WM_UAHDRAWMENU` / `WM_UAHDRAWMENUITEM` hooks;
- a generic rule such as "preserve text when the background is unknown".

## Why a generic background rule is rejected

`SetTextColor` does not know the surface that will be behind the glyphs. With a
transparent background mode, the background may have been painted earlier by an
unrelated path, and that state is no longer encoded in the DC. A future safety
rule must therefore be gated by concrete measured context, as the 9f button-text
exception is gated by known theme class and part.

## Probe additions

Add two controlled windows to `tests/probe/probe_hooks.cpp`.

### M: real top-level menu

Window M has an actual `HMENU` attached to the top-level window and a real
non-client frame, such as `WS_OVERLAPPEDWINDOW`, rather than the existing
`WS_POPUP` probe shape. The menu bar is non-client rendering, so the capture
region must be derived from the actual window geometry and `GetMenuBarInfo`, not
from a fixed client-relative rectangle.

M must be kept in-bounds and visible only during its phase, following the same
monitor/occlusion discipline as window U. Hide it after the phase so it cannot
contaminate later captures.

Measure, for OFF and ON policy captures:

- menu-bar background color;
- menu-bar text color and contrast;
- `SetTextColor` calls that occur while drawing the menu text;
- `GetSysColor` calls for `COLOR_MENUTEXT`, `COLOR_MENU`, and
  `COLOR_MENUBAR` during the menu paint;
- UxTheme `DrawThemeText` / `DrawThemeTextEx` and
  `DrawThemeBackground` / `DrawThemeBackgroundEx` events whose class is `Menu`.

Menu text must not reuse the exact non-antialiased glyph oracle from U: the
non-client menu uses the system menu font and ClearType. Use a tolerant oracle:
background is the modal color in the measured menu rectangle; text is the pixel
or cluster farthest from that background in luminance after excluding border and
selection artifacts; contrast is computed between that text candidate and the
background. The probe must enforce a minimum number of text-candidate pixels.

### S: v6 status bar

Window S contains a `msctls_statusbar32` created under the existing Common
Controls v6 activation context. It exists to measure the status-bar failure
seen in real applications.

The status bar paints in its own child HWND during `WM_PAINT` or
`WM_PRINTCLIENT`. Attribution must therefore mark the status HWND as the current
paint target, not merely the top-level probe window.

Measure, for OFF and ON policy captures:

- status-bar background color;
- status-bar text color and contrast;
- `SetTextColor` calls while drawing the status text;
- `GetSysColor` calls for `COLOR_BTNTEXT`, `COLOR_WINDOWTEXT`, and
  `COLOR_3DFACE` during the status-bar paint;
- UxTheme text/background events whose class is `Status` or the concrete class
  observed by the probe.

Unlike M, S may use the existing exact text oracle: send a non-antialiased test
font with `WM_SETFONT` to the status bar and measure the rendered text similarly
to the U text surfaces.

## Paint attribution requirements

Attribution must be narrow enough that hot-path hooks do not contaminate the
rest of the probe.

- M: mark painting during the top-level window's `WM_NCPAINT`, `WM_PRINT`, and
  any menu-print path used by `PrintWindow`.
- S: mark painting during the status child window's `WM_PAINT` and
  `WM_PRINTCLIENT`.
- The `GetSysColor` tap must count and optionally bypass only while the current
  paint target is the surface under test. Outside that context it must return
  the normal product-mapped result and avoid affecting existing autotests.

## Required causal categories

Each surface must be measured four times under the ON policy:

1. no bypass;
2. literal bypass only (`SetTextColor` returns the incoming color);
3. semantic bypass only (`GetSysColor` preserves selected text roles);
4. combined bypass.

Classify the cause as:

- `literal`: only the literal bypass restores the text color/contrast to the
  OFF-policy reference;
- `semantic`: only the semantic bypass restores it;
- `combined`: neither bypass alone restores it, but the combined bypass does;
- `theme`: no bypass restores it, but UxTheme text drawing for the target class
  is attributed to the surface;
- `unknown`: none of the above is established.

`theme` and `unknown` are valid measurement outcomes. They must not trigger a
production rule by themselves.

## Reporting budget

Use a compact CI notice separate from the crowded autotest group, or place the
summary under the existing `surfaces` reporting area where there is remaining
space. Preserve the notice-prefix rule learned from run 302: include a colon
only when the emitted prefix actually carries one, and confirm in the run that
all expected lines are visible.

A compact target shape is:

```text
text-safety M.menu bg=F5F5F5 text=E8E8E8 contrast=1.1 calls lit=3 sem=1 theme=0 cause=literal
text-safety S.status bg=F0F0F0 text=E8E8E8 contrast=1.1 calls lit=0 sem=4 theme=2 cause=semantic
```

## Product logging for 11b

The Windhawk template logs only startup status after `RegisterPhase1Hooks`:

- `ColorFix: phase1 core hooks ON` or `FAILED`;
- `ColorFix: uxtheme hooks ON` or `OFF`.

This logging remains in `windhawk/colorfix.wh.template.cpp`. It must not move
into `hooks/`, because the shared hook code must not depend on Windhawk APIs.

## Follow-up rules

A production text-safety rule can be added only after the probe establishes a
specific cause and context.

- `literal`: add a `SetTextColor` exception gated by measured class/part/window
  context.
- `semantic`: add an equivalent contextual exception around the relevant
  `GetSysColor` role, only within the measured paint context.
- `combined`: preserve both paths only within the measured context.
- `theme` or `unknown`: do not patch text mapping; add more observation or use a
  separate non-client/menu strategy.
