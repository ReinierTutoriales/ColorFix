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

ColorFix's own identity cache of the original system color brush handles
(increment 9b, used by the `FillRect` hook) is not refreshed on policy
changes. It stores handles only; whether a role is remapped is decided at the
call by the same predicate as `GetSysColorBrush`. That the handles are stable
is measured across real policy signals (section 9), not across `SetSysColors`;
if that premise breaks, the cache refresh must be wired with evidence.

## 6. Disabled processes and candidate injection models

The standalone injection model is **not decided yet**. SetWindowsHookEx is a
documented candidate, not the final architecture. Compare three candidates with
the same measurements before choosing:

1. Global SetWindowsHookEx / WH_CBT: no polling and no privileged service, but
   it is a desktop-wide shared resource and matching DLLs can become resident
   in non-selected GUI processes. Architecture-specific hosts/DLLs are required.
2. Process detection + directed injection: touches only selected processes, but
   under the current unprivileged/no-polling requirement it has no suitable
   process-start notification. ETW Kernel-Process / Win32_ProcessStartTrace are
   treated as privileged candidates for this design; otherwise use polling or
   a privileged service.
3. Launcher injection: ColorFix starts a selected application suspended, loads
   ColorFix, then resumes it. This avoids a global hook and privileged
   process-start monitoring and provides the earliest injection point, before
   later UI initialization/caching. Coverage is limited to launches routed
   through ColorFix, such as its shortcut or an association.

Measure startup latency, steady-state overhead, process coverage, architecture
behavior, compatibility/conflicts, security/privilege requirements, and how
early ColorFix is active. Do not select a model from API status alone.

If SetWindowsHookEx is selected, an unselected/disabled process must remain
inert: DLL residency caused by the global hook must not install ColorFix
rendering hooks. Process selection is evaluated once outside loader lock and
cached; the hook procedure must not repeatedly perform configuration/process
selection.

The atomic pass-through state is for a process whose rendering hooks were
already installed and whose effective theme policy changes while it is running.

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

### Runtime policy wiring

Probe increment 8 wires the policy to the real Windows signals through shared
product code in `hooks/colorfix_runtime.hpp` and uses the same runtime path from
the Windhawk adapter.

- `g_effectiveDark` starts OFF. A host must explicitly publish a mode with real
  signals before rendering hooks can modify output.
- `RefreshPolicy` reads High Contrast and `AppsUseLightTheme`; a read failure
  forces the effective state OFF and increments failure telemetry. It never
  preserves a previous dark state or invents substitute signals.
- A dedicated listener thread owns a hidden top-level window (not an
  `HWND_MESSAGE` window) so it can receive broadcast theme changes. Relevant
  signals are `WM_SETTINGCHANGE`/`"ImmersiveColorSet"`,
  `WM_SETTINGCHANGE`/`SPI_SETHIGHCONTRAST`, and `WM_SYSCOLORCHANGE`.
- On an effective ON/OFF transition, the atomic state is already published
  before invalidation. The runtime posts `WM_SYSCOLORCHANGE` to the process
  top-level windows and their children, then schedules `RedrawWindow`; it does
  not use blocking cross-thread `SendMessage` for propagation.
- Windhawk exposes `FollowSystem` (default), `ForceDark`, and `Disabled`.
  Unknown setting values fail safe to `Disabled`. The adapter publishes real
  signals before registering rendering hooks and stops the listener on unload
  or on initialization failure.
- Probe phase E drives the real runner HKCU `AppsUseLightTheme` value through
  light -> dark -> light and emits a real broadcast `WM_SETTINGCHANGE`
  `"ImmersiveColorSet"`. Reception, semantic transition, and E/K/C visual
  behavior are separate gates; each visual step must match 24/24 reference
  surfaces. The original registry value is restored before exit.
- CI does not toggle High Contrast for the runner session. The High Contrast
  veto remains covered by the simulated policy matrix, while the runtime
  listener's `SPI_SETHIGHCONTRAST` path is compiled but not exercised by CI.

Remaining integration debt: the shared runtime is wired into the Windhawk host,
but a future standalone `ColorFix.dll`/launcher host must call the same runtime
initialization and shutdown paths. Windhawk metadata/settings APIs were checked
against current Windhawk documentation; the generated-mod syntax check is still
not a substitute for executing the mod inside Windhawk.

## 9. Measured rendering boundaries

Evidence established by the current probes:

- Phase 1 export hooks cover explicit application calls through the hooked
  USER32/GDI exports.
- A `COLOR_x + 1` class-background erase through `DefWindowProcW` bypasses
  those Phase 1 exports; the Phase 1b `WM_ERASEBKGND` detour covers it.
- Default USER32 Static and Edit backgrounds are covered through
  `WM_CTLCOLOR*` adjustment.
- An application-chosen Static color is preserved.
- The classic USER32 push-button face remained a visual MISS through
  increment 9a even though the `WM_CTLCOLORBTN` semantic autotest passes. Its
  face is a separate rendering path, covered since increment 9b (below).
- The scrollbar observation is informational only; no stronger conclusion is
  established yet.
- Common Controls v6 (comctl32 6.16, probe increment 5, both load orders):
  - v6 Static background is covered through `WM_CTLCOLORSTATIC`, early and
    late, with no cache effect.
  - v6 Edit background (`FFFFFF`) and v6 push-button face (`FDFDFD`) remain a
    visual MISS in both orders and after `WM_SYSCOLORCHANGE`, although the
    `DefWindowProc` ctlcolor detour is observed in that window. This rules
    out load order and caches; it is consistent with themed (UxTheme)
    rendering, which the UxTheme increment must confirm.
  - The v6 ListView background is covered when hooks precede comctl32 v6
    (early), is a MISS when comctl32 v6 initialized first (late), and becomes
    covered after `WM_SYSCOLORCHANGE` is sent to the window and its children.
    Load order matters for comctl32 caches, and the invalidation step in
    section 5 recovers them. That the refresh goes through the hooked
    `GetSysColor` is an inference: calls during the invalidation step were
    not counted.
  - `comctl32.dll` was absent at process start in every run, so the probe has
    no static dependency that would blur the early/late distinction.
- UxTheme observation (probe increment 6, passive hooks, both load orders,
  x64/x86/ARM64):
  - The passive observer changed no pixels (all increment-5 references held)
    and lost no events, so these are measurements, not artifacts.
  - The v6 Edit background is painted by `DrawThemeBackground` with class
    `Edit`, part 3 = `EP_BACKGROUND`, state 1 = `EBS_NORMAL`.
  - The v6 push-button face is painted by `DrawThemeBackground` with class
    `Button`, part 1 = `BP_PUSHBUTTON`, state 1 = `PBS_NORMAL`. No
    `DrawThemeBackgroundEx` was observed.
  - The observed `GetThemeColor` calls are not background sources: Edit
    part 1/state 8 is `EP_EDITTEXT`/`ETS_CUEBANNER`, and property 3803 is
    `TMT_TEXTCOLOR` (the cue-banner text color). Static and Tooltip query
    `TMT_TEXTCOLOR` as well. Part, state, and property names were checked
    against `vsstyle.h`/`vssym32.h`.
  - Attribution is by theme class plus draw-rect size (90x80), not by HWND or
    coordinates: these controls draw onto a memory DC, so `WindowFromDC`
    cannot localize the rect.
  - Occasionally a query arrives on an `HTHEME` opened before the observer
    was installed and is logged with an unknown class (property 2425,
    `TMT_TEXTGLOWSIZE`). It is counted, not dropped, and does not affect the
    Edit/Button findings.
- Theme opt-out (probe increment 7, `SetWindowTheme`, both load orders,
  x64/x86/ARM64):
  - `SetWindowTheme(hwnd, L"", L"")` suppresses the observed 86x76 UxTheme
    draws for both the v6 Edit (`Edit`/`EP_BACKGROUND`) and v6 push button
    (`Button`/`BP_PUSHBUTTON`); restoring with `SetWindowTheme(hwnd, NULL,
    NULL)` makes those draws reappear.
  - The v6 Edit changes from themed `FFFFFF` MISS to `202020` COVERED while
    opted out, then returns exactly to `FFFFFF` after theme restoration.
    This demonstrates that the documented per-window theme opt-out is
    sufficient to expose the Edit to ColorFix's already-covered non-themed
    rendering path in the measured configuration.
  - The v6 push-button face changes from themed `FDFDFD` MISS to classic
    `F0F0F0` MISS while opted out, then returns exactly to `FDFDFD`. Theme
    opt-out therefore changes its rendering path but does not cover the button
    face; the separate classic-button boundary remains.
  - Static remains `2D2D2D` COVERED throughout themed, opted-out, and restored
    phases.
  - Exact pixel reversibility passed for every measured T surface in both
    capture modes. `SetWindowTheme` and restoration both returned `S_OK`, the
    T magenta sentinel remained valid, and the passive observer lost no events.
  - The 86x76 telemetry was interpretable in every run: Edit and Button draws
    were present before opt-out, absent during opt-out, and present again after
    restoration. This establishes the transition for the probe controls, not a
    product-wide policy for when theme opt-out should be applied.
- Button face causality (probe increment 9a, probe-only detours, one candidate
  at a time, control vs experiment of the same state, both load orders,
  x64/x86/ARM64; hover excluded as non-deterministic):
  - All candidate autotests passed (interception counted, marker or
    suppression effective), restoration was exact and the product hooks were
    still live afterwards, so the verdicts below are measurements.
  - USER32 classic button (window C), NORMAL/PRESSED/DISABLED/DEFAULTED: the
    face (`F0F0F0`) is caused by `FillRect` (marker reached the face center in
    both capture modes). `DrawEdge` is called but does not paint the face;
    `DrawFrameControl`, `PatBlt`, `ExtTextOutW` and `DrawThemeBackground` are
    not called. `FillRect` and `DrawEdge` draw on the button's own window DC
    (`WindowFromDC` matched 16/16).
  - comctl32 v6 button with theme opt-out (window T), all four states: the
    face is caused by `FillRect`, and suppressing `DrawFrameControl` also
    removes it. Consistent with `DrawFrameControl` filling the face through
    `FillRect`; that internal call chain is an inference.
  - comctl32 v6 themed button (window V), all four states: the face is caused
    by `DrawThemeBackground` class `Button`/`BP_PUSHBUTTON` (marker in both
    modes). Face colors: NORMAL/DEFAULTED `FDFDFD`, PRESSED `CCE4F7`,
    DISABLED `F9F9F9`. `FillRect` is called but is not causal; all draws use
    a memory DC (`WindowFromDC` 0).
- Classic button face: brush identity and product hook (probe increment 9b,
  runs 213 and 215, both load orders, x64/x86/ARM64):
  - Measured before any substitution: every `FillRect` call in the classic
    face paint receives exactly the original `GetSysColorBrush(COLOR_BTNFACE)`
    handle (index 15, `F0F0F0`), one identity only, in C (96 calls) and
    T opted out (80 calls), four states each, no samples dropped. No
    `COLOR_x + 1` value and no other brush was seen there.
  - Product rule (tested contract): the `FillRect` hook replaces a brush only
    when it is identical to an original system color brush handle whose role
    the mapper remaps, and only while the policy is active; the replacement is
    the same semantic brush `GetSysColorBrush` returns. Recognition is by
    handle identity, never by color: an application brush of the same color
    (`F0F0F0`), an unmapped role (`COLOR_HIGHLIGHT`), a stock brush and a
    ColorFix semantic brush all pass through; policy OFF and the High
    Contrast veto pass through.
  - `COLOR_x + 1` values and a NULL brush pass through untouched: the hooked
    result equals the unhooked call on a fresh DC with the same input. A NULL
    brush paints the DC's current brush (`FFFFFF` on a new memory DC), not
    nothing; run 214 failed on a wrong prediction of that native behavior,
    which is why the oracle is differential.
  - Visual result: the classic C face goes from `F0F0F0` to `2D2D2D` COVERED
    in both capture modes, and the opted-out T face from `F0F0F0` to `2D2D2D`
    COVERED; theme restoration still returns T exactly to its themed capture.
    Every policy step (OFF, ON, HC veto, FollowSystem light/dark) matched
    24/24 surfaces and the real-signal runtime steps passed, so the hook
    follows the effective state in both directions.
  - No regression: every surface previously COVERED, PRESERVED or VALID kept
    its verdict. The themed v6 button (window V) keeps `FDFDFD`/`CCE4F7`/
    `F9F9F9` and remains a MISS outside the scope of this increment
    (`DrawThemeBackground`); the passive-pixel invariant held.
  - The 9a characterization is unchanged in meaning with the product hook
    live: `FillRect` stays causal (marker) for C and T opted out, and
    `DrawFrameControl` suppression still removes the T face. Only the control
    face color changed (`2D2D2D`), by design.
  - Identity cache: 31 handles, 31 distinct, equal to the unhooked originals,
    identical after a rebuild and after the real-signal runtime phase
    (`AppsUseLightTheme` plus `ImmersiveColorSet` broadcast). Not measured:
    stability across `SetSysColors`, which that phase does not exercise.
  - Probe scenes (outside the autotests): 211 product substitutions and zero
    `COLOR_x + 1` values reached `FillRect`. This describes the probe windows
    only; real applications are not yet measured.
  - Probe architecture: the product owns `user32!FillRect`; the button-face
    observer is a probe-only tap inside the product hook (compiled out of the
    mod), so no target has two detours.

These results were reproduced by CI on x64, x86, and native ARM64. Increment 3 was squash-merged to `main` as `5cd97a1`; main CI run 48 was reported green on all three architectures.

Increment 9b evidence: runs 213 (measurement) and 215 (product hook, all
gates PASS, `exit=0` in all six processes). No notice-length warning was
emitted, so no annotation was truncated.

## 10. Requirements for future rendering mechanisms

Button-specific handling, menus, Common Controls v6, UxTheme, GDI+, DirectWrite,
Direct2D, and later mechanisms must all obey the same effective policy state.
No new rendering path may bypass the High Contrast override or modify a process
that policy says should remain unchanged.

Windhawk generation currently incorporates shared hooks from `hooks/`.
Syntax validation of the generated mod is not equivalent to runtime validation
inside Windhawk; keep that distinction explicit.

## 11. Windows compatibility and performance principles

- Prefer documented Windows APIs when they can produce the required result.
  Hooking exists to cover behavior that public APIs cannot retrofit into a
  legacy third-party process.
- Keep rendering-hook hot paths allocation-free and lock-free where practical.
  Do not read the registry, enumerate processes/windows, perform IPC, or
  recalculate process selection on every intercepted call.
- Cache process-selection state after safe initialization outside loader lock.
- High Contrast is inviolable and overrides every dark-mode policy.
- If a global Windows hook is used, chain with CallNextHookEx except where a
  specific hook contract demonstrably requires consuming the notification.
- Treat architecture matching as a hard injection constraint. Validate x86,
  x64, native ARM64, and later ARM64 emulation scenarios separately.
- Use DwmSetWindowAttribute with DWMWA_USE_IMMERSIVE_DARK_MODE for the Windows
  11 non-client frame/title bar where applicable. It is documented and covers
  a region the current GDI/USER32 color hooks do not. Its documented behavior
  honors the system dark-mode setting; future ForceDark behavior must be tested
  rather than assumed from this attribute.
- Keep undocumented UxTheme mechanisms, including DarkMode_* theme-class
  conventions and undocumented ordinals, isolated behind a replaceable
  boundary. They must not become dependencies of the core mapper/policy.
- Benchmark candidate injection models under equivalent workloads before
  selecting the standalone architecture. Global-hook convenience is not proof
  of lower system cost.
- Preserve the distinction between documented API behavior, measured ColorFix
  behavior, and implementation assumptions.

## 12. Maintenance rule

Update this document when an increment:

- changes policy semantics;
- proves or disproves a rendering-path assumption;
- introduces a new architecture/injection constraint;
- establishes a new accessibility or compatibility invariant; or
- turns a previously informational observation into a tested contract.

Prefer measured statements over assumptions, and label unverified expectations
as such.
