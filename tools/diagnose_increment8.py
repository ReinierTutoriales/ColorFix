from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path, old, new):
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected 1 anchor, got {n}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")

replace_once(
    "tests/probe/probe_hooks.cpp",
    "        const WindowShot now[3] = {Shoot(winE), Shoot(winK), Shoot(winC)};\n"
    "        const WindowShot* base[3] = {&baseE, &baseK, &baseC};\n"
    "        const WindowShot* hooked[3] = {&hookE, &hookK, &hookC};\n",
    "        const Counts runtimeBefore = Snapshot();\n"
    "        const WindowShot now[3] = {Shoot(winE), Shoot(winK), Shoot(winC)};\n"
    "        const Counts runtimeAfter = Snapshot();\n"
    "        const WindowShot* base[3] = {&baseE, &baseK, &baseC};\n"
    "        const WindowShot* hooked[3] = {&hookE, &hookK, &hookC};\n",
)

replace_once(
    "tests/probe/probe_hooks.cpp",
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

replace_once(
    "tests/probe/probe_hooks.cpp",
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

replace_once(
    "windhawk/colorfix.wh.template.cpp",
    "    // Hooks queued during Wh_ModInit are applied by Windhawk after it returns.\n"
    "    // Wh_ApplyHookOperations is only for hooks queued after initialization.\n"
    "    return colorfix::hooks::RegisterPhase1Hooks(WindhawkRegisterHook) ? TRUE : FALSE;\n",
    "    // Hooks queued during Wh_ModInit are applied by Windhawk after it returns.\n"
    "    // Wh_ApplyHookOperations is only for hooks queued after initialization.\n"
    "    if (!colorfix::hooks::RegisterPhase1Hooks(WindhawkRegisterHook)) {\n"
    "        // Windhawk won't call later callbacks when Wh_ModInit returns FALSE.\n"
    "        colorfix::runtime::StopListener();\n"
    "        return FALSE;\n"
    "    }\n"
    "    return TRUE;\n",
)

subprocess.run(["git", "add", "tests/probe/probe_hooks.cpp", "windhawk/colorfix.wh.template.cpp"], cwd=ROOT, check=True)
subprocess.run(["git", "commit", "-m", "test(policy): diagnose runtime dark transition"], cwd=ROOT, check=True)
