#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Target-side cost estimate of the hot DSP code, from the pi32v2 objdump listing tools/build.py writes
(build/melodee.dis). For each engine's render function and the audio ISR: the instructions
inside natural control-flow loops (their heads dominate their backedges), and the hardware divides
and calls in them. Unknown control flow retains the conservative backward-span estimate.
cost = the loop instructions, each weighted 4 per
level of nesting (an inner loop runs several times per sample) and 1 + 8 for a divide (many cycles). A static count, not a cycle count: it changes only when the compiled code changes,
so it is exact run to run, and it catches a render loop that grew (more work per sample, a new divide,
something not inlined any more). Compared with tests/target_budget.txt at +10 %. The host
CPU check (regress.c) is the primary one; this one sees the target compiler's code.
  tests/target_budget.py [DIS [BUDGET]]      BUDGET_UPDATE=1 rewrites BUDGET"""
import os
import re
import sys

FUNCS = ["phase_render", "sid_render", "cz_native_render", "cz_native_wave", "formant_render",
         "trio_render", "trio_pass", "wheel_render", "wheel_block",
         "phys_render", "drum_render", "noise_render", "fm6_render", "p5_samples", "p5_mod_samples", "px_modal_block", "px_modal_run", "px_memb_block",
         "px_string_excite", "px_string_run", "px_symp_run",
         "dr_bd", "dr_sd", "dr_tom", "dr_rs", "dr_cl", "dr_cp", "dr_ma", "dr_metal", "dr_cb", "dr_cy", "dr_hat", "dr8_run",   # drum_808.c
         "drum_source", "drum_limit", "drum_color", "dr_metal_tune",
         "d9_render_bd", "d9_render_sd", "d9_render_tom", "d9_render_rim", "d9_render_clap", "d9_render_smp",
                                   # SLICE (eng_slice.c): the render, the reverse windows
         "fm1_alnk0_irq", "track_render_audio",       # per-track renderer has its own bounded function
         "midi_parameter", "audition_tick", "click_render", # bounded optional recording/performance paths
         "mod_begin", "mod_voice", "mod_end",                 # the modulation matrix (mod.c), called when active
         "fx_filter", "fx_down", "fx_up", "fx_time", "mix_fx_samples",
         "fx_tape",                                        # fused, smoothed stereo Tape path
         "fx_reverb",                                      # reduced-rate tank and input conversion
         "fx_chorus",                                      # full-rate integer stereo taps
         "mix_extra_sends", "fx_bus_kernel",                # optional independent sends and returns
         ]
OPTIONAL = {}                   # built only with an option, not in BUDGET: absent, skipped; present, checked against these
# The FM6 operator kernel may be extracted from its renderer. Keep the original
# combined budget, so moving work into a helper cannot weaken the existing guard.
EXTRACTED = {
    "cz_native_wave": ("cz_native_wave_window4",),
    "sid_render": ("sid_render_into",),
    "drum_render": ("drum_render_into",),
    "dr8_run": ("dr8_run_into",),
    "fm6_render": ("fm6_run", "fm6_run_into", "fm6_output", "fm6_join"),
    "fm1_alnk0_irq": ("mix_block", "mix_part", "mix_prepare_batch", "audio_batch_flush", "mix_mono_samples", "master_samples", "master_out"),
    "track_render_audio": ("fm6_dc_samples", "p5_dc_samples"),
}
KERNEL_NAMES = {n for names in EXTRACTED.values() for n in names}
FEATURE_OPTIONAL = {"p5_samples", "p5_mod_samples"}  # MELODEE_PROPHET=0 omits these
TOL = 0.10                      # exact (no noise): small edits pass, a grown render loop does not
DIV_W = 8                       # a divide weighs 1 + 8 instructions
NEST = 4                        # an instruction in a loop inside a loop weighs 4, two deep 16, ...
MAXD = 4

dis = sys.argv[1] if len(sys.argv) > 1 else "build/melodee.dis"
budget = sys.argv[2] if len(sys.argv) > 2 else "tests/target_budget.txt"
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9.]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >")


def functions(path):
    """{name: [(addr, text)]}: a function runs to the next label not starting with '.' (the compiler's
    jump-table labels are inside functions); jump-table data ('< n : 0x.. >') is left out"""
    out, cur = {}, None
    with open(path) as f:
        for line in f:
            m = LABEL.match(line.strip())
            if m:
                name = m.group(1)
                if not name.startswith("."):
                    cur = out.setdefault(name, []) if name in FUNCS or name in KERNEL_NAMES else None
                continue
            m = INSN.match(line)
            if cur is not None and m and "<unknown instruction>" in m.group(3):
                raise SystemExit("target: undecoded instruction; disassemble with -mcpu=r3 -mattr=+fprev1")
            if cur is not None and m and not m.group(3).lstrip().startswith("<"):
                cur.append((int(m.group(1), 16), m.group(3).strip()))
    return out


def loop_members(insns):
    """A backward jump closes a loop only if its head dominates its tail.

    Compiler layout may put an alternative branch after its merge point. Such
    backward jumps do not repeat work and must not add loop nesting. Unknown
    indirect branches retain the old conservative estimate for that function.
    """
    index = {addr: i for i, (addr, _) in enumerate(insns)}
    successors = [set() for _ in insns]
    predecessors = [set() for _ in insns]
    unknown = False
    def conservative():
        heads = {}
        for addr, text in insns:
            branch = TARGET.search(text)
            if branch:
                head = int(branch.group(1), 16)
                if insns[0][0] <= head < addr:
                    heads[head] = max(heads.get(head, addr), addr)
        return [{i for i, (addr, _) in enumerate(insns) if head <= addr <= tail}
                for head, tail in heads.items()]
    for i, (addr, text) in enumerate(insns):
        branch = TARGET.search(text)
        if branch:
            target = int(branch.group(1), 16)
            if target in index:
                successors[i].add(index[target])
            else:
                unknown = True
        elif re.search(r"\bgoto\b", text):
            unknown = True
        returns = text.startswith(("{pc,", "pc =", "rts"))
        if i + 1 < len(insns) and not text.startswith("goto ") and not returns:
            successors[i].add(i + 1)
        for target in successors[i]: predecessors[target].add(i)
    if unknown:
        return conservative()
    reachable = {0}
    pending = [0]
    while pending:
        for target in successors[pending.pop()] - reachable:
            reachable.add(target)
            pending.append(target)
    dominators = {i: set(reachable) for i in reachable}
    dominators[0] = {0}
    changed = True
    while changed:
        changed = False
        for i in sorted(reachable - {0}):
            parents = predecessors[i] & reachable
            value = {i} | set.intersection(*(dominators[p] for p in parents))
            if value != dominators[i]:
                dominators[i] = value
                changed = True
    edges = {(tail, head) for tail in reachable for head in successors[tail]
             if head in dominators[tail]}
    # A reducible control-flow graph is acyclic after its natural backedges
    # are removed. Keep the old estimate for irreducible loops as well.
    degree = {i: len({p for p in predecessors[i] & reachable if (p, i) not in edges})
              for i in reachable}
    pending = [i for i, d in degree.items() if not d]
    visited = 0
    while pending:
        node = pending.pop()
        visited += 1
        for target in successors[node]:
            if (node, target) in edges: continue
            degree[target] -= 1
            if not degree[target]: pending.append(target)
    if visited != len(reachable): return conservative()
    loops = {}
    for tail, head in edges:
        members = {head, tail}
        pending = [tail] if head != tail else []
        while pending:
            for parent in predecessors[pending.pop()] & reachable:
                if parent not in members:
                    members.add(parent)
                    pending.append(parent)
        loops.setdefault(head, set()).update(members)
    return list(loops.values())


def cost(insns):
    """One natural loop per head; an instruction in d loops weighs NEST ** (d - 1).
    Loop members can lie after their latch in the compiler's physical layout.
    """
    loops = loop_members(insns)
    n = divs = calls = 0
    w = 0
    for i, (a, t) in enumerate(insns):
        d = min(sum(i in members for members in loops), MAXD)
        if not d:
            continue
        k = NEST ** (d - 1)
        n += 1
        if re.search(r"= r\d+ / r\d+", t):
            divs += 1
            k *= 1 + DIV_W
        calls += t.startswith("call")
        w += k
    return {"insns": len(insns), "loop": n, "div": divs, "call": calls, "cost": w}


def main():
    if not os.path.exists(dis):
        print(f"target: skip ({dis} missing: run ./build.sh)")
        return 0
    fns = functions(dis)
    res = {n: cost(fns[n]) for n in FUNCS if fns.get(n)}
    # A wrapper may inline away; keep its old budget identity and include
    # all extracted work rather than reporting a missing or zero-cost render.
    for parent,kernels in EXTRACTED.items():
        if parent not in res and any(fns.get(k) for k in kernels):
            res[parent]={key:0 for key in ("insns","loop","div","call","cost")}
    for parent, kernels in EXTRACTED.items():
        if parent not in res: continue
        for kernel in kernels:
            if fns.get(kernel):
                for key, value in cost(fns[kernel]).items(): res[parent][key] += value
    retired = {"trio_render", "trio_pass", "wheel_render", "wheel_block", "phys_render",
               "phase_render", "formant_render", "noise_render",
               "px_modal_block", "px_modal_run", "px_memb_block", "px_string_excite",
               "px_string_run", "px_symp_run"}
    disabled = set()
    missing = [n for n in FUNCS if n not in res and n not in OPTIONAL and n not in FEATURE_OPTIONAL and n not in retired and n not in disabled]
    if disabled: print("target: resident audio profile has delay/reverb disabled; their absent kernels are excluded")
    base = {}
    if os.path.exists(budget):
        for line in open(budget):
            p = line.split()
            if len(p) >= 2 and not line.startswith("#"):
                base[p[0]] = int(p[1])
    if os.environ.get("BUDGET_UPDATE"):
        with open(budget, "w") as f:
            f.write("# MELODEE target cost budget (tests/target_budget.py): instructions in the loops of each\n"
                    f"# function in build/melodee.dis, x{NEST} per nesting level, divides x{1 + DIV_W}. The check allows "
                    f"+{TOL * 100:.0f} %.\n# Rewritten by BUDGET_UPDATE=1.\n")
            for n, r in res.items():
                if n not in OPTIONAL:
                    f.write(f"{n} {r['cost']}\n")
        print(f"target: budget {budget} rewritten ({len(res)} functions)")
    fail = 0
    for n in missing:
        print(f"target: FAIL {n} not found in {dis} (renamed? inlined? update FUNCS)")
        fail += 1
    for n, r in res.items():
        b = base.get(n, OPTIONAL.get(n))
        state = "no budget (BUDGET_UPDATE=1 adds it)" if b is None else "ok"
        if b is not None and not os.environ.get("BUDGET_UPDATE"):
            if r["cost"] > b * (1 + TOL):
                state = (f"OVER BUDGET (+{(r['cost'] / b - 1) * 100:.0f} %, limit +{TOL * 100:.0f} %)"
                         if b else "OVER BUDGET (previously no loop)")
                fail += 1
            elif r["cost"] < b * (1 - TOL):
                state = f"note: {(r['cost'] / b - 1) * 100:.0f} % (BUDGET_UPDATE=1 to keep it)"
        print(f"target: {n:15s} {r['insns']:5d} instructions, {r['loop']:4d} in loops, {r['div']} divides, "
              f"{r['call']:2d} calls there: cost {r['cost']:5d} (budget {b if b is not None else '-'}) {state}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
