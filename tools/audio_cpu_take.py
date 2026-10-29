#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Bounded live REC/overdub takes for guarded CPU screening.

tests/audio_cpu_take.c replays the same schedule through the firmware's own
MIDI/recording code. The device take is decoded by the same validator and
compared within a timing tolerance. Input is host-paced USB MIDI: this is not
physical key, TRS clock or listening acceptance. Runtime bytes stay in memory.
"""
import hashlib
import os
import shlex
import subprocess
import threading
import time
from pathlib import Path

import mido
from audio_performance_device import setup, profile, stats, metadata, COUNTERS, DEADLINE_US, NAMES
from audio_cpu_sequence import lab_control, require_disarmed

ROOT = Path(__file__).resolve().parents[1]
HELPER_SOURCES = ('audio_cpu_take.c', 'audio_cpu_fixture.c', 'ui_test.c', 'hostsim.c')
EXECUTABLE = ROOT/'build/audio_cpu_take'
FIELDS = ('bpm', 'notes', 'mask', 'latch', 'quant', 'base')


def take_spec(bpm, notes, tracks, mode, timing, base, assignment):
    spec = dict(bpm=bpm, notes=notes, mask=1, latch=int(mode == 'latched'),
                quant=int(timing == 'grid'), base=base, assignment=assignment)
    if (not isinstance(bpm, int) or not 60 <= bpm <= 160 or notes not in (16, 64) or tracks != 'single'
            or mode not in ('latched', 'one-pass') or timing not in ('free', 'grid') or base not in (0, 64)
            or len(assignment) != 4 or any(c not in '012345' for c in assignment)):
        raise ValueError('Live take: one recording track, BPM 60..160, 16/64 notes, base 0/64')
    return spec


def _check(digests):
    for path, digest in digests.items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest:
            return False
    return True


def _helper(args, data=None):
    return subprocess.run([str(EXECUTABLE), *args], cwd=ROOT, input=data, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def compile_helper(manifest):
    """The expected take is only meaningful for the installed firmware sources."""
    if not manifest.get('firmware_sha256') or not _check(manifest['firmware_sha256']):
        raise RuntimeError('Take source differs from package manifest; use matching firmware sources')
    sources = {str(ROOT/'tests'/name): hashlib.sha256((ROOT/'tests'/name).read_bytes()).hexdigest() for name in HELPER_SOURCES}
    command = [*shlex.split(os.environ.get('CC', 'cc')), '-O2', '-ffp-contract=off', '-w',
               '-Ibuild/gen', '-Ifirmware/src', str(ROOT/'tests'/HELPER_SOURCES[0]), '-lm', '-o', str(EXECUTABLE)]
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return sources


def parse_take(text, prefix=''):
    """E: track bank cell flags on duration exponent note vel pitch length; S: overview."""
    events, steps = [], []
    for line in text.splitlines():
        tag, _, rest = line.partition(' ')
        if tag == prefix+'E':
            events.append(tuple(int(x) for x in rest.split()))
        elif tag == prefix+'S':
            steps.append(tuple(int(x) for x in rest.split()))
    if any(len(e) != 11 for e in events) or any(len(s) != 11 for s in steps):
        raise RuntimeError('Malformed take description')
    return events, sorted(steps)


def build_take(spec, manifest):
    sources = compile_helper(manifest)
    args = [str(spec[k]) for k in FIELDS] + [spec['assignment']]
    base = _helper(['--base', *args])
    plan = _helper(['--plan', *args]).decode('ascii')
    if not _check({**manifest['firmware_sha256'], **sources}):
        raise RuntimeError('Take sources changed during construction')
    if not 12 <= len(base) <= 256*1024 or base[:4] not in (b'FBKP', b'FBKK') or int.from_bytes(base[4:8], 'little') != len(base):
        raise RuntimeError('Take helper did not emit a bounded FBKP/FBKK base')
    header = [line.split()[1:] for line in plan.splitlines() if line.startswith('H ')]
    notes = [tuple(map(int, line.split()[1:])) for line in plan.splitlines() if line.startswith('N ')]
    if len(header) != 1 or len(header[0]) != 9 or len(notes) != spec['notes']:
        raise RuntimeError('Take helper emitted an incomplete plan')
    step, fs, frame_ticks, timer_hz, *echo = map(int, header[0])
    if echo != [spec[k] for k in FIELDS[:5]]:
        raise RuntimeError('Take helper plan differs from the requested take')
    expected, expected_steps = parse_take(plan, 'X')
    base_events, _ = parse_take(plan, 'B')
    return dict(spec=spec, base=base, step_frames=step, firmware_rate=fs, frame_s=frame_ticks/timer_hz, notes=notes,
                expected=expected, expected_steps=expected_steps, base_events=base_events,
                info=dict(**spec, base_bytes=len(base), base_sha256=hashlib.sha256(base).hexdigest(),
                          helper_source_sha256=sources, loop_s=64*step*frame_ticks/timer_hz, snapshot_on_disk=False,
                          evidence='Host-paced USB MIDI take; physical keys, TRS and listening are separate'))


def drum_channel(preferences):
    """Read MIDI IN's DRUM channel from the RAM preference snapshot (0 = OFF)."""
    return int(_helper(['--drum-channel'], bytes(preferences[:604])).decode('ascii'))


def inspect(raw):
    return _helper(['--inspect'], bytes(raw)).decode('ascii')


def compare(plan, observed, tolerance_ms, lateness_ms=0.0):
    """Exact structure, base events and overview; onsets/durations within tolerance."""
    events, steps = parse_take(observed)
    step_s = plan['step_frames']*plan['frame_s']
    unit_ms = step_s*1000/65536
    limit = (tolerance_ms+lateness_ms)/unit_ms
    reasons, remaining = [], list(events)
    for event in plan['base_events']:
        if event in remaining:
            remaining.remove(event)
        else:
            reasons.append(f'base event changed or missing: {event}')
    new = list(plan['expected'])
    for event in plan['base_events']:
        new.remove(event)
    key = lambda e: e[:4]+e[7:]
    worst_on = worst_length = 0.0
    for want in new:
        candidates = [e for e in remaining if key(e) == key(want)]
        if not candidates:
            reasons.append(f'missing take event: {want}')
            continue
        got = min(candidates, key=lambda e: abs(e[4]-want[4]))
        remaining.remove(got)
        on = abs(got[4]-want[4])
        length = abs((got[5] << got[6])-(want[5] << want[6]))
        slack = (1 << got[6])+(1 << want[6])
        worst_on, worst_length = max(worst_on, on*unit_ms), max(worst_length, length*unit_ms)
        if on > limit or length > 2*limit+slack or not got[5]:
            reasons.append(f'timing outside {tolerance_ms+lateness_ms:.1f} ms: {got} for {want}')
    reasons += [f'unexpected event: {e}' for e in remaining]
    if steps != plan['expected_steps']:
        reasons.append(f'overview differs in {len(set(steps) ^ set(plan["expected_steps"]))} step rows')
    return dict(passed=not reasons, reasons=reasons[:20], reason_count=len(reasons), events=len(events),
                expected_events=len(plan['expected']), base_events=len(plan['base_events']),
                max_onset_error_ms=worst_on, max_duration_error_ms=worst_length,
                tolerance_ms=tolerance_ms, input_lateness_ms=lateness_ms)


class LiveInput:
    """One sender; every scheduled message goes out, a late one late.

    Send-return lateness includes Mido's port lock and driver time; it is a
    host bound, not a USB arrival timestamp. Held notes are released on exit.
    """
    def __init__(self, outgoing, plan, max_lateness_ms):
        if not 0 < max_lateness_ms <= 10:
            raise ValueError('Input lateness limit 0..10 ms')
        frame_s = plan['frame_s']
        events = []
        for on, off, track, note, vel, _ in plan['notes']:
            events += [(on, 1, track, note, vel), (off, 0, track, note, 0)]
        # Stable plan order, releases first at equal times: the native replay's order.
        events.sort(key=lambda e: (e[0], e[1]))
        self.events = [(t*frame_s, kind, track, note, vel) for t, kind, track, note, vel in events]
        self.outgoing = outgoing
        self.stop_event = threading.Event()
        self.held = set()
        self.started_at = None
        self.error = None
        self.result = dict(scheduled=len(self.events), sent=0, max_lateness_ms=0.0, max_lateness_limit_ms=max_lateness_ms,
                           released_on_exit=0, completed=False, stopped=False, passed=False)

    def _send(self, kind, track, note, vel):
        name = 'note_on' if kind else 'note_off'
        self.outgoing.send(mido.Message(name, channel=track, note=note, velocity=vel))
        (self.held.add if kind else self.held.discard)((track, note))

    def _run(self):
        try:
            for due_s, kind, track, note, vel in self.events:
                if self.started_at is None:
                    self.started_at = due = time.monotonic()
                else:
                    due = self.started_at+due_s
                    wait = due-time.monotonic()
                    if wait > 0 and self.stop_event.wait(wait):
                        break
                if self.stop_event.is_set():
                    break
                self._send(kind, track, note, vel)
                self.result['sent'] += 1
                self.result['max_lateness_ms'] = max(self.result['max_lateness_ms'], (time.monotonic()-due)*1000)
            self.result['completed'] = self.result['sent'] == self.result['scheduled']
        except BaseException as error:
            self.error = error
        finally:
            for track, note in sorted(self.held):
                try:
                    self.outgoing.send(mido.Message('note_off', channel=track, note=note, velocity=0))
                    self.result['released_on_exit'] += 1
                except BaseException as error:
                    self.error = self.error or error
            self.held.clear()

    def alive(self):
        return self.thread.is_alive()

    def __enter__(self):
        self.thread = threading.Thread(target=self._run, name='cpu-live-input', daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.stop_event.set()
        self.thread.join(timeout=2)
        self.result['stopped'] = not self.thread.is_alive()
        self.result['passed'] = bool(self.result['stopped'] and self.result['completed'] and not self.error
            and self.result['max_lateness_ms'] <= self.result['max_lateness_limit_ms'])
        if self.thread.is_alive():
            raise RuntimeError('Live input sender failed to stop')
        if self.error:
            raise RuntimeError(f'Live input sender failed: {self.error}') from self.error


def _wait_stopped(d):
    until = time.monotonic()+2
    while True:
        state = lab_control(d)
        if not state['playing'] and not state['counting'] and not state['pending']:
            return state
        if time.monotonic() >= until:
            raise RuntimeError('Native transport did not stop after the take')
        time.sleep(.01)


def _cleanup(d, mask, error):
    """Disarm and Stop are idempotent; report a cleanup failure with the cause."""
    try:
        lab_control(d, 5, mask)
        lab_control(d, 2)
        _wait_stopped(d)
    except BaseException as cleanup:
        raise RuntimeError(f'Live take failed ({error!r}); cleanup also failed: {cleanup!r}') from error


def run_take(d, plan, categories, variant, read_runtime, tolerance_ms=8.0, lateness_ms=3.0):
    """Arm stopped tracks; the first note starts internal transport at step zero."""
    retries = d.diagnostic_retries
    spec = plan['spec']
    mask = spec['mask']
    require_disarmed(d)
    notes = setup(d, categories, [0]*4, variant)
    hook = getattr(d, 'performance_patch_hook', None)
    if hook:
        hook(d, categories, variant, notes)
    state = lab_control(d)
    if any(state[k] for k in ('playing', 'counting', 'rec', 'pending', 'clock')):
        raise RuntimeError(f'Live take requires stopped, disarmed INT clock: {state}')
    profile(d, True);before = stats(d, True)
    rows, states = [], []
    sender = LiveInput(d.outgoing, plan, lateness_ms)
    take = dict(armed=False, punched_out=False, stopped=False, punch_out_s=None)
    loop_s = plan['info']['loop_s']
    try:
        state = lab_control(d, 3 if spec['latch'] else 4, mask)
        if state['rec'] != mask or any(state[k] for k in ('playing', 'counting', 'pending')):
            raise RuntimeError(f'Arming did not produce a stopped armed take: {state}')
        take['armed'] = True
        with sender:
            while sender.alive():
                time.sleep(.08)
                rows.append(stats(d));states.append(lab_control(d))
        if spec['latch']:
            state = lab_control(d, 5, mask)
            take['punch_out_s'] = time.monotonic()-sender.started_at
            # A punch-out in the next pass would not be the replayed take.
            if state['rec'] or not state['playing'] or take['punch_out_s'] >= loop_s-.25:
                raise RuntimeError(f'Latched punch-out missed its pass: {state}, {take["punch_out_s"]:.2f} s')
        else:
            until = sender.started_at+loop_s+2
            while True:
                rows.append(stats(d));state = lab_control(d)
                if not state['rec']:
                    break
                if time.monotonic() >= until:
                    raise RuntimeError('One-pass take did not finish at its loop end')
                time.sleep(.08)
            if not state['playing']:
                raise RuntimeError('Transport stopped before the one-pass take finished')
        take['punched_out'] = True
        lab_control(d, 2);_wait_stopped(d)
        take['stopped'] = True
    except BaseException as error:
        _cleanup(d, mask, error)
        raise
    after = rows[-1] if rows else stats(d);spans = profile(d);meta = metadata(d)
    raw = read_runtime(d)
    observed = inspect(raw)
    validation = compare(plan, observed, tolerance_ms, sender.result['max_lateness_ms'])
    delta = {k: (after[k]-before[k]) & 0xffffffff for k in COUNTERS+('bench_shed_requests',)}
    peak = max([r['audio_full_max_us'] for r in rows] or [after['audio_full_max_us']])
    faults = ('audio_late', 'audio_deadline_misses', 'voices_shed', 'voices_given_up', 'capacity_steals',
              'bench_shed_requests', 'core1_faults', 'core1_rejected', 'core1_timeouts', 'resource_failures')
    # Every sample during input: armed, running from the first note, INT clock.
    states_ok = bool(states) and all(s['rec'] == mask and s['playing'] and not s['counting'] and not s['pending']
                                     and s['clock'] == 0 for s in states)
    rows = rows or [after]
    return dict(assignment=list(categories), engines=[NAMES[c] for c in categories], voices=None,
        variant=variant, workload='live-recording', seconds=time.monotonic()-sender.started_at, fixed_bench=False,
        diagnostic_retries=d.diagnostic_retries-retries, peak_complete_upper_us=peak, deadline_us=DEADLINE_US,
        headroom_pct=100*(1-peak/DEADLINE_US), voices_min=min(r['voices_active'] for r in rows),
        voices_max=max(r['voices_active'] for r in rows), voices_expected=None,
        core1_online_all=all(r['core1_online'] for r in rows), delta=delta, resource_peak=after['resource_peak'],
        normal_budget_fades=max(0, delta['voices_given_up']-delta['voices_shed']),
        resource_capacity=after['resource_capacity'], profile=spans, metadata=meta,
        usb_play_alt=after['play_alt'], usb_capture_alt=after['cap_alt'],
        live_take=dict(spec=plan['info'], input=sender.result, take=take, validation=validation,
                       native_state_samples=len(states), take_sha256=hashlib.sha256(bytes(raw)).hexdigest()),
        passed=bool(peak <= .8*DEADLINE_US and max(r['voices_active'] for r in rows) > 0
            and all(r['core1_online'] and not r['fixed_voice_bench'] for r in rows)
            and meta['clock_mhz'] == 360 and meta['half_frames'] == 128 and meta['timer5_max_ticks'] < 2400
            and sender.result['passed'] and validation['passed'] and states_ok and take['stopped']
            and not any(delta[k] for k in faults)),
        evidence='Host-paced USB MIDI live take against the native replay; no physical key/TRS/listening acceptance')
