from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def replace_once(path, old, new):
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{path}: anchor count {n}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")

# Remove temporary hook-liveness diagnostics around listener startup.
replace_once(
    "tests/probe/probe_hooks.cpp",
    '''    auto printHookLive = [](const char* tag) {
        const Counts a = Snapshot();
        const COLORREF sys = GetSysColor(COLOR_WINDOW);
        HBRUSH brush = CreateSolidBrush(kWhite);
        const COLORREF made = BrushColor(brush);
        DeleteObject(brush);
        const Counts b = Snapshot();
        std::printf("runtime-detail: hook-live-%s active=%d sys=", tag, cfp::Active() ? 1 : 0);
        PrintRgb(sys);
        std::printf(" brush=");
        PrintRgb(made);
        std::printf(" calls GetSysColor=%ld CreateSolidBrush=%ld\\n",
                    Delta(a, b, HookId::GetSysColor),
                    Delta(a, b, HookId::CreateSolidBrush));
    };
    printHookLive("pre-listener");
    const bool listenerOk = cfr::StartListener(cfp::Mode::FollowSystem);
    printHookLive("post-listener");
''',
    '''    const bool listenerOk = cfr::StartListener(cfp::Mode::FollowSystem);
''')

# Remove direct hook liveness oracle from each runtime step.
replace_once(
    "tests/probe/probe_hooks.cpp",
    '''        // Direct liveness oracle: prove the installed hooks still intercept after
        // the real WM_SETTINGCHANGE transition, independently of repaint paths.
        const Counts live0 = Snapshot();
        const COLORREF liveSys = GetSysColor(COLOR_WINDOW);
        HBRUSH liveBrush = CreateSolidBrush(kWhite);
        const COLORREF liveCreated = BrushColor(liveBrush);
        DeleteObject(liveBrush);
        const Counts live1 = Snapshot();
        std::printf("runtime-detail: hook-live active=%d sys=", cfp::Active() ? 1 : 0);
        PrintRgb(liveSys);
        std::printf(" brush=");
        PrintRgb(liveCreated);
        std::printf(" calls GetSysColor=%ld CreateSolidBrush=%ld\\n",
                    Delta(live0, live1, HookId::GetSysColor),
                    Delta(live0, live1, HookId::CreateSolidBrush));

        const Counts runtimeBefore = Snapshot();
        const WindowShot now[3] = {Shoot(winE), Shoot(winK), Shoot(winC)};
        const Counts runtimeAfter = Snapshot();
''',
    '''        const WindowShot now[3] = {Shoot(winE), Shoot(winK), Shoot(winC)};
''')

# Restore the concise visual gate loop.
replace_once(
    "tests/probe/probe_hooks.cpp",
    '''                ++total;
                const bool surfaceOk =
                    !ref.mode[m].px.empty() && !now[wi].mode[m].px.empty() &&
                    SurfaceColor(ref.mode[m], s.rect, &want) &&
                    SurfaceColor(now[wi].mode[m], s.rect, &got) && want == got;
                if (surfaceOk) ++match;
                std::printf("runtime-detail: %-18s mode=%s want=", s.name,
                            m == 0 ? "flags0" : "full");
                PrintRgb(want);
                std::printf(" got=");
                PrintRgb(got);
                std::printf(" %s\\n", surfaceOk ? "MATCH" : "MISS");
''',
    '''                ++total;
                if (!ref.mode[m].px.empty() && !now[wi].mode[m].px.empty() &&
                    SurfaceColor(ref.mode[m], s.rect, &want) &&
                    SurfaceColor(now[wi].mode[m], s.rect, &got) && want == got)
                    ++match;
''')

# Remove temporary counter diagnostics while preserving contractual gates.
replace_once(
    "tests/probe/probe_hooks.cpp",
    '''        const bool visualOk = match == total;
        std::printf("runtime-detail: hook-delta GetSysColor=%ld GetSysColorBrush=%ld "
                    "GetStockObject=%ld SetTextColor=%ld SetBkColor=%ld "
                    "CreateSolidBrush=%ld DefWindowProcErase=%ld DefWindowProcCtlColor=%ld\\n",
                    Delta(runtimeBefore, runtimeAfter, HookId::GetSysColor),
                    Delta(runtimeBefore, runtimeAfter, HookId::GetSysColorBrush),
                    Delta(runtimeBefore, runtimeAfter, HookId::GetStockObject),
                    Delta(runtimeBefore, runtimeAfter, HookId::SetTextColor),
                    Delta(runtimeBefore, runtimeAfter, HookId::SetBkColor),
                    Delta(runtimeBefore, runtimeAfter, HookId::CreateSolidBrush),
                    Delta(runtimeBefore, runtimeAfter, HookId::DefWindowProcErase),
                    Delta(runtimeBefore, runtimeAfter, HookId::DefWindowProcCtlColor));
''',
    '''        const bool visualOk = match == total;
''')

# Replace obsolete policy-wiring debt with the measured runtime contract.
replace_once(
    "docs/design-premises.md",
    '''### Open debt: policy is not wired to real signals

The policy (modes, High Contrast veto, single atomic effective state) is
implemented and verified dynamically by probe increment 4, but nothing calls
`ReadWindowsSignals`/`Publish` yet and no `WM_SETTINGCHANGE` handler exists.
At runtime ColorFix therefore still behaves as a fixed `ForceDark`. Wiring the
policy to real signals is a prerequisite for any use of ColorFix outside the
test environment.
''',
    '''### Runtime policy wiring

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
''')

subprocess.run(["git", "add", "tests/probe/probe_hooks.cpp", "docs/design-premises.md"], cwd=ROOT, check=True)
subprocess.run(["git", "commit", "-m", "docs(policy): record runtime wiring evidence"], cwd=ROOT, check=True)
