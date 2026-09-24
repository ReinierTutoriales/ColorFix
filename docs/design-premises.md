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
- Button text and scoped theme opt-out (probe increment 9c, window U, runs
  227 push and 228 pull request, both load orders, x64/x86/ARM64;
  measurement only, no product change):
  - Opt-out scope: `SetWindowTheme(hwnd, L"", L"")` on the v6 push button
    alone takes its face from themed `FDFDFD` to `2D2D2D` COVERED, and
    `SetWindowTheme(hwnd, NULL, NULL)` restores it exactly. The themed Static
    and Edit siblings stay pixel-identical during and after. The 82x72
    `Button`/`BP_PUSHBUTTON` draws stop while opted out (4, 0, 5) and the
    `Edit`/`EP_BACKGROUND` draws continue (3, 3, 3). In the measured
    configuration the opt-out affects only the window it is applied to.
  - Text method: bold 28 px non-antialiased "MM". The glyph covers 582
    pixels in every configuration, so the text color is identified by that
    count, not by frequency rank. Contrast is the WCAG 2 ratio, reported as a
    finding; no threshold is a requirement.
  - Policy OFF: black text on `F0F0F0` (classic and opted out, ratio 18.4)
    and on the themed faces (`FDFDFD` 20.6, pressed `CCE4F7` 16.0).
  - Policy ON, themed v6 button: the face is unchanged (`FDFDFD`, pressed
    `CCE4F7`) while the text becomes `DCDCDC`, ratio 1.3 (pressed 1.0).
    ColorFix currently makes themed push-button text illegible. `DCDCDC` is
    `MapLiteralColor` of black; attributing it to the `SetTextColor` hook is
    an inference, not a measurement.
  - Policy ON, classic button: face `2D2D2D` and text `DCDCDC`, drawn over a
    `202020` block of 1470 pixels around the text (text to block 11.9):
    legible, with a visible artifact. `202020` is `MapLiteralColor(F0F0F0)`;
    an opaque text background mapped literally instead of semantically is
    inferred.
  - Policy ON, opted-out v6 button: face `2D2D2D`, text `DCDCDC`, ratio 10.0,
    no block.
  - DISABLED and DEFAULTED text varies between load orders (up to six
    colors); no conclusion is drawn from them.
  - The U classic button face matched window C (OFF `F0F0F0`, ON `2D2D2D`),
    as required for the classic measurement to stand for C's class.
  - Capture reliability: at (780,100)-(1100,300) U extended past the
    1024x768 runner monitor, and its captures were intermittently black,
    sentinel included, in runs 221 to 224. Moved on top and fully inside the
    monitor for this phase only, and hidden afterwards, U gave valid first
    captures and needed no retries in all twelve processes of runs 227 and
    228. This is a correlation, not a proven cause; a bounded sentinel retry
    remains as a detector.
- Themed button text causality (probe increment 9d, runs 235 push and 238 pull
  request, both load orders, x64/x86/ARM64; measurement only):
  - Run 235 intervened only at the product `SetTextColor` mapping while the
    U themed push button painted under policy ON. The mapped control was
    `FDFDFD` with `DCDCDC` text (582 pixels, contrast 1.3); bypassing that
    mapping changed only the text to `000000` (582 pixels, contrast 20.6);
    restoring the mapping returned exactly to the mapped capture. This proves
    that the product `SetTextColor` hook causes the increment-9c themed
    push-button text regression.
  - Run 238 measured 36 attributed `SetTextColor` calls across the three
    button captures: input histogram `000000` x24 and `DCDCDC` x12, or
    eight black plus four `DCDCDC` calls per capture. All 24 black calls
    occurred while the thread was inside `DrawThemeText`; none of the 12
    `DCDCDC` calls did. `DrawThemeTextEx` was not observed for this path.
    The source of the four `DCDCDC` inputs per capture is not measured.
    A possible source is an already-mapped `GetSysColor(COLOR_BTNTEXT)`,
    but that remains an inference.
  - A text-bearing themed v6 Static was added as a safety control. Its
    `SetTextColor` calls were outside `DrawThemeText`
    (`inside-theme-text=0`), and the DrawThemeText-scoped bypass did not
    change its visible result: `2D2D2D` background, `DCDCDC` text,
    582 pixels, contrast 10.0. This establishes only that the measured Static
    does not depend on the candidate DrawThemeText rule. Its visible text
    color is not proven to originate from the hooked `SetTextColor`; a
    direct `SetTextColor_Original` call from ctlcolor adjustment is a
    plausible but unmeasured explanation.
  - A process-wide rule that bypasses literal mapping inside
    `DrawThemeText*` is therefore not yet a product contract. Checkbox,
    radio-button and group-box labels can plausibly use themed text over a
    parent background that ColorFix has already darkened; passing native black
    through there could create the inverse contrast failure. Increment 9d did
    not contain those controls, so this is a prediction, not a measurement.
    Before changing product behavior, measure those variants and compare the
    DrawThemeText-scoped bypass with push-button-only theme opt-out. The latter
    is already measured to produce a `2D2D2D` push-button face with
    `DCDCDC` text (contrast 10.0), but restoration can conflict with an
    application's own `SetWindowTheme` choice and must be treated as a
    separate compatibility cost.

These results were reproduced by CI on x64, x86, and native ARM64. Increment 3 was squash-merged to `main` as `5cd97a1`; main CI run 48 was reported green on all three architectures.

Increment 9b evidence: runs 213 (measurement) and 215 (product hook, all
gates PASS, `exit=0` in all six processes). No notice-length warning was
emitted, so no annotation was truncated. Increment 9b was squash-merged to
`main` as `61ca15f`; main CI run 218 passed on all three architectures.

Increment 9c evidence: runs 227 (push) and 228 (pull request), `exit=0` in
all six processes of each, with the increment 9b gates unchanged.

Increment 9d evidence: run 235 (push) established the causal SetTextColor
intervention; run 238 (pull request on `9e5f505`) completed the call-context
and Static-control characterization. All six processes in run 238 ended with
`exit=0`, with the increment 9b and 9c gates unchanged.

- Button text candidate safety and product rule (probe increments 9e/9f and
  product run 294, both load orders, x64/x86/ARM64):
  - Increment 9e rejected a blanket DrawThemeText-scoped bypass. Checkbox and
    radio labels changed from `202020:DCDCDC` (582 text pixels, contrast
    11.9) to black text on `202020` (contrast 1.3); group-box text showed
    the same regression, with 694 `DCDCDC` pixels in the mapped capture (582
    glyph pixels plus, most likely, part of the frame inside the measured area;
    inferred).
  - Increment 9f measured the narrower rule: bypass literal SetTextColor
    mapping only inside DrawThemeText/DrawThemeTextEx for a known single-class
    `Button` HTHEME and part 1 (`BP_PUSHBUTTON`). Push NORMAL and PRESSED
    changed exactly 582 pixels from `DCDCDC` to `000000`; DISABLED changed
    zero pixels because its `838383` text is not mapped. Checkbox, radio,
    group box and the Static safety control changed zero pixels.
  - The shared-handle hypothesis was confirmed by the probe: the push button,
    checkbox, radio and group box used the same HTHEME. Therefore HTHEME
    classification is reference-counted across observed OpenThemeData* calls
    and successful CloseThemeData calls; forgetting on every close is invalid.
  - Product contract: ColorFix owns the OpenThemeData, OpenThemeDataForDpi,
    OpenThemeDataEx, CloseThemeData, DrawThemeText and DrawThemeTextEx detours.
    The probe observes those targets through COLORFIX_PROBE taps, so there is
    no second detour on the same target. The six UxTheme exports are resolved
    before registration; the enhancement is active only when the complete
    surface registered, and partial registration remains inert. ColorFix does
    not load uxtheme.dll to obtain this enhancement.
  - Run 294 is the product gate. In all six processes the UxTheme product
    autotest reported complete=1 and classifier+refcount PASS. The themed push
    button under policy ON rendered NORMAL/PRESSED text as `000000` x582
    over the unchanged `FDFDFD`/`CCE4F7` themed faces; DISABLED remained
    `838383`. Static remained `2D2D2D:DCDCDC`; checkbox and radio remained
    `202020:DCDCDC` x582 and group box `202020:DCDCDC` x694.
  - The final passive observer state is a gate, not an earlier snapshot.
    Run 294 retained every observed event (`dropped=0`) in all six processes,
    reported observer PASS, and ended `infrastructure=VALID hooks=PASS exit=0`.
    The increment 9b and 9c gates remained unchanged.
  - Unknown, ambiguous/multi-class and TLS-overflow contexts fail closed: they
    keep the ordinary ColorFix literal mapping rather than applying the
    push-button bypass. Handles opened before the product can observe their
    class are therefore not guessed.
  - The themed push button is now legible but not dark: its face stays
    `FDFDFD`/`CCE4F7`. Darkening it (theme opt-out, candidate (b)) remains a
    separate, unmeasured product decision because preserving application-owned
    `SetWindowTheme` state is unresolved.

Increment 9e/9f evidence: experimental runs 261 and 263 established the scoped
decision table and shared-HTHEME/refcount requirement. Product evidence: pull
request run 294 on `1fa5ad6`, all three architecture jobs and both load
orders successful, with complete product gates visible in the CI output.

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

## Increment 11a: palettes and literal-color roles

Decided in increment 11a (design; runtime evidence pending in CI and in the
Windhawk integration test):

- Mode (Disabled / ForceDark / FollowSystem) and palette (Default / AMOLED)
  are independent dimensions. The host reads both once and applies them as one
  configuration (`runtime::SetConfiguration`).
- Activity and palette are published in one atomic byte
  (`policy::g_state`). Hooks load it once per call and use that copy only.
- Repaint happens when activity changes, and also when the palette changes
  while ColorFix stays active.
- Literal colors are mapped by role. `MapLiteralText` (SetTextColor) and
  `MapLiteralFill` (SetBkColor, CreateSolidBrush) replace the shared
  `MapLiteralColor`, which no longer exists. Reason: with AMOLED, #000000 is
  both the semantic surface color and the most common literal text color.
- AMOLED invariants (compile-time, tests/core_check.cpp): text #000000 ->
  #DCDCDC; fill #000000 -> #000000; fill #FFFFFF -> #000000; every semantic
  color of the active palette is a fixed point of the fill path.
- The 9f exception keeps precedence over `MapLiteralText` in every palette.
- The Default palette is unchanged: text and fill mapping equal the pre-11a
  mapper for every grey and a 6x6x6 RGB lattice (frozen oracle in
  tests/core_check.cpp). CI evidence from 9b-9f therefore still applies.
- System brushes: one never-deleted bank per palette. The palette is captured
  before selecting/creating a brush, so a concurrent switch cannot publish a
  brush of one palette in the other palette's slot.
- Not assumed: that every AMOLED role is visible in real applications. That
  depends on which paint paths reach the hooks and must be measured.

## 12. Maintenance rule

Update this document when an increment:

- changes policy semantics;
- proves or disproves a rendering-path assumption;
- introduces a new architecture/injection constraint;
- establishes a new accessibility or compatibility invariant; or
- turns a previously informational observation into a tested contract.

Prefer measured statements over assumptions, and label unverified expectations
as such.
