#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Live REC/overdub plans, take comparison, input sender and guarded takes."""
import contextlib
import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import audio_cpu_take as take
import audio_cpu_device as cpu
import audio_cpu_stress_test as normal


def manifest():
    source = take.ROOT/'firmware/src/core.h'
    return dict(firmware_sha256={str(source): hashlib.sha256(source.read_bytes()).hexdigest()})


class Outgoing:
    def __init__(self, fail_at=None):
        self.sent, self.fail_at = [], fail_at

    def send(self, message):
        if self.fail_at is not None and len(self.sent) == self.fail_at:
            self.fail_at = None
            raise OSError('MIDI port lost')
        self.sent.append(message)


def quick_plan(latch=1, mask=1, notes=((0, 30, 0, 60, 100, 0), (10, 40, 0, 65, 90, 1))):
    spec = dict(bpm=120, notes=len(notes), mask=mask, latch=latch, quant=0, base=0, assignment='0124')
    return dict(spec=spec, notes=list(notes), frame_s=1e-5, step_frames=5512, base_events=[], expected=[],
                expected_steps=[], info=dict(spec, loop_s=60.0))


class TakeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.single = take.build_take(take.take_spec(120, 16, 'single', 'latched', 'free', 0, '0124'), manifest())
        cls.overdub = take.build_take(take.take_spec(120, 64, 'single', 'one-pass', 'grid', 64, '0124'), manifest())

    def observe(self, plan, seed, jitter):
        s = plan['spec']
        args = [str(seed), str(jitter), *(str(s[k]) for k in take.FIELDS), s['assignment']]
        return take._helper(['--observe', *args]).decode()         # compiled by build_take

    def test_plan_is_bounded_in_memory_and_note_started(self):
        plan = self.single
        self.assertEqual(plan['base'][:4], b'FBKP')           # (16 patterns a part: the FBKP record)
        self.assertLess(len(plan['base']), 256*1024)
        self.assertFalse(plan['info']['snapshot_on_disk'])
        self.assertEqual(len(plan['notes']), 16)
        self.assertEqual(plan['notes'][0][:2][0], 0)            # the first note starts the transport
        self.assertEqual(len(plan['expected']), 16)
        self.assertEqual(plan['base_events'], [])
        self.assertAlmostEqual(plan['info']['loop_s'], 64*5512*544/24e6)
        self.assertEqual(len(self.overdub['base_events']), 64)
        self.assertEqual(len(self.overdub['expected']), 128)

    def test_jittered_native_take_passes_and_exact_retained_base(self):
        for plan in (self.single, self.overdub):
            for seed in (1, 2):
                result = take.compare(plan, self.observe(plan, seed, 200), 8)
                self.assertTrue(result['passed'], result['reasons'])
                self.assertLess(result['max_onset_error_ms'], 8)
        self.assertEqual(take.compare(self.overdub, self.observe(self.overdub, 3, 0), 8)['base_events'], 64)

    def test_missing_extra_changed_base_overview_and_timing_fail(self):
        plan = self.overdub
        lines = self.observe(plan, 4, 100).splitlines()
        events = [i for i, line in enumerate(lines) if line.startswith('E ')]
        base = set(' '.join(map(str, e)) for e in plan['base_events'])
        new = [i for i in events if lines[i][2:] not in base]
        old = [i for i in events if lines[i][2:] in base]
        def variant(change):
            copy = list(lines);change(copy);return take.compare(plan, '\n'.join(copy), 8)
        self.assertFalse(variant(lambda c: c.pop(new[3]))['passed'])
        self.assertFalse(variant(lambda c: c.append(lines[new[0]]))['passed'])
        def base_velocity(c):
            f = c[old[5]].split();f[9] = str(int(f[9]) % 120+1);c[old[5]] = ' '.join(f)
        self.assertRegex(' '.join(variant(base_velocity)['reasons']), 'base event')
        def late(c):
            f = c[new[2]].split();f[5] = str(int(f[5])+int(20/(5512*544/24e3)*65536));c[new[2]] = ' '.join(f)
        self.assertRegex(' '.join(variant(late)['reasons']), 'timing')
        overview = next(i for i, line in enumerate(lines) if line.startswith('S '))
        def step(c):
            f = c[overview].split();f[-1] = str(int(f[-1]) ^ 1);c[overview] = ' '.join(f)
        self.assertRegex(' '.join(variant(step)['reasons']), 'overview')
        with self.assertRaises(RuntimeError):
            take.parse_take('E 1 2 3')

    def test_specs_rejected_before_construction(self):
        for args in ((59, 16, 'single', 'latched', 'free', 0), (120, 256, 'single', 'latched', 'free', 0),
                     (120, 16, 'single', 'latched', 'free', 256), (120, 32, 'single', 'latched', 'free', 0),
                     (120.5, 16, 'single', 'latched', 'free', 0), (120, 16, 'single', 'held', 'free', 0),
                     (120, 16, 'all', 'latched', 'free', 0)):
            with self.assertRaises(ValueError):
                take.take_spec(*args, '0124')
        with patch.object(take.subprocess, 'run') as run, self.assertRaisesRegex(RuntimeError, 'source'):
            take.build_take(take.take_spec(120, 16, 'single', 'latched', 'free', 0, '0124'),
                            dict(firmware_sha256={str(take.ROOT/'firmware/src/core.h'): 'mismatch'}))
        run.assert_not_called()

    def test_sender_order_lateness_and_release_on_failure(self):
        plan = quick_plan(notes=((0, 30, 0, 60, 100, 0), (30, 50, 1, 62, 90, 0), (30, 60, 0, 64, 80, 1)))
        out = Outgoing()
        with take.LiveInput(out, plan, 3) as sender:
            sender.thread.join(1)
        self.assertTrue(sender.result['passed'])
        self.assertEqual([(m.type, m.channel, m.note) for m in out.sent],
            [('note_on', 0, 60), ('note_off', 0, 60), ('note_on', 1, 62), ('note_on', 0, 64), ('note_off', 1, 62), ('note_off', 0, 64)])
        out = Outgoing(fail_at=2)
        with self.assertRaisesRegex(RuntimeError, 'MIDI port lost'):
            with take.LiveInput(out, quick_plan(), 3) as sender:
                sender.thread.join(1)
        self.assertFalse(sender.result['passed'])
        # Both held notes are released after the failed send.
        self.assertEqual([(m.type, m.note) for m in out.sent[-2:]], [('note_off', 60), ('note_off', 65)])
        self.assertEqual(sender.result['released_on_exit'], 2)
        with self.assertRaises(ValueError):
            take.LiveInput(out, plan, 0)

    def native(self, plan, one_pass_polls=2, drop_arm=False, fail_op=None, cleanup_fails=False):
        mask = plan['spec']['mask'];calls = []
        state = dict(playing=0, counting=0, rec=0, rec_wait=0, rec_pass=0, rec_latch=0, pending=0, clock=0)
        polls = [0]
        def control(d, op=0, m=0):
            calls.append((op, m))
            if fail_op == op and not (cleanup_fails is False and calls.count((op, m)) > 1):
                if cleanup_fails or calls.count((op, m)) == 1:
                    raise OSError(f'op {op} lost')
            if op in (3, 4):
                state.update(rec=mask, rec_latch=mask*(op == 3))
            elif op == 5:
                state.update(rec=0, rec_latch=0)
            elif op == 2:
                state.update(playing=0)
            elif state['rec'] and not plan['spec']['latch'] and state['playing']:
                polls[0] += 1
                if polls[0] > one_pass_polls:
                    state.update(rec=0)
            if state['rec'] and op == 0 and not state['playing']:
                state['playing'] = 1                          # the first note started it
            if drop_arm and op == 0 and state['playing'] and polls[0] == 0:
                polls[0] = 1;return dict(state, rec=0)
            return dict(state)
        return control, calls

    def run_native(self, plan, control, compare=dict(passed=True), out=None):
        row = normal.snapshot(0);row.update(voices_active=3, resource_peak=10, resource_capacity=20)
        device = SimpleNamespace(diagnostic_retries=0, outgoing=out or Outgoing())
        with patch.object(take, 'lab_control', control), patch.object(take, 'require_disarmed'), \
             patch.object(take, 'setup', return_value=[]), patch.object(take, 'profile', return_value={}), \
             patch.object(take, 'stats', return_value=row), patch.object(take, 'metadata', return_value=normal.metadata()), \
             patch.object(take, 'inspect', return_value=''), patch.object(take, 'compare', return_value=compare):
            return take.run_take(device, plan, [0, 1, 2, 4], 0, lambda d: b'FBKK take')

    def test_latched_and_one_pass_takes_disarm_stop_and_validate(self):
        for latch in (1, 0):
            plan = quick_plan(latch=latch)
            control, calls = self.native(plan)
            row = self.run_native(plan, control)
            self.assertTrue(row['passed'], row['live_take'])
            self.assertEqual(calls[1], (3 if latch else 4, 1))
            self.assertIn((2, 0), calls)
            self.assertEqual((5, 1) in calls, bool(latch))
            self.assertEqual(row['live_take']['take_sha256'], hashlib.sha256(b'FBKK take').hexdigest())
            self.assertIsNone(row['voices'])

    def test_failures_disarm_stop_and_report(self):
        plan = quick_plan()
        control, calls = self.native(plan)
        with self.assertRaisesRegex(RuntimeError, 'sender failed'):
            self.run_native(plan, control, out=Outgoing(fail_at=1))
        self.assertEqual(calls[-3:-1], [(5, 1), (2, 0)])
        late = quick_plan();late['info']['loop_s'] = 0
        control, calls = self.native(late)
        with self.assertRaisesRegex(RuntimeError, 'missed its pass'):
            self.run_native(late, control)
        self.assertIn((2, 0), calls)
        control, calls = self.native(plan, fail_op=5, cleanup_fails=True)
        with self.assertRaisesRegex(RuntimeError, 'cleanup also failed') as caught:
            self.run_native(plan, control)
        self.assertIsInstance(caught.exception.__cause__, OSError)
        stuck = quick_plan(latch=0);stuck['info']['loop_s'] = -3
        control, calls = self.native(stuck, one_pass_polls=10**9)
        with self.assertRaisesRegex(RuntimeError, 'loop end'):
            self.run_native(stuck, control)
        self.assertEqual(calls[-3:-1], [(5, 1), (2, 0)])

    def test_validation_or_dropped_arm_fails_row_without_exception(self):
        plan = quick_plan()
        control, _ = self.native(plan)
        self.assertFalse(self.run_native(plan, control, compare=dict(passed=False))['passed'])
        slow = quick_plan(notes=((0, 30000, 0, 60, 100, 0),))
        control, _ = self.native(slow, drop_arm=True)
        self.assertFalse(self.run_native(slow, control)['passed'])

    def cli(self, extra, flags=None, drum=10, session_error=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory);package = root/'lab.fwsc';package.write_bytes(b'lab take')
            path = root/'manifest.json'
            path.write_text(json.dumps(dict(package=str(package), package_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
                editor_identity='lab take', flags=flags or dict(MELODEE_AUDIO_PROFILE=1, MELODEE_AUDIO_BENCH=1, MELODEE_ADMISSION=0))))
            inside = False;transfers = [];values = []
            def request(command, args=()):
                if command == 1:
                    return list(b'lab take\0')
                if command == 27:
                    return [0, 4]+[0]*28
                values.append(args);return []
            device = SimpleNamespace(request=request, silence=lambda: None, close=lambda: None)
            class Session:
                captured = True;preferences = bytes(604)
                def __init__(self):
                    self.d = device;self.result = dict(verified=False)
                def __enter__(self):
                    nonlocal inside
                    inside = True;return self
                def __exit__(self, *exc):
                    nonlocal inside
                    inside = False;self.result['verified'] = True
            def transfer(d, ident, raw):
                self.assertTrue(inside);transfers.append((ident, raw))
            meta = normal.metadata();meta.update(profiling=1, benchmark_support=1)
            import audio_cpu_device_test as baseline
            row = baseline.CpuBaselineTest().row()
            row.update(metadata=meta, voices_min=0, profile=dict(half=dict(ticks=100, calls=2, max_ticks=60)))
            globals_ = {key: k for k, key in enumerate(('G_BUDGET', 'G_CLOCK', 'G_ROUTE', 'G_MFXTYPE', 'G_MFX2TYPE', 'G_MFX3TYPE', 'G_BPM'))}
            plan = dict(base=b'take base', info=dict(snapshot_on_disk=False, notes=16))
            stdout = io.StringIO()
            with patch.object(sys, 'argv', ['cpu', '--build-manifest', str(path), '--output', str(root/'run'), *extra]), \
                 patch.object(sys.stdin, 'isatty', return_value=True), patch.object(cpu, 'ReliableDevice', return_value=device), \
                 patch.object(cpu, 'RuntimeSession', return_value=Session()), patch.object(cpu, 'metadata', return_value=meta), \
                 patch.object(cpu, 'param_ids', return_value=({}, globals_)), patch.object(cpu, 'run_take', return_value=row) as run, \
                 patch.object(cpu, 'run') as held, patch.object(cpu, 'write_ram', side_effect=transfer), \
                 patch.object(cpu, 'build_take', return_value=plan), patch.object(cpu, 'drum_channel', return_value=drum), \
                 patch.object(cpu, 'histogram', return_value=row['histogram']), contextlib.redirect_stdout(stdout), \
                 contextlib.redirect_stderr(io.StringIO()):
                try:
                    code = cpu.main()
                except (SystemExit, RuntimeError) as error:
                    code = error
            summary = json.loads((root/'run/summary.json').read_text()) if (root/'run/summary.json').exists() else None
            clock = [a for a in values if a[:2] == [1, globals_['G_CLOCK']]]
            return code, transfers, run, held, summary, clock, inside

    def test_cli_transfers_base_per_take_inside_guard_and_restores(self):
        code, transfers, run, held, summary, clock, inside = self.cli(['--repeat', '2', '--live-notes', '16'])
        self.assertEqual(code, 0)
        self.assertEqual(transfers, [(0, b'take base')]*2)
        self.assertEqual(run.call_count, 2);held.assert_not_called()
        self.assertTrue(summary['restoration']['verified'] and not inside)
        self.assertTrue(clock and all((a[2] | a[3] << 7)-8192 == 0 for a in clock))

    def test_cli_refuses_drum_channel_and_bad_options(self):
        code, transfers, run, _, summary, _, inside = self.cli(['--live-notes', '16'], drum=2)
        self.assertIsInstance(code, RuntimeError)
        self.assertEqual(transfers, []);run.assert_not_called()
        self.assertTrue(summary['restoration']['verified'] and not summary['passed'] and not inside)
        for extra in (['--live-notes', '16', '--recording-events', '64'], ['--live-notes', '16', '--sequence-bpm', '120.5'],
                      ['--live-notes', '256', '--live-tracks', 'single'], ['--live-notes', '16', '--take-tolerance-ms', '0'],
                      ['--live-notes', '16', '--live-base', '256', '--live-tracks', 'single']):
            code, transfers, *_ = self.cli(extra)
            self.assertIsInstance(code, SystemExit);self.assertEqual(transfers, [])
        code, transfers, *_ = self.cli(['--live-notes', '16'], flags=dict(MELODEE_AUDIO_PROFILE=1, MELODEE_AUDIO_BENCH=0, MELODEE_ADMISSION=0))
        self.assertIsInstance(code, SystemExit)


if __name__ == '__main__':
    unittest.main()
