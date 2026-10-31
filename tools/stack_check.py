# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""The stacks' worst case, from the device build's disassembly (build/grimm.dis: tools/build.py's objdump -d, .text
and .ram_text). Each function's frame: its prologue's push ({rets, r15-r4}: 4 B a register) and the deepest its sp
goes after (sp += -N, read straight down); each call's callee then on top of the deepest frame. The calls through a
pointer, by where they are: an engine's block (the audio ISR), begin (voice_begin), text (app_main); a drawing
(ui_region's callback); the flash driver's RAM code (fl_write, st_read, ...). Two stacks (firmware/app.ld):
  user stack   the main loop: fm1_cstart and app_main and all they call, under _ustack_top
  system stack the interrupts: isr_timer5 and isr_alnk0 (the audio), each as fm1_isr.S has them (8 + 4 registers
               pushed), both at once as the worst (one nested in the other)
    python3 tools/stack_check.py [DIS] [--min-margin BYTES]   prints both; exit 1 if a margin is under BYTES"""
import re
import sys

dis = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else "build/grimm.dis"
MIN = int(sys.argv[sys.argv.index("--min-margin") + 1]) if "--min-margin" in sys.argv else 0
USTACK = 0x01C7A000 - 0x01C74100                # (firmware/app.ld: _ustack_top - _ustack_lo)
SSTACK = 0x01C7C000 - 0x01C7A100                # (_sstack_top - _sstack_lo)

funcs, cur = {}, None
for line in open(dis, encoding="utf-8", errors="replace"):
    m = re.match(r"^([A-Za-z_][\w.]*):$", line.strip())
    if m and not line.startswith(" "):
        cur = m.group(1)
        funcs.setdefault(cur, [])
        continue
    m = re.match(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} )+\s*(.*)$", line)
    if m and cur:
        funcs[cur].append(m.group(1).strip())


def regs(spec):                                 # {rets, r15-r4, r9, ...}: how many
    n = 0
    for p in spec.split(","):
        p = p.strip()
        m = re.match(r"r(\d+)-r(\d+)", p)
        n += abs(int(m.group(1)) - int(m.group(2))) + 1 if m else 1
    return n


frame, calls, ind = {}, {}, {}
for f, ins in funcs.items():
    depth = deep = 0
    cs, ic = set(), 0
    for i in ins:
        m = re.search(r"\[--sp\] = \{([^}]*)\}", i)
        if m:
            depth += 4 * regs(m.group(1))
        m = re.search(r"\{([^}]*)\} = \[sp\+\+\]", i)
        if m:
            depth -= 4 * regs(m.group(1))
        m = re.search(r"\bsp \+= (-?\d+)", i)
        if m:
            depth -= int(m.group(1))
        deep = max(deep, depth)
        m = re.search(r"\bcall -?\d+ <([\w.]+)", i)
        if m:
            cs.add(m.group(1))
        elif re.search(r"\bcall r\d+", i):
            ic += 1
    frame[f], calls[f], ind[f] = deep, cs, ic

ENG = [f for f in funcs if re.fullmatch(r"(va|fm|sampler|formant|choir|wheel|phys|phase|pd)_block", f)]
BEG = [f for f in funcs if re.fullmatch(r"\w+_begin", f) and f != "voice_begin"]
TXT = [f for f in funcs if re.fullmatch(r"(va|fm|sampler|formant|wheel|phys|phase)_text", f)]
DRAW = [f for f in funcs if re.search(r"draw|^pic_", f)]
RAMT = [f for f in funcs if f.startswith(("rt_flash", "fl_", "spi_", "flash_")) and not calls[f]]
TARGETS = {"app_audio": ENG, "voice_begin": BEG, "app_main": TXT, "ui_region": DRAW,
           "fl_write": RAMT, "st_read": RAMT, "st_write": RAMT, "ota_erase": RAMT, "sample_flash_erase": RAMT,
           "settings_save": RAMT}
unknown = [f for f in funcs if ind[f] and f not in TARGETS and f != "fm1_cstart"]
memo, path = {}, {}


def worst(f, stack=()):
    if f in memo:
        return memo[f]
    if f in stack or f not in frame:
        return 0                                # (a cycle: counted once)
    best, via = 0, None
    for c in calls[f] | (set(TARGETS.get(f, [])) if ind[f] else set()):
        w = worst(c, stack + (f,))
        if w > best:
            best, via = w, c
    memo[f], path[f] = frame[f] + best, via
    return memo[f]


def chain(f):
    out = []
    while f:
        out.append(f"{f} {frame.get(f, 0)}")
        f = path.get(f)
    return " > ".join(out)


u = frame.get("fm1_cstart", 0) + worst("app_main")
isr = {n: 4 * (3 + 4) + worst(b) for n, b in (("isr_timer5", "fm1_timer5_irq"), ("isr_alnk0", "fm1_alnk0_irq"))}
s = sum(isr.values())
print(f"stack check ({dis}):")
print(f"  user stack:   worst {u} B of {USTACK} ({USTACK - u} B free)   {chain('app_main')}")
for n, b in (("isr_timer5", "fm1_timer5_irq"), ("isr_alnk0", "fm1_alnk0_irq")):
    print(f"  {n}: {isr[n]} B   {chain(b)}")
print(f"  system stack: worst {s} B of {SSTACK} (both interrupts nested; {SSTACK - s} B free)")
if unknown:
    print("  calls through a pointer not accounted for in: " + ", ".join(unknown))
bad = USTACK - u < MIN or SSTACK - s < MIN or unknown
if MIN:
    print(f"  {'FAIL' if bad else 'ok  '}  stack margins at least {MIN} B")
sys.exit(1 if bad else 0)
