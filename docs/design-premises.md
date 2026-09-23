# ColorFix design premises

This document records architectural constraints and measured conclusions that
future increments must preserve. Update it when CI evidence changes a premise
or establishes a new boundary.

## 1. Mechanism and policy are separate

Hooks answer **which rendering paths ColorFix can intercept**. Policy answers
**whether ColorFix should alter a selected process at this moment**.

Coverage probes deliberately test the mechanism without theme-policy
heuristics. A visual MISS must continue to mean that the tested mechanism did
not cover that rendering path, rather than that policy silently disabled it.

## 2. Runtime policy modes

The product policy will expose three modes:

- `Disabled`: ColorFix does not modify the process.
- `ForceDark`: apply the dark mapping regardless of the normal Windows
  light/dark application preference.
- `FollowSystem`: follow the Windows application-theme preference.

High Contrast overrides all three modes as described below.

The effective state should be computed outside the hook hot path. Installed
hooks should need only a cheap atomic state read/branch.

## 3. High Contrast is an absolute override

When Windows High Contrast is enabled, ColorFix must not remap colors, including
under `ForceDark`.

Detection: `SystemParametersInfo(SPI_GETHIGHCONTRAST)` and
`HCF_HIGHCONTRASTON`.

Rationale: High Contrast is an accessibility mechanism whose system colors must
not be replaced by ColorFix's palette.

## 4. FollowSystem signal

`FollowSystem` uses the per-user `AppsUseLightTheme` value under the Windows
Themes/Personalize settings. `SystemUsesLightTheme` is not the application
theme signal.

Theme changes should be observed through `WM_SETTINGCHANGE` /
`"ImmersiveColorSet"`. Registry/theme-state reads happen when the state
changes, never inside rendering hooks.

## 5. Hot switching and caches

Do not reinstall hooks merely because the effective policy changes. Update the
effective atomic state instead.

Disabling an already-active process is not symmetric with enabling it: an
application may have cached a ColorFix brush/color, and Common Controls may
cache colors too. On an effective-state change:

1. update the effective state;
2. send `WM_SYSCOLORCHANGE` to windows in the process so cooperative code
   refreshes cached system colors;
3. force the appropriate repaint.

Some applications may not refresh all cached state and can require a restart.
That is a product limitation to document rather than hide.

## 6. Disabled processes and final injection model

In the final `SetWindowsHookEx` injection architecture, an unselected or
disabled application should remain inert: the DLL may be resident because of
the global hook mechanism, but it must not install ColorFix rendering hooks.

The atomic pass-through state is for a process whose rendering hooks were
already installed and whose effective state changes while it is running.

## 7. Preserve explicit application choices

Prefer exact semantic guards over visual guesses.

Current examples:

- ColorFix's own mapped palette values are fixed points where appropriate.
- `WM_CTLCOLOR*` adjustment runs the original first and replaces its result
  only when it is exactly the expected DefWindowProc system brush.
- An application-provided `WM_CTLCOLORSTATIC` reply was measured as preserved
  with zero ColorFix DefWindowProcCtlColor calls.

A future whole-window "already dark" detector would be heuristic. Keep it
optional, separately measured, and outside the fundamental coverage mechanism.

## 8. Policy CI gates

Add a dedicated policy increment before expanding rendering coverage further.
Its intended gates are:

- `FollowSystem` + light application theme: surfaces equal baseline.
- `FollowSystem` + dark application theme: mapped surfaces are covered.
- hot dark -> light transition: return to baseline after
  `WM_SYSCOLORCHANGE` plus repaint, subject to documented cache limitations.
- High Contrast: baseline/pass-through regardless of `ForceDark` or
  `FollowSystem`.

The test may control its runner user's application-theme setting when isolated
and restore the original setting before exit. Do not mix these policy gates
into the existing mechanism-coverage verdicts.

## 9. Measured rendering boundaries

Evidence established by the current probes:

- Phase 1 export hooks cover explicit application calls through the hooked
  USER32/GDI exports.
- A `COLOR_x + 1` class-background erase through `DefWindowProcW` bypasses
  those Phase 1 exports; the Phase 1b `WM_ERASEBKGND` detour covers it.
- Default USER32 Static and Edit backgrounds are covered through
  `WM_CTLCOLOR*` adjustment.
- An application-chosen Static color is preserved.
- The classic USER32 push-button face remains a visual MISS even though the
  `WM_CTLCOLORBTN` semantic autotest passes. Its face is therefore a separate
  rendering path.
- The scrollbar observation is informational only; no stronger conclusion is
  established yet.

These results were reproduced by CI on x64, x86, and native ARM64.

## 10. Requirements for future rendering mechanisms

Button-specific handling, menus, Common Controls v6, UxTheme, GDI+, DirectWrite,
Direct2D, and later mechanisms must all obey the same effective policy state.
No new rendering path may bypass the High Contrast override or modify a process
that policy says should remain unchanged.

Windhawk generation currently incorporates shared hooks from `hooks/`.
Syntax validation of the generated mod is not equivalent to runtime validation
inside Windhawk; keep that distinction explicit.

## 11. Maintenance rule

Update this document when an increment:

- changes policy semantics;
- proves or disproves a rendering-path assumption;
- introduces a new architecture/injection constraint;
- establishes a new accessibility or compatibility invariant; or
- turns a previously informational observation into a tested contract.

Prefer measured statements over assumptions, and label unverified expectations
as such.
