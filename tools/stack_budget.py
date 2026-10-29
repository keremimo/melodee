#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Check linked stack frames and optimized direct/indirect call paths.

No runtime/watermark claim: this is a build-time conservative estimate. Known
engine fields and worker registries constrain indirect calls. Unexpected dynamic
frames, unresolved callbacks and recursive call paths fail the audit. CPU0 IRQ
budget includes a nested TIMER5 and CPU0 fault path; CPU1 has IRQs disabled and
its fault handler returns before the display/CPU0 fault path.
"""
import argparse
import json
import re
from pathlib import Path
from audio_ram import FUNCTION, REFERENCE, call_arguments


def analyze(ir, dis, src):
    seq = (src / 'seq.c').read_text()
    usb = (src / 'usb_host.c').read_text()
    if not ("if (song.playing || cin_left" in seq and
            "if (started) seq_advance(0);" in seq and
            "#define UH_HUB_LEVELS 2u" in usb and
            "return depth < UH_HUB_LEVELS ? uh_hub_probe" in usb and
            "uh_probe((uint8_t)(level + 1u), m)" in usb):
        raise ValueError(
            'review stack re-entry bounds after transport/USB changes')
    source = '\n'.join(p.read_text()
                       for p in [*src.glob('*.c'), *src.glob('*.h')])
    worker_entries = set(re.findall(r'audio_worker_submit\((\w+)', source))
    batch_entries = set(re.findall(
        r'audio_batch_push(?:_cost)?\((\w+)', source))
    fns = {m[2]: m for m in FUNCTION.finditer(ir)}
    asm = {}
    cur = None
    for line in dis.splitlines():
        m = re.match(r'^([A-Za-z_][\w.]*):$', line)
        if m and not m[1].startswith('.'):
            cur = m[1]
            asm[cur] = []
        elif cur:
            asm[cur].append(line)
    frames = {}
    direct = {}
    dynamic = []
    for name, lines in asm.items():
        total = 0
        calls = set()
        for l in lines:
            m = re.search(r'sp \+= -(\d+)', l)
            if m:
                total += int(m[1])
            m = re.search(r'\[--sp\] = \{([^}]+)\}', l)
            if m:
                for r in m[1].split(','):
                    p = re.match(r'r(\d+)-r(\d+)', r.strip())
                    total += 4*(abs(int(p[1])-int(p[2]))+1) if p else 4
            if re.search(r'sp (?:\+=|-=|=).*r\d', l):
                dynamic.append(name)
            m = re.search(r'\bcall -?\d+ <([\w.]+) :', l)
            if m:
                calls.add(m[1])
        frames[name] = total
        direct[name] = calls
    # Possible function-pointer values: globals and local function-address uses.
    taken = set(REFERENCE.findall(FUNCTION.sub('', ir))) & fns.keys()
    for name, m in fns.items():
        for line in m[5].splitlines():
            refs = set(REFERENCE.findall(line)) & fns.keys()
            direct_call = re.search(r'\bcall\b[^\n@]*@([\w.]+)\(', line)
            taken |= refs-({direct_call[1]}if direct_call else set())

    def signature(m):
        args, _ = call_arguments('('+m[3]+')', 1)
        types = []
        for a in args:
            if not a:
                continue
            match = re.match(
                r'(?:\[[^\]]+\]|%[\w.]+|i\d+|float|double|void)\**', a)
            if not match:
                raise ValueError(a)
            end = match.end()
            rest = a[end:].lstrip()
            if rest.startswith('('):
                begin = len(a)-len(rest)
                depth = 0
                for pos in range(begin, len(a)):
                    depth += (a[pos] == '(')-(a[pos] == ')')
                    if depth == 0:
                        end = pos+1
                        while end < len(a) and a[end] == '*':
                            end += 1
                        break
            types.append(a[:end])
        return m[1].strip().split()[-1]+' ('+', '.join(types)+')'
    sig = {name: signature(m) for name, m in fns.items()}
    by_sig = {}
    for n in taken:
        if n in asm:
            by_sig.setdefault(sig[n], set()).add(n)
    engine_definition = re.search(
        r'typedef struct\s*\{[^}]*\}\s*engine_t;', (src/'core.h').read_text(), re.S)[0]
    callback_names = re.findall(r'\(\*(\w+)\)', engine_definition)
    layout = re.search(r'^%struct.engine_t = type \{(.*)\}$', ir, re.M)[1]
    engine_fields, _ = call_arguments('('+layout+')', 1)
    callback_fields = [i for i, field in enumerate(
        engine_fields)if '(' in field]
    assert len(callback_fields) == len(callback_names)
    engine_callbacks = {}
    for source in src.glob('eng_*.c'):
        for field, fn in re.findall(r'\.(\w+)\s*=\s*(\w+)\s*[,}]', source.read_text()):
            if field in callback_names and fn in asm:
                engine_callbacks.setdefault(
                    callback_names.index(field), set()).add(fn)
    field_targets = {field: engine_callbacks.get(
        i, set())for i, field in enumerate(callback_fields)}
    indirect = {}
    missing = []
    for name, m in fns.items():
        if name not in asm:
            continue
        bindings = {}
        lines = m[5].splitlines()

        def values(line):
            result = set(REFERENCE.findall(line)) & fns.keys()
            result |= {r.removeprefix('__audio_far_')for r in REFERENCE.findall(
                line)if r.startswith('__audio_far_')}
            for r in re.findall(r'%[\w.]+', line):
                result |= bindings.get(r, set())
            return result
        while True:
            before = sum(map(len, bindings.values()))
            for line in lines:
                regs = re.findall(r'%[\w.]+', line)
                field = re.search(
                    r'= getelementptr(?: inbounds)? %struct.engine_t,.*i32 0, i32 (\d+)', line)
                if field and int(field[1]) in field_targets:
                    targets = field_targets[int(field[1])]
                    if name == 'engine_batch_kernel':
                        targets = targets-{'fm6_render', 'p5_render'}
                    bindings.setdefault(regs[0], set()).update(targets)
                if line.strip().startswith('store ') and regs:
                    v = values(line)
                    if v:
                        bindings.setdefault(regs[-1], set()).update(v)
                elif re.match(r'\s*%[\w.]+ = (?:load|bitcast|select|phi)', line):
                    v = values(line.split('=', 1)[1])
                    if v:
                        bindings.setdefault(regs[0], set()).update(v)
            if sum(map(len, bindings.values())) == before:
                break
        for line in lines:
            call = re.search(
                r'\bcall\s+(?:fastcc\s+)?([\w.%]+\**|void)\s+(%[\w.]+)\(', line)
            if not call:
                continue
            args, end = call_arguments(line, call.end())
            ret, callee = call[1], call[2]
            fake = 'define '+ret + \
                ' @fake('+','.join(args)+') {\n  ret void\n}\n'
            fm = next(FUNCTION.finditer(fake))
            typ = signature(fm)
            targets = bindings.get(callee, by_sig.get(typ, set()))
            if name == 'audio_worker_loop':
                targets = {n for n in worker_entries if n in asm}
            if name == 'audio_batch_worker':
                targets = {n for n in batch_entries if n in asm}
            indirect.setdefault(name, set()).update(targets)
            if not targets:
                missing.append((name, typ, line.strip()))
    # Far-boundary loads name their actual target; include their direct stack path.
    for name, m in fns.items():
        if name not in asm:
            continue
        direct[name] |= set(re.findall(
            r'\bcall\b[^\n@]*@([\w.]+)\(', m[5])) & asm.keys()
        direct[name] |= {n.removeprefix('__audio_far_')for n in REFERENCE.findall(
            m[5]) if n.startswith('__audio_far_')}
    graph = {name: (direct[name] | indirect.get(
        name, set())) & asm.keys() for name in asm}
    visiting = set()
    trail = []
    cache = {}

    def depth(name, note_started=False, hub_depth=0):
        key = (name, note_started, hub_depth)
        if key in visiting:
            raise ValueError("recursive stack path: " +
                             " -> ".join(trail + [name]))
        if key in cache:
            return cache[key]
        visiting.add(key)
        trail.append(name)
        children = []
        for target in graph.get(name, ()):
            # input_on can start an armed, stopped recorder and advance step
            # zero. rec_note_start publishes song.playing before that advance;
            # the nested chord tones cannot start transport a second time.
            starts_note = name == 'input_on' and target == 'seq_advance'
            if starts_note and note_started:
                continue
            # Two hub levels are supported (UH_HUB_LEVELS). The port helper
            # calls the child probe one level down; at depth 2 it rejects
            # hubs. Depending on inlining, that call can also appear inside
            # uh_probe itself.
            probes_child = name in {'uh_probe', 'uh_hub_port_probe'} and target == 'uh_probe'
            if probes_child and hub_depth >= 2:
                continue
            children.append(depth(target, note_started or starts_note,
                                  hub_depth + int(probes_child)))
        size, path = max(children, default=(0, []))
        visiting.remove(key)
        trail.pop()
        cache[key] = (frames.get(name, 0) + size, [name] + path)
        return cache[key]
    roots = ('fm1_cstart', 'isr_alnk0', 'isr_timer5',
             'fm1_core1_main', 'fm1_fault_c', 'isr_ble_event', 'isr_ble_rx')
    paths = {name: depth(name)for name in roots if name in asm}
    reachable = {key[0] for key in cache}
    missing = [item for item in missing if item[0] in reachable]
    dynamic = [name for name in dynamic if name in reachable]
    if missing:
        raise ValueError("unresolved stack callbacks: "+repr(missing))
    if dynamic:
        raise ValueError("dynamic stack frames: "+repr(dynamic))
    return {'paths': paths, 'frames': frames, 'indirect': {n: sorted(v)for n, v in indirect.items()if n in reachable}}


def check(report, foreground, irq, worker, worker_user=None):
    paths = report['paths']
    errors = []
    required = {'fm1_cstart', 'isr_alnk0',
                'isr_timer5', 'fm1_core1_main', 'fm1_fault_c'}
    if not required <= paths.keys():
        return ['missing stack roots: '+str(sorted(required-paths.keys()))]
    # Extra 2 KiB foreground margin beyond the full linked call path.
    need = paths['fm1_cstart'][0]+2048
    if need > foreground:
        errors.append(
            f'foreground stack needs {need} B including margin, has {foreground} B')
    ble_roots = {'isr_ble_event', 'isr_ble_rx'}
    if ble_roots & paths.keys() and not ble_roots <= paths.keys():
        errors.append('missing BLE interrupt stack root')
    ble_stack = max((paths[n][0] for n in ble_roots if n in paths), default=0)
    # BLE runs below audio; audio and TIMER5 can both preempt it.
    # TIMER5 preempts audio. Fatal handling can preempt either; include its
    # entire CPU0 display/recovery path and assembly exception context.
    need = ble_stack + paths['isr_alnk0'][0] + \
        paths['isr_timer5'][0]+paths['fm1_fault_c'][0]+128
    if need > irq:
        errors.append(
            f'interrupt stack needs {need} B including nested/fault paths, has {irq} B')
    # CPU1 stays IRQ-disabled: the renderer uses its 3 KiB user bank,
    # including a 256-byte bottom guard and at least 512 bytes of margin.
    # The separate 1 KiB supervisor bank handles its early fault branch;
    # that branch returns before entering CPU0's display/recovery path.
    if worker_user is None:
        worker_user = worker - 1024
    need = paths['fm1_core1_main'][0] + 512 + 256
    if need > worker_user:
        errors.append(f'worker user stack needs {need} B including margin/guard, has {worker_user} B')
    supervisor = worker - worker_user
    fault_need = max(1024, report['frames'].get('fm1_fault_c', 0) + 128 + 256)
    if supervisor < fault_need:
        errors.append(f'worker supervisor stack needs {fault_need} B, has {supervisor} B')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ir', type=Path,
                        default=Path('build/melodee_audio.ll'))
    parser.add_argument('--dis', type=Path, default=Path('build/melodee.dis'))
    parser.add_argument('--src', type=Path, default=Path('firmware/src'))
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    report = analyze(args.ir.read_text(), args.dis.read_text(), args.src)
    errors = check(report, 12288, 6144, 4096)
    for name, (size, path) in report['paths'].items():
        print(f'stack {name}: {size} B: ' + ' -> '.join(path))
    if args.json:
        args.json.write_text(json.dumps(report, indent=2)+'\n')
    for error in errors:
        print('FAIL stack: '+error)
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
