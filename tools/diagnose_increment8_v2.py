from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = ROOT / "tests/probe/probe_hooks.cpp"
text = p.read_text(encoding="utf-8")
marker = "    // ------------------------------------------------ Phase E: runtime wiring\n"
if text.count(marker) != 1:
    raise SystemExit("Phase E marker not unique")
pre, phase = text.split(marker, 1)

def rep(old, new):
    global phase
    n = phase.count(old)
    if n != 1:
        raise SystemExit(f"phase-E anchor expected 1, got {n}: {old[:60]!r}")
    phase = phase.replace(old, new, 1)

rep(
    "        const WindowShot now[3] = {Shoot(winE), Shoot(winK), Shoot(winC)};\n"
    "        const WindowShot* base[3] = {&baseE, &baseK, &baseC};\n",
    "        const Counts runtimeBefore = Snapshot();\n"
    "        const WindowShot now[3] = {Shoot(winE), Shoot(winK), Shoot(winC)};\n"
    "        const Counts runtimeAfter = Snapshot();\n"
    "        const WindowShot* base[3] = {&baseE, &baseK, &baseC};\n",
)
rep(
    "                ++total;\n"
    "                if (!ref.mode[m].px.empty() && !now[wi].mode[m].px.empty() &&\n"
    "                    SurfaceColor(ref.mode[m], s.rect, &want) &&\n"
    "                    SurfaceColor(now[wi].mode[m], s.rect, &got) && want == got)\n"
    "                    ++match;\n",
    "                ++total;\n"
    "                const bool surfaceOk =\n"
    "                    !ref.mode[m].px.empty() && !now[wi].mode[m].px.empty() &&\n"
    "                    SurfaceColor(ref.mode[m], s.rect, &want) &&\n"
    "                    SurfaceColor(now[wi].mode[m], s.rect, &got) && want == got;\n"
    "                if (surfaceOk) ++match;\n"
    "                std::printf(\"runtime-detail: %-18s mode=%s want=\", s.name,\n"
    "                            m == 0 ? \"flags0\" : \"full\");\n"
    "                PrintRgb(want);\n"
    "                std::printf(\" got=\");\n"
    "                PrintRgb(got);\n"
    "                std::printf(\" %s\\n\", surfaceOk ? \"MATCH\" : \"MISS\");\n",
)
rep(
    "        const bool visualOk = match == total;\n"
    "        if (!received) infraOk = false;\n",
    "        const bool visualOk = match == total;\n"
    "        std::printf(\"runtime-detail: hook-delta GetSysColor=%ld GetSysColorBrush=%ld \"\n"
    "                    \"GetStockObject=%ld SetTextColor=%ld SetBkColor=%ld \"\n"
    "                    \"CreateSolidBrush=%ld DefWindowProcErase=%ld DefWindowProcCtlColor=%ld\\n\",\n"
    "                    Delta(runtimeBefore, runtimeAfter, HookId::GetSysColor),\n"
    "                    Delta(runtimeBefore, runtimeAfter, HookId::GetSysColorBrush),\n"
    "                    Delta(runtimeBefore, runtimeAfter, HookId::GetStockObject),\n"
    "                    Delta(runtimeBefore, runtimeAfter, HookId::SetTextColor),\n"
    "                    Delta(runtimeBefore, runtimeAfter, HookId::SetBkColor),\n"
    "                    Delta(runtimeBefore, runtimeAfter, HookId::CreateSolidBrush),\n"
    "                    Delta(runtimeBefore, runtimeAfter, HookId::DefWindowProcErase),\n"
    "                    Delta(runtimeBefore, runtimeAfter, HookId::DefWindowProcCtlColor));\n"
    "        if (!received) infraOk = false;\n",
)
p.write_text(pre + marker + phase, encoding="utf-8", newline="\n")

q = ROOT / "windhawk/colorfix.wh.template.cpp"
t = q.read_text(encoding="utf-8")
old = (
    "    // Hooks queued during Wh_ModInit are applied by Windhawk after it returns.\n"
    "    // Wh_ApplyHookOperations is only for hooks queued after initialization.\n"
    "    return colorfix::hooks::RegisterPhase1Hooks(WindhawkRegisterHook) ? TRUE : FALSE;\n"
)
new = (
    "    // Hooks queued during Wh_ModInit are applied by Windhawk after it returns.\n"
    "    // Wh_ApplyHookOperations is only for hooks queued after initialization.\n"
    "    if (!colorfix::hooks::RegisterPhase1Hooks(WindhawkRegisterHook)) {\n"
    "        // Windhawk won't call later callbacks when Wh_ModInit returns FALSE.\n"
    "        colorfix::runtime::StopListener();\n"
    "        return FALSE;\n"
    "    }\n"
    "    return TRUE;\n"
)
if t.count(old) != 1:
    raise SystemExit("Windhawk init anchor not unique")
q.write_text(t.replace(old, new, 1), encoding="utf-8", newline="\n")

subprocess.run(["git", "add", str(p.relative_to(ROOT)), str(q.relative_to(ROOT))], cwd=ROOT, check=True)
subprocess.run(["git", "commit", "-m", "test(policy): diagnose runtime dark transition"], cwd=ROOT, check=True)
