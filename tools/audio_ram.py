#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Place the enabled audio call graph in SRAM after LLVM has finished inlining.

The resident profile places the complete dry audio graph in executable POOL SRAM.
Existing flash-safe and low-SRAM DSP sections keep their placement. Crossing
XIP/SRAM uses a volatile function pointer: pi32v2 direct branches cannot span
that distance. Calling conventions and function signatures stay unchanged.
"""
import json
import re
import sys
from pathlib import Path

FUNCTION = re.compile(r'^define ([^\n]+?)@([\w.]+)\(([^\n]*)\)([^\n]*)\{\n(.*?)^}', re.M | re.S)
REFERENCE = re.compile(r'@([\w.]+)')
CALL = re.compile(r'\bcall\b[^\n@]*@([\w.]+)\(')
ROOTS = {'fm1_alnk0_irq', 'audio_block', 'mix_block', 'ua_audio', 'click_render',
         'p5_worker_kernel', 'fm6_worker_kernel', 'audio_batch_worker', 'audio_batch_parallel', 'p5_batch_kernel', 'fm6_batch_kernel', 'engine_batch_kernel', 'fm1_timer5_irq',
         'mod_part_mem', 'voice_scale_pitch', 'trk_nvoice', 'audio_batch_push', 'audio_batch_push_cost',
         'audio_worker_submit', 'audio_worker_join', 'audio_profile_add', 'mod_h_pitch_inc', 'mod_h_octave',
         'mod_h_no_submit', 'mod_h_no_join', 'mod_h_no_profile',
         'patch_play', 'mix_h_stereo'}   # (the host table's calls a synth module makes from the audio: eng_module.c
                         # MOD_HOST3; patch: the part's patch, patch.c; stereo: host 5's, mix.c)
CALLBACKS = {'note_on', 'render', 'amp', 'block', 'done', 'alloc', 'legato',
             'mono_key', 'post', 'units', 'cap', 'keys', 'desc',
             'state', 'env_q15', 'chord', 'reuse', 'batch', 'render_p', 'menv', 'join', 'reserve', 'bend_range',
             'patch_changed', 'pget', 'pput'}   # (Motion plays a synth's parameters from the sequencer's tick)
COLD_CALLBACKS = {'preset_make', 'pformat', 'pactive', 'draw', 'sysex', 'sysex_dump',   # (the pages', SysEx: the main loop's;
                  'rec_check', 'rec_stream', 'rec_pad'}                                   # a synth's data records too)


def engine_roots(src):
    # Indirect engine calls are absent from LLVM's direct call graph. Check
    # the interface too, so a new callback cannot silently stay in flash.
    core = (src / 'core.h').read_text()
    engine = re.search(r'typedef struct\s*\{[^}]*\}\s*engine_t;', core, re.S)
    if not engine:
        raise ValueError('Engine callback interface is missing')
    fields = set(re.findall(r'\(\*(\w+)\)', engine[0]))
    unknown = fields - CALLBACKS - COLD_CALLBACKS
    if unknown:
        raise ValueError(f'Unclassified engine callbacks: {sorted(unknown)}')
    roots = set(ROOTS)
    for file in src.glob('eng_*.c'):
        for field, name in re.findall(r'\.(\w+)\s*=\s*(\w+)\s*[,}]', file.read_text()):
            if field in CALLBACKS:
                roots.add(name)
    return roots


def function_type(match):
    # Optimized definitions have scalar/pointer arguments; reject a new ABI
    # shape rather than silently constructing the wrong indirect call type.
    ret = match[1].strip().split()[-1]
    args = []
    depth = 0
    args_source = []
    begin = 0
    for pos, char in enumerate(match[3] + ','):
        depth += (char == '(') - (char == ')')
        if char == ',' and depth == 0:
            args_source.append(match[3][begin:pos].strip())
            begin = pos + 1
    for arg in filter(None, args_source):
        token = re.match(r'(?:void|i\d+|float|double|%[\w.]+)\**|\.\.\.', arg)
        if not token:
            raise ValueError(f'Unsupported audio argument type for {match[2]}: {arg}')
        end = token.end()
        rest = arg[end:].lstrip()
        if rest.startswith('('):
            start = len(arg) - len(rest)
            depth = 0
            for pos in range(start, len(arg)):
                depth += (arg[pos] == '(') - (arg[pos] == ')')
                if not depth:
                    end = pos + 1
                    while end < len(arg) and arg[end] == '*':
                        end += 1
                    break
        typ = arg[:end]
        args.append(typ)
    return f'{ret} ({", ".join(args)})'


def call_arguments(line, begin):
    """Read old LLVM intrinsic arguments, including nested constant GEPs."""
    args, depth, start = [], 0, begin
    for pos in range(begin, len(line)):
        char = line[pos]
        if char == ')' and depth == 0:
            args.append(line[start:pos].strip())
            return args, pos
        if char == ',' and depth == 0:
            args.append(line[start:pos].strip())
            start = pos + 1
        depth += (char in '([{') - (char in ')]}')
    raise ValueError('Unterminated LLVM memory intrinsic')


def relocate(ir, roots, required_root="fm1_alnk0_irq", low=()):   # fm1-base (platform/tools/ir_pass.py): its render's
    # root, and the resident functions it puts in the low SRAM's DSP section rather than the pool's (its arena)
    functions = {m[2]: m for m in FUNCTION.finditer(ir)}
    selected = set()
    pending = list(roots)
    while pending:
        name = pending.pop()
        # usb_poll is deliberately excluded: TIMER5 calls it only outside
        # the audio interrupt. Nested endpoint/UART/input work remains resident.
        if name == 'usb_poll' or name not in functions or name in selected:
            continue
        selected.add(name)
        pending.extend(REFERENCE.findall(functions[name][5]))
        # Backend lowering can turn these intrinsics into libc calls after
        # this pass. Their implementation must join an audio caller's closure.
        pending.extend(re.findall(r'@llvm\.(memcpy|memset)\.', functions[name][5]))
    if required_root not in selected:
        raise ValueError('Audio ISR is missing from the optimized module')

    sections = {name: (re.search(r'\bsection "([^"]+)"', m[4]) or [None, ''])[1]
                for name, m in functions.items()}
    missing = set(low) - selected
    if missing:
        raise ValueError(f'Low-SRAM audio functions not resident: {", ".join(sorted(missing))}')
    for name in selected:
        if not sections[name]:
            sections[name] = ('.audio_libc' if name in {'memcpy', 'memset'} else
                              '.dsp_text' if name in low else '.audio_text')
        elif sections[name] not in ('.audio_text', '.audio_libc', '.dsp_text', '.ram_text'):
            raise ValueError(f'Unexpected audio section for {name}: {sections[name]}')
    far = {}

    def rewrite(match):
        name = match[2]
        header = match[0].split('\n', 1)[0]
        if name in selected and not re.search(r'\bsection "', match[4]):
            header = header[:-1].rstrip() + f' section "{sections[name]}" {{'
        body = []
        for line in match[5].splitlines():
            call = CALL.search(line)
            intrinsic = re.search(r'call void @llvm\.(memcpy|memset)\.p0i8(?:\.p0i8)?\.i32\(', line)
            if intrinsic:
                target = intrinsic[1]
                if target in functions and bool(sections[name]) != bool(sections[target]):
                    args, end = call_arguments(line, intrinsic.end())
                    if len(args) != 5 or args[-1] != 'i1 false':
                        raise ValueError('Unsupported cross-region memory intrinsic')
                    typ = function_type(functions[target])
                    far[target] = typ
                    local = f'%audio_far_{len(body)}'
                    body.append(f'  {local} = load volatile {typ}*, {typ}** @__audio_far_{target}, align 4')
                    args = args[:3]
                    if target == 'memset':
                        value = args[1].removeprefix('i8 ')
                        if value.startswith('%'):
                            wide = f'%audio_fill_{len(body)}'
                            body.append(f'  {wide} = zext i8 {value} to i32')
                            value = wide
                        args[1] = 'i32 ' + value
                    # Name the unused libc result: an unnamed nonvoid call
                    # would consume a numeric LLVM slot and renumber the IR.
                    prefix = line[:intrinsic.start()]
                    qualifier = prefix.strip()
                    if qualifier not in ('', 'tail', 'notail'):
                        raise ValueError('Unsupported memory intrinsic call qualifier')
                    qualifier = qualifier + ' ' if qualifier else ''
                    line = f'  %audio_copy_{len(body)} = {qualifier}call i8* {local}(' + ', '.join(args) + line[end:]
                    call = None
            if call and call[1] in functions and sections[name] != sections[call[1]]:
                # SRAM sections can call each other directly; all fit well
                # inside branch range. Only a flash/RAM crossing needs this.
                if bool(sections[name]) != bool(sections[call[1]]):
                    target = call[1]
                    typ = function_type(functions[target])
                    far[target] = typ
                    local = f'%audio_far_{len(body)}'
                    body.append(f'  {local} = load volatile {typ}*, {typ}** @__audio_far_{target}, align 4')
                    line = line[:call.start(1)-1] + local + line[call.end(1):]
            body.append(line)
        return header + '\n' + '\n'.join(body) + '\n}'

    result = FUNCTION.sub(rewrite, ir)
    globals_ir = '\n'.join(f'@__audio_far_{name} = internal global {typ}* @{name}, align 4'
                           for name, typ in sorted(far.items()))
    result += '\n' + globals_ir + '\n'
    return result, sorted(selected), sorted(far)


def main(source, destination):
    src = Path(__file__).resolve().parents[1] / 'firmware/src'
    roots = engine_roots(src)
    result, selected, far = relocate(Path(source).read_text(), roots)
    Path(destination).write_text(result)
    Path(destination).with_suffix('.json').write_text(json.dumps({'resident': selected, 'far_calls': far}, indent=2))
    print(f'audio RAM: {len(selected)} reachable functions; {len(far)} flash/RAM call boundaries')


if __name__ == '__main__':
    main(*sys.argv[1:3])
