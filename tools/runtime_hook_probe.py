from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = ROOT / "tests/probe/probe_hooks.cpp"
t = p.read_text(encoding="utf-8")
old = "        const bool after = cfp::Active();\n\n        const Counts runtimeBefore = Snapshot();\n"
new = "        const bool after = cfp::Active();\n\n        // Direct liveness oracle: prove the installed hooks still intercept after\n        // the real WM_SETTINGCHANGE transition, independently of repaint paths.\n        const Counts live0 = Snapshot();\n        const COLORREF liveSys = GetSysColor(COLOR_WINDOW);\n        HBRUSH liveBrush = CreateSolidBrush(kWhite);\n        const COLORREF liveCreated = BrushColor(liveBrush);\n        DeleteObject(liveBrush);\n        const Counts live1 = Snapshot();\n        std::printf(\"runtime-detail: hook-live active=%d sys=\", cfp::Active() ? 1 : 0);\n        PrintRgb(liveSys);\n        std::printf(\" brush=\");\n        PrintRgb(liveCreated);\n        std::printf(\" calls GetSysColor=%ld CreateSolidBrush=%ld\\n\",\n                    Delta(live0, live1, HookId::GetSysColor),\n                    Delta(live0, live1, HookId::CreateSolidBrush));\n\n        const Counts runtimeBefore = Snapshot();\n"
if t.count(old) != 1:
    raise SystemExit(f"anchor count {t.count(old)}")
p.write_text(t.replace(old, new, 1), encoding="utf-8", newline="\n")
subprocess.run(["git", "add", "tests/probe/probe_hooks.cpp"], cwd=ROOT, check=True)
subprocess.run(["git", "commit", "-m", "test(policy): probe hook liveness after runtime signal"], cwd=ROOT, check=True)
