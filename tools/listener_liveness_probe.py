from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = ROOT / "tests/probe/probe_hooks.cpp"
t = p.read_text(encoding="utf-8")
anchor = "    const bool listenerOk = cfr::StartListener(cfp::Mode::FollowSystem);\n"
probe = r'''    auto printHookLive = [](const char* tag) {
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
        std::printf(" calls GetSysColor=%ld CreateSolidBrush=%ld\n",
                    Delta(a, b, HookId::GetSysColor),
                    Delta(a, b, HookId::CreateSolidBrush));
    };
    printHookLive("pre-listener");
    const bool listenerOk = cfr::StartListener(cfp::Mode::FollowSystem);
    printHookLive("post-listener");
'''
if t.count(anchor) != 1:
    raise SystemExit(f"anchor count {t.count(anchor)}")
p.write_text(t.replace(anchor, probe, 1), encoding="utf-8", newline="\n")
subprocess.run(["git", "add", "tests/probe/probe_hooks.cpp"], cwd=ROOT, check=True)
subprocess.run(["git", "commit", "-m", "test(policy): isolate listener hook liveness"], cwd=ROOT, check=True)
