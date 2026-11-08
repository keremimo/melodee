#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Repeatable four-track device load; compare normal drawing with screen sleep.

Requires mido/python-rtmidi. Never saves a project or installs firmware.
Prepare only after making a complete backup with prophet_device.backup.
The original runtime is restored and persistent CRCs checked by restore.
"""
import argparse
import json
from pathlib import Path
import statistics
import time
import zlib

from prophet_device import Device, backup, inventory, pack, param_ids, u32, verify, patch_get, patch_put, param_set
from usb_audio_stats import snapshot

COUNTERS = ('audio_late', 'voices_shed', 'voices_given_up', 'core1_faults',
            'core1_rejected', 'core1_timeouts', 'resource_failures',
            'play_underruns', 'play_overruns', 'cap_underruns',
            'cap_overruns', 'missed_frames')


def restore_runtime(d, directory):
    """Retry the whole RAM-only transfer after a lost acknowledgement.

    Smaller chunks reduce SysEx queue pressure. Starting over also avoids
    guessing whether a timed-out chunk advanced the device's staging cursor.
    Object zero restores live music without writing a saved project slot.
    """
    raw = (directory/'0.bin').read_bytes()
    for attempt in range(3):
        try:
            d.request(74, [3])
            time.sleep(.2)
            transactions = [[0, 0, *u32(len(raw)), *u32(zlib.crc32(raw))]]
            transactions += [[1, 0, *u32(off), *pack(raw[off:off+128])]
                             for off in range(0, len(raw), 128)]
            transactions += [[2, 0]]
            for args in transactions:
                reply = d.request(67, args)
                if len(reply) < 3 or reply[2]:
                    raise RuntimeError(f'Runtime restore failed: {reply}')
                time.sleep(.003)
            print('Runtime restored', flush=True)
            return
        except TimeoutError:
            if attempt == 2:
                raise
            print('Restarting a timed-out runtime restore', flush=True)
            time.sleep(.2)


def stats(d):
    for attempt in range(3):
        try:
            return snapshot(d.incoming, d.outgoing, True, True, True, True, True)
        except TimeoutError:
            if attempt == 2:
                raise
            print('Retrying a timed-out diagnostics read', flush=True)


def checked(reply, rc_index, what):
    if reply[rc_index]:
        raise RuntimeError(f'{what}: {reply}')
    return reply


def global_value(d, key, value):
    _, ids = param_ids()
    wire = value + 8192
    d.request(3, [1, ids[key], wire & 127, wire >> 7])


def prepare(d, directory):
    original = directory / 'original-backup'
    if not (original / 'manifest.json').exists():
        raise RuntimeError('Complete original-backup is required first')
    d.silence()
    # Runtime restore detaches the current project from saved slots, so
    # autosave cannot overwrite the user's saved song during construction.
    restore_runtime(d, original)
    verify(d, original)
    for key, value in {'G_BPM': 180, 'G_SWING': 0, 'G_CLOCK': 0,
                       'G_MFXTYPE': 0, 'G_MFX2TYPE': 0,
                       'G_MFX3TYPE': 0}.items():
        global_value(d, key, value)
    engines = (19, 12, 15, 10)
    presets = (3, 4, 4, 0)  # Forever Keys, PAD, STRINGS 1, 808
    for track, (engine, preset) in enumerate(zip(engines, presets)):
        d.request(27, [track])
        reply = d.request(8, [engine, preset])
        if reply[:2] != [engine, preset]:
            raise RuntimeError(f'Preset readback failed: {reply}')
        checked(d.request(73, [track, 1, 0]), 2, 'pattern select')
        checked(d.request(64, [track, 2]), 1, 'motion clear')
        common = dict(P_LEVEL=44, P_PAN=0, P_MUTE=0, P_VOICE=0,
                      P_GLIDE=0, P_AMODE=0, P_AHOLD=0,
                      P_QUANT=0, P_CHRD=0, P_TRANS=0,
                      P_SLEN=64, P_SDIV=2, P_SSWING=0, P_SGATE=120,
                      P_LD_PIT=0, P_LD_FLT=0, P_LD_SHP=0, P_LD_AMP=0,
                      P_ED_FLT=0, P_ED_PIT=0, P_M1AMT=0, P_M2AMT=0,
                      P_M3AMT=0, P_M4AMT=0, P_FXTYPE=0,
                      P_FX2TYPE=0, P_FX3TYPE=0)
        if track == 3:
            common['P_E0'] = 4  # explicitly 808 rather than 909
        d.params(track, common)
        ids, _ = param_ids()
        for key, expected in common.items():
            reply = d.request(31, [track, ids[key]])
            actual = (reply[2] | reply[3] << 7)-8192
            if actual != expected:
                raise RuntimeError(f'Parameter readback failed: {track}/{key}: {actual}')
    # Enable the Prophet's costly SSI filter and audio-rate Poly-Mod path.
    for field, value in {3: 1, 4: 1, 5: 1, 6: 1, 7: 1, 10: 1,
                         14: 100, 15: 90, 17: 85, 18: 80, 20: 0,
                         32: 70, 33: 75, 34: 1, 35: 1, 36: 1,
                         44: 0, 48: 110, 50: 72, 51: 1}.items():
        param_set(d, 0, field, value)
    engine, p5 = patch_get(d, 0)
    p5[65:85] = b'UI STRESS P5'.ljust(20)          # (the Prophet schema's name: 20 bytes at 65)
    patch_put(d, 0, engine, p5)
    # Both CZ lines render, rather than a cheaper single-line preset.
    engine, raw = patch_get(d, 2)
    if len(raw) != 144:
        raise RuntimeError(f'Unexpected CZ tone length: {len(raw)}')
    raw[0] = (raw[0] & ~3) | 2
    raw[128:144] = b'UI STRESS CZ    '
    patch_put(d, 2, engine, raw)
    # 64 populated steps per track. Chords/release tails and dense drums
    # deliberately stress both rendering and the shared voice allocator.
    harmony = ((48, 55, 60), (53, 60, 65), (55, 62, 67), (51, 58, 63))
    patterns = []
    for track in range(4):
        pattern = []
        for step in range(64):
            root, fifth, octave = harmony[(step//16) % 4]
            if track == 0:
                notes = [root, fifth, octave]
            elif track == 1:
                notes = [root+12, root+16, fifth+12, octave+12]
            elif track == 2:
                notes = [root+24+(step % 4)*2, fifth+24]
            else:
                notes = []
            hit = (1 << 3) | ((1 << 0) if step % 4 == 0 else 0)
            if step % 8 == 4:
                hit |= (1 << 1) | (1 << 2)
            if step % 8 == 7:
                hit |= (1 << 5) | (1 << 7)
            if track != 3:
                hit = 0
            payload = [len(notes), *(notes+[0]*4)[:4], 0,
                       int(step % 4 == 0), 110, hit & 127,
                       (hit if step % 4 == 0 else 0) & 127,
                       (hit >> 7) | (((hit if step % 4 == 0 else 0) >> 7) << 1), 100]
            reply = d.request(30, [track, step, *payload])
            if reply[:2] != [track, step] or reply[2:] != payload:
                raise RuntimeError(f'Step readback failed: {track}/{step}: {reply}')
            pattern.append(payload)
        patterns.append(pattern)
    checked(d.request(74, [1, 16, *([0, 0, 0, 0, 16]*16)]), 1, 'chain')
    d.request(27, [0])
    manifest = dict(name='UI load: Prophet / FM6 / CZ-1 / 808', bpm=180,
                    engines=engines, presets=presets, patterns=patterns,
                    fx='off', expected_screen_sleep_minutes=5)
    (directory/'stress-patterns.json').write_text(json.dumps(manifest, indent=2)+'\n')
    backup(d, directory/'stress-backup')
    verify(d, original)
    print('READY: all 256 steps read back; stress runtime backed up', flush=True)


def summarize(directory):
    rows = [json.loads(line) for line in (directory/'samples.jsonl').read_text().splitlines()]
    markers = json.loads((directory/'markers.json').read_text()) if (directory/'markers.json').exists() else {}
    result = dict(markers=markers, phases={})
    # Windows are paired by their length; only classify actual user-confirmed
    # screen states, never infer sleep from elapsed time alone.
    for phase in ('drawing', 'sleeping'):
        start = markers.get(phase)
        if start is None:
            continue
        end = start+65
        if phase == 'drawing' and markers.get('sleeping', 0) > start:
            end = min(end, markers['sleeping']-1)
        chosen = [r for r in rows if start+5 <= r['wall_time'] <= end]
        if len(chosen) < 2:
            continue
        first, last = chosen[0], chosen[-1]
        result['phases'][phase] = dict(samples=len(chosen), seconds=last['elapsed_s']-first['elapsed_s'],
            median_cpu_pct=statistics.median(r['cpu_pct'] for r in chosen),
            max_audio_render_us=max(r['audio_max_us'] for r in chosen),
            voices_min=min(r['voices_active'] for r in chosen),
            voices_max=max(r['voices_active'] for r in chosen),
            core1_online_all=all(r['core1_online'] for r in chosen),
            delta={k:last[k]-first[k] for k in COUNTERS},
            track_shed=[last[f'track_{t}_voices_shed']-first[f'track_{t}_voices_shed'] for t in range(1,5)])
        phase_result = result['phases'][phase]
        phase_result['per_second'] = {k:round(value/phase_result['seconds'], 3)
                                      for k,value in phase_result['delta'].items()}
    (directory/'summary.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


def measure(d, directory, seconds, load_stress=False):
    if load_stress:
        d.silence()
        restore_runtime(d, directory/'stress-backup')
        original = next(item for item in json.loads(
            (directory/'stress-backup/manifest.json').read_text()) if item['id'] == 0)
        loaded = next((length, crc) for ident, length, crc in inventory(d) if ident == 0)
        if loaded != (original['length'], original['crc']):
            restore_runtime(d, directory/'original-backup')
            raise RuntimeError('Stress runtime checksum changed during restore')
        print('Stress runtime checksum matches the reference', flush=True)
    checked(d.request(74, [2]), 1, 'start chain')
    start = time.monotonic()
    try:
        with (directory/'samples.jsonl').open('w') as out:
            while time.monotonic()-start < seconds:
                row = stats(d)
                row.update(elapsed_s=round(time.monotonic()-start, 3), wall_time=time.time(),
                           cpu_pct=round(row['cpu_q8']*100/256, 2), transport=d.request(74, [0])[:6])
                if not row['transport'][3]:
                    raise RuntimeError(f'Transport stopped: {row["transport"]}')
                out.write(json.dumps(row)+'\n'); out.flush()
                if int(row['elapsed_s']) % 20 < 2:
                    print(json.dumps({k:row[k] for k in ('elapsed_s', 'cpu_pct', 'audio_late', 'voices_shed',
                                                        'voices_given_up', 'core1_online', 'voices_active')}), flush=True)
                summarize(directory)
                markers_path = directory/'markers.json'
                if markers_path.exists():
                    markers = json.loads(markers_path.read_text())
                    if ('sleeping' in markers and 'drawing' in markers and
                            time.time() >= max(markers.values())+65):
                        break
                time.sleep(2)
    finally:
        d.silence()
        d.request(74, [3])
        restore_runtime(d, directory/'original-backup')
        verify(d, directory/'original-backup')
        summarize(directory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'measure', 'restore', 'summary', 'mark'))
    parser.add_argument('--port', default='Melodee')
    parser.add_argument('--directory', type=Path, default=Path('build/ui-load-comparison'))
    parser.add_argument('--seconds', type=float, default=440)
    parser.add_argument('--load-stress', action='store_true',
                        help='load the backed-up stress runtime before measuring')
    parser.add_argument('--phase', choices=('drawing', 'sleeping'))
    parser.add_argument('--remove', action='store_true')
    args = parser.parse_args()
    if args.action == 'mark':
        if args.phase is None:
            parser.error('mark requires --phase')
        path = args.directory/'markers.json'
        markers = json.loads(path.read_text()) if path.exists() else {}
        if args.remove: markers.pop(args.phase, None)
        else: markers[args.phase] = time.time()
        path.write_text(json.dumps(markers, indent=2)+'\n')
        return
    if args.action == 'summary':
        print(json.dumps(summarize(args.directory), indent=2)); return
    d = Device(args.port)
    try:
        if args.action == 'prepare':
            try: prepare(d, args.directory)
            except BaseException:
                if (args.directory/'original-backup/manifest.json').exists():
                    d.silence(); restore_runtime(d, args.directory/'original-backup')
                    verify(d, args.directory/'original-backup')
                raise
        elif args.action == 'measure': measure(d, args.directory, args.seconds, args.load_stress)
        else:
            d.silence(); restore_runtime(d, args.directory/'original-backup')
            verify(d, args.directory/'original-backup')
    finally:
        d.close()


if __name__ == '__main__':
    main()
