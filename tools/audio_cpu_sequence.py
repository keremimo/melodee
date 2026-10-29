#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Synthetic recording playback and measured host-paced USB clock screening.

Temporary runtime bytes remain in process memory. This is playback of constructed
onsets/gates, not evidence of live recording or physical/TRS clock precision.
"""
import hashlib
import math
import os
import shlex
import subprocess
import threading
import time
from pathlib import Path

import mido
from audio_performance_device import setup, profile, stats, metadata, COUNTERS, DEADLINE_US, NAMES

ROOT = Path(__file__).resolve().parents[1]


def build_fixture(count, timing, layout, tails, assignment, manifest):
    if count not in (64, 256, 1024) or timing not in ('free', 'grid') or layout not in ('single', 'all') or tails not in ('short', 'long'):
        raise ValueError('Invalid recording fixture')
    if len(assignment) != 4 or any(c not in '012345' for c in assignment):
        raise ValueError('Four engine categories 0..5 required')
    if not manifest.get('firmware_sha256'):
        raise RuntimeError('Fixture requires firmware source hashes in the build manifest')
    for path, digest in manifest['firmware_sha256'].items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest:
            raise RuntimeError('Fixture source differs from package manifest; use matching firmware sources')
    sources = [ROOT/'tests'/name for name in ('audio_cpu_fixture.c', 'ui_test.c', 'hostsim.c')]
    source_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
    source = sources[0]
    executable = ROOT/'build/audio_cpu_fixture'
    command = [*shlex.split(os.environ.get('CC', 'cc')), '-O2', '-ffp-contract=off', '-w',
               '-Ibuild/gen', '-Ifirmware/src', str(source), '-lm', '-o', str(executable)]
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    raw = subprocess.run([str(executable), '--emit', str(count), str(int(timing == 'grid')),
        str(int(layout == 'all')), str(int(tails == 'long')), assignment], cwd=ROOT,
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    for path, digest in {**manifest['firmware_sha256'], **source_hashes}.items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest:
            raise RuntimeError('Fixture source changed during construction')
    if not 12 <= len(raw) <= 256*1024 or raw[:4] not in (b'FBKP', b'FBKK') or int.from_bytes(raw[4:8], 'little') != len(raw):
        raise RuntimeError('Fixture builder did not emit a bounded FBKP/FBKK runtime')
    return raw, dict(events=count, timing=timing, layout=layout, tails=tails,
        assignment=assignment, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
        builder_source_sha256=source_hashes, snapshot_on_disk=False,
        evidence='Constructed playback fixture; live capture/overdub are separate workloads')


class USBClock:
    """One clock sender, no catch-up bursts. Driver sends use Mido's port lock."""
    def __init__(self, outgoing, bpm, max_jitter_ms=2):
        if not math.isfinite(bpm) or not 40 <= bpm <= 240 or not math.isfinite(max_jitter_ms) or not 0 < max_jitter_ms < 60000/(bpm*24):
            raise ValueError('BPM 40..240; positive jitter limit below one clock interval')
        self.outgoing = outgoing
        self.period = 60/(bpm*24)
        self.stop_event = threading.Event()
        self.result = dict(bpm=bpm, max_jitter_ms=max_jitter_ms, pulses=0, missed_pulses=0,
                           max_lateness_ms=0.0, stopped=False, passed=False)
        self.error = None

    def _run(self):
        due = time.monotonic()
        try:
            self.outgoing.send(mido.Message('start'))
            while not self.stop_event.is_set():
                wait = due-time.monotonic()
                if wait > 0 and self.stop_event.wait(wait):
                    break
                self.outgoing.send(mido.Message('clock'))
                # Send-return time conservatively includes port-lock and driver
                # delays. It does not measure physical USB arrival timing.
                late = max(0.0, time.monotonic()-due)
                missed = int(late/self.period)
                self.result['missed_pulses'] += missed
                self.result['max_lateness_ms'] = max(self.result['max_lateness_ms'], late*1000)
                self.result['pulses'] += 1
                due += (missed+1)*self.period
        except BaseException as error:
            self.error = error
            self.stop_event.set()

    def __enter__(self):
        self.thread = threading.Thread(target=self._run, name='cpu-usb-clock', daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.stop_event.set()
        self.thread.join(timeout=2)
        try:
            self.outgoing.send(mido.Message('stop'))
            self.result['stopped'] = not self.thread.is_alive()
        finally:
            self.result['passed'] = bool(self.result['stopped'] and not self.error and self.result['pulses'] > 0
                and not self.result['missed_pulses'] and self.result['max_lateness_ms'] <= self.result['max_jitter_ms'])
        if self.thread.is_alive():
            raise RuntimeError('USB clock sender failed to stop')
        if self.error:
            raise RuntimeError(f'USB clock sender failed: {self.error}') from self.error


def require_disarmed(d):
    reply = d.request(27)
    if len(reply) != 30 or reply[1] != 4 or any(reply[7+6*k] for k in range(4)):
        raise RuntimeError('Recording playback requires all tracks disarmed; use the panel to disarm before running')


def lab_control(d, op=0, mask=0):
    """Retry only query, Stop and disarm after a lost acknowledgement.

    Start and arming are never resent: a duplicate Start is refused while the
    first one is pending/running, and callers resolve it by query and Stop.
    """
    for attempt in range(3):
        try:
            reply = d.request(101, [op, mask])
            break
        except TimeoutError:
            if op not in (0, 2, 5) or attempt == 2:
                raise
            time.sleep(.05)
    if len(reply) != 10 or reply[:2] != [0, 1]:
        raise RuntimeError(f'Guarded native transport/REC control rejected: {reply}')
    names = ('playing', 'counting', 'rec', 'rec_wait', 'rec_pass', 'rec_latch', 'pending', 'clock')
    state = dict(zip(names, reply[2:]))
    if any(state[k] not in (0, 1) for k in ('playing', 'counting')) or any(state[k] > 15 for k in ('rec', 'rec_wait', 'rec_pass', 'rec_latch')) or state['pending'] > 3 or state['clock'] > 2:
        raise RuntimeError(f'Unknown native control state: {state}')
    return state


class InternalClock:
    """Ordinary local transport; no bank-chain preparation or USB MIDI clock."""
    def __init__(self, device):
        self.d = device
        self.result = dict(source='internal', started=False, stopped=False, passed=False)

    def _wait(self, playing):
        until = time.monotonic()+2
        while True:
            state = lab_control(self.d)
            if state['clock'] != 0 or state['rec'] or state['counting']:
                raise RuntimeError(f'Internal playback state changed: {state}')
            if state['playing'] == playing and not state['pending']:
                return state
            if time.monotonic() >= until:
                raise RuntimeError('Native transport did not acknowledge its queued transition')
            time.sleep(.01)

    def __enter__(self):
        state = lab_control(self.d)
        if any(state[k] for k in ('clock', 'playing', 'counting', 'rec', 'pending')):
            raise RuntimeError(f'Internal playback requires stopped/disarmed INT clock: {state}')
        try:
            lab_control(self.d, 1)
            self._wait(1)
            self.result['started'] = True
        except BaseException as error:
            # Start may have succeeded even if its acknowledgement was lost.
            try:
                self.__exit__(None, None, None)
            except BaseException as cleanup:
                raise RuntimeError(f'Native Start failed ({error!r}); cleanup Stop also failed: {cleanup!r}') from error
            raise
        return self

    def __exit__(self, *exc):
        lab_control(self.d, 2)
        self._wait(0)
        self.result['stopped'] = True
        self.result['passed'] = self.result['started']


def run_sequence(d, categories, voices, variant, seconds, fixed):
    if fixed:
        raise ValueError('Recording playback never uses fixed benchmark bypass')
    retries = d.diagnostic_retries
    require_disarmed(d)
    notes = setup(d, categories, [0]*4, variant)
    hook = getattr(d, 'performance_patch_hook', None)
    if hook:
        hook(d, categories, variant, notes)
    profile(d, True);before = stats(d, True)
    start = time.monotonic();rows = []
    internal = getattr(d, 'cpu_sequence_clock', 'usb') == 'internal'
    clock_source = InternalClock(d) if internal else USBClock(d.outgoing, d.cpu_sequence_bpm, d.cpu_clock_jitter_ms)
    states = []
    with clock_source as clock:
        while not rows or time.monotonic()-start < seconds:
            time.sleep(min(.08, max(0, seconds-(time.monotonic()-start))))
            hook = getattr(d, 'performance_step_hook', None)
            if hook:
                hook(d, categories, [], len(rows)+1)
            rows.append(stats(d))
            if internal:
                states.append(lab_control(d))
    after = rows[-1];spans = profile(d);meta = metadata(d)
    delta = {k: (after[k]-before[k]) & 0xffffffff for k in COUNTERS+('bench_shed_requests',)}
    peak = max(r['audio_full_max_us'] for r in rows)
    faults = ('audio_late', 'audio_deadline_misses', 'voices_shed', 'voices_given_up', 'capacity_steals',
              'bench_shed_requests', 'core1_faults', 'core1_rejected', 'core1_timeouts', 'resource_failures')
    return dict(assignment=list(categories), engines=[NAMES[c] for c in categories], voices=None,
        variant=variant, workload='recording-playback', seconds=time.monotonic()-start, fixed_bench=False,
        diagnostic_retries=d.diagnostic_retries-retries, peak_complete_upper_us=peak, deadline_us=DEADLINE_US,
        headroom_pct=100*(1-peak/DEADLINE_US), voices_min=min(r['voices_active'] for r in rows),
        voices_max=max(r['voices_active'] for r in rows), voices_expected=None,
        core1_online_all=all(r['core1_online'] for r in rows), delta=delta, resource_peak=after['resource_peak'],
        normal_budget_fades=max(0, delta['voices_given_up']-delta['voices_shed']),
        resource_capacity=after['resource_capacity'], profile=spans, metadata=meta,
        usb_play_alt=after['play_alt'], usb_capture_alt=after['cap_alt'],
        external_usb_clock=None if internal else clock.result,
        internal_clock=clock.result if internal else None,
        native_transport_samples=len(states),
        passed=bool(peak <= .8*DEADLINE_US and max(r['voices_active'] for r in rows) > 0
            and all(r['core1_online'] and not r['fixed_voice_bench'] for r in rows)
            and meta['clock_mhz'] == 360 and meta['half_frames'] == 128 and meta['timer5_max_ticks'] < 2400
            and clock.result['passed'] and all(s['playing'] and not any(s[k] for k in ('counting', 'rec', 'pending', 'clock')) for s in states)
            and not any(delta[k] for k in faults)),
        evidence=f'Constructed playback under {"native internal" if internal else "measured host USB"} clock; no live-recording/TRS/physical acceptance')
