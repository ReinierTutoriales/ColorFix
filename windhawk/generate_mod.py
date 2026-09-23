#!/usr/bin/env python3
"""Generate generated/colorfix.wh.cpp from shared headers and the Windhawk template."""
import re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORE = ROOT / "core"
HOOKS = ROOT / "hooks" / "colorfix_hooks.hpp"
TEMPLATE = ROOT / "windhawk" / "colorfix.wh.template.cpp"
OUTPUT = ROOT / "generated" / "colorfix.wh.cpp"
CORE_MARKER = "// @@COLORFIX_CORE@@"
HOOKS_MARKER = "// @@COLORFIX_HOOKS@@"

LOCAL_INC = re.compile(r'^\s*#\s*include\s+"([^"]+)"\s*$')
SYS_INC = re.compile(r'^\s*#\s*include\s+<([^>]+)>\s*$')
PRAGMA_ONCE = re.compile(r'^\s*#\s*pragma\s+once\s*$')

def ordered_headers():
    deps = {}
    for h in sorted(CORE.glob("*.hpp")):
        deps[h.name] = [
            m.group(1)
            for line in h.read_text(encoding="utf-8").splitlines()
            if (m := LOCAL_INC.match(line))
        ]
    order = []
    state = {}
    def visit(name, stack):
        if state.get(name) == "done": return
        if state.get(name) == "visiting":
            sys.exit(f"include cycle: {' -> '.join(stack + [name])}")
        if name not in deps:
            sys.exit(f"missing local core include: {name}")
        state[name] = "visiting"
        for dep in deps[name]:
            visit(dep, stack + [name])
        state[name] = "done"
        order.append(name)
    for name in deps:
        visit(name, [])
    return order

def flatten(path, sys_includes):
    body = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if PRAGMA_ONCE.match(line) or LOCAL_INC.match(line):
            continue
        if m := SYS_INC.match(line):
            if m.group(1) not in sys_includes:
                sys_includes.append(m.group(1))
            continue
        body.append(line)
    return body

def main():
    template = TEMPLATE.read_text(encoding="utf-8")
    for marker in (CORE_MARKER, HOOKS_MARKER):
        if template.count(marker) != 1:
            sys.exit(f"template must contain exactly one {marker}")

    sys_includes = []
    core_body = []
    order = ordered_headers()
    for name in order:
        core_body.append(f"// ---- core/{name} ----")
        core_body.extend(flatten(CORE / name, sys_includes))

    hooks_body = ["// ---- hooks/colorfix_hooks.hpp ----"]
    hooks_body.extend(flatten(HOOKS, sys_includes))

    includes = "\n".join(f"#include <{name}>" for name in sys_includes)
    core_block = "\n".join([
        "// GENERATED from core/ and hooks/ by windhawk/generate_mod.py. Do not edit.",
        includes,
        "",
        *core_body,
    ])
    hooks_block = "\n".join(hooks_body)

    out = template.replace(CORE_MARKER, core_block).replace(HOOKS_MARKER, hooks_block)
    OUTPUT.parent.mkdir(exist_ok=True)
    OUTPUT.write_text(out, encoding="utf-8", newline="\n")
    print(
        f"{OUTPUT.relative_to(ROOT)}: {len(out.splitlines())} lines, "
        f"core headers: {', '.join(order)}, hooks: {HOOKS.relative_to(ROOT)}"
    )

if __name__ == "__main__":
    main()
