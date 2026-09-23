#!/usr/bin/env python3
"""Generate generated/colorfix.wh.cpp from core/*.hpp and the Windhawk template."""
import re, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
CORE=ROOT/"core"; TEMPLATE=ROOT/"windhawk"/"colorfix.wh.template.cpp"
OUTPUT=ROOT/"generated"/"colorfix.wh.cpp"; MARKER="// @@COLORFIX_CORE@@"
LOCAL_INC=re.compile(r'^\s*#\s*include\s+"([^"]+)"\s*$')
SYS_INC=re.compile(r'^\s*#\s*include\s+<([^>]+)>\s*$')
PRAGMA_ONCE=re.compile(r'^\s*#\s*pragma\s+once\s*$')
def ordered_headers():
    deps={}
    for h in sorted(CORE.glob("*.hpp")):
        deps[h.name]=[m.group(1) for line in h.read_text(encoding="utf-8").splitlines() if (m:=LOCAL_INC.match(line))]
    order=[]; state={}
    def visit(name,stack):
        if state.get(name)=="done": return
        if state.get(name)=="visiting": sys.exit(f"include cycle: {' -> '.join(stack+[name])}")
        if name not in deps: sys.exit(f"missing local core include: {name}")
        state[name]="visiting"
        for d in deps[name]: visit(d,stack+[name])
        state[name]="done"; order.append(name)
    for name in deps: visit(name,[])
    return order
def main():
    template=TEMPLATE.read_text(encoding="utf-8")
    if template.count(MARKER)!=1: sys.exit(f"template must contain exactly one {MARKER}")
    sys_includes=[]; body=[]
    order=ordered_headers()
    for name in order:
        body.append(f"// ---- core/{name} ----")
        for line in (CORE/name).read_text(encoding="utf-8").splitlines():
            if PRAGMA_ONCE.match(line) or LOCAL_INC.match(line): continue
            if m:=SYS_INC.match(line):
                if m.group(1) not in sys_includes: sys_includes.append(m.group(1))
                continue
            body.append(line)
    block="\n".join([f"#include <{i}>" for i in sys_includes]+[""]+body)
    out=template.replace(MARKER,"// GENERATED from core/ by windhawk/generate_mod.py. Do not edit.\n"+block)
    OUTPUT.parent.mkdir(exist_ok=True)
    OUTPUT.write_text(out,encoding="utf-8",newline="\n")
    print(f"{OUTPUT.relative_to(ROOT)}: {len(out.splitlines())} lines, headers: {', '.join(order)}")
if __name__=="__main__": main()
