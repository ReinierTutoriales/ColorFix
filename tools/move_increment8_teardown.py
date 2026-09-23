from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = ROOT / "tests/probe/probe_hooks.cpp"
t = p.read_text(encoding="utf-8")
old = "    cfp::Publish(cfp::Mode::ForceDark, {});\n\n    MH_DisableHook(MH_ALL_HOOKS);\n    MH_Uninitialize();\n\n    // ------------------------------------------------------------ report\n"
new = "    cfp::Publish(cfp::Mode::ForceDark, {});\n\n    // ------------------------------------------------------------ report\n"
if t.count(old) != 1:
    raise SystemExit(f"teardown anchor count {t.count(old)}")
t = t.replace(old, new, 1)
old2 = "    std::printf(\"runtime: restore AppsUseLightTheme=%s %s\\n\",\n                origStatus == ERROR_SUCCESS ? (origLight ? \"1\" : \"0\") : \"absent\",\n                restored == ERROR_SUCCESS ? \"OK\" : \"INFRASTRUCTURE_FAILURE\");\n\n    const int code = "
new2 = "    std::printf(\"runtime: restore AppsUseLightTheme=%s %s\\n\",\n                origStatus == ERROR_SUCCESS ? (origLight ? \"1\" : \"0\") : \"absent\",\n                restored == ERROR_SUCCESS ? \"OK\" : \"INFRASTRUCTURE_FAILURE\");\n\n    // Phase E must run with the same installed hooks it is validating. Tear\n    // MinHook down only after the listener has stopped and the runner setting\n    // has been restored.\n    MH_DisableHook(MH_ALL_HOOKS);\n    MH_Uninitialize();\n\n    const int code = "
if t.count(old2) != 1:
    raise SystemExit(f"final anchor count {t.count(old2)}")
p.write_text(t.replace(old2, new2, 1), encoding="utf-8", newline="\n")
subprocess.run(["git", "add", "tests/probe/probe_hooks.cpp"], cwd=ROOT, check=True)
subprocess.run(["git", "commit", "-m", "fix(probe): keep hooks active through runtime phase"], cwd=ROOT, check=True)
