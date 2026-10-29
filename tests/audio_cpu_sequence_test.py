#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Synthetic runtime construction, USB timing failures and guarded integration."""
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
import audio_cpu_sequence as sequence
import audio_cpu_device as cpu


class SequenceTest(unittest.TestCase):
    def native_device(self):
        state = [0, 1, 0, 0, 0, 0, 0, 0, 0, 0]
        requests = []
        def request(cmd, args):
            self.assertEqual(cmd, 101)
            requests.append(tuple(args))
            op, mask = args
            self.assertEqual(mask, 0)
            if op in (1, 2):
                state[8] = op
            elif state[8]:
                state[2] = int(state[8] == 1)
                state[8] = 0
            return state[:]
        return SimpleNamespace(request=request), state, requests

    def test_internal_transport_uses_native_queue_and_stops(self):
        device, state, requests = self.native_device()
        with sequence.InternalClock(device) as clock:
            self.assertTrue(state[2])
            self.assertTrue(clock.result['started'])
        self.assertFalse(state[2])
        self.assertTrue(clock.result['passed'])
        self.assertEqual(requests, [(0, 0), (1, 0), (0, 0), (2, 0), (0, 0)])

    def test_internal_transport_rejects_busy_or_unknown_schema_without_starting(self):
        for index, value in ((2, 1), (3, 1), (4, 1), (8, 1), (9, 1)):
            device, state, requests = self.native_device();state[index] = value
            # Do not consume a pre-existing pending request in this test model.
            device.request = lambda cmd, args: (requests.append(tuple(args)), state[:])[1]
            with self.assertRaisesRegex(RuntimeError, 'stopped/disarmed'):
                with sequence.InternalClock(device):
                    pass
            self.assertEqual(requests, [(0, 0)])
        for reply in ([], [3, 1]+[0]*8, [0, 2]+[0]*8, [0, 1, 2]+[0]*7, [0, 1]+[0]*7+[3]):
            with self.assertRaises(RuntimeError):
                sequence.lab_control(SimpleNamespace(request=lambda *args: reply))

    def test_internal_failed_start_ack_and_body_failure_both_stop(self):
        for fail_start in (True, False):
            device, state, requests = self.native_device();native = device.request
            def request(cmd, args):
                result = native(cmd, args)
                if fail_start and args[0] == 1:
                    raise TimeoutError('lost native start reply')
                return result
            device.request = request
            with self.assertRaises((TimeoutError, ValueError)):
                with sequence.InternalClock(device):
                    raise ValueError('injected workload failure')
            self.assertFalse(state[2] or state[8])
            self.assertIn((2, 0), requests)
            self.assertEqual(requests.count((1, 0)), 1)

    def test_internal_lost_idempotent_acks_retry_but_start_never_resends(self):
        device, state, requests = self.native_device();native = device.request
        lost = {0: 1, 2: 1}
        def request(cmd, args):
            result = native(cmd, args)
            if lost.get(args[0]):
                lost[args[0]] -= 1
                raise TimeoutError('lost idempotent reply')
            return result
        device.request = request
        with patch.object(sequence.time, 'sleep'):
            with sequence.InternalClock(device) as clock:
                self.assertTrue(state[2])
        self.assertTrue(clock.result['passed'] and not state[2] and not state[8])
        self.assertEqual(requests.count((1, 0)), 1)
        self.assertEqual(requests.count((2, 0)), 2)
        # A persistently lost Stop still fails after bounded retries.
        device, state, requests = self.native_device();native = device.request
        def no_stop_reply(cmd, args):
            result = native(cmd, args)
            if args[0] == 2:
                raise TimeoutError('no Stop reply')
            return result
        device.request = no_stop_reply
        with patch.object(sequence.time, 'sleep'), self.assertRaises(TimeoutError):
            with sequence.InternalClock(device):
                pass
        self.assertEqual(requests.count((2, 0)), 3)
        self.assertEqual(requests.count((1, 0)), 1)

    def test_internal_start_and_cleanup_failure_reports_both(self):
        device, state, requests = self.native_device()
        def request(cmd, args):
            requests.append(tuple(args))
            if args[0] == 1:
                raise TimeoutError('lost native start reply')
            if args[0] == 2:
                raise OSError('device disconnected')
            return state[:]
        device.request = request
        with self.assertRaisesRegex(RuntimeError, 'Start failed.*cleanup Stop also failed') as caught:
            with sequence.InternalClock(device):
                pass
        self.assertIsInstance(caught.exception.__cause__, TimeoutError)
        self.assertEqual(requests, [(0, 0), (1, 0), (2, 0)])

    def test_internal_missing_transition_and_stop_failure_prevent_pass(self):
        device, state, requests = self.native_device();native = device.request
        now = 0
        def sleep(delay):
            nonlocal now
            now += delay
        def stuck(cmd, args):
            requests.append(tuple(args))
            if args[0] == 2:
                state[8] = 0
            elif args[0] == 1:
                state[8] = 1
            return state[:]
        device.request = stuck
        clock = sequence.InternalClock(device)
        with patch.object(sequence.time, 'monotonic', lambda: now), patch.object(sequence.time, 'sleep', sleep), self.assertRaisesRegex(RuntimeError, 'transition'):
            with clock:
                pass
        self.assertFalse(clock.result['passed'])
        device.request = native
        clock = sequence.InternalClock(device)
        state[8] = 0
        with self.assertRaisesRegex(OSError, 'stop failed'):
            with clock:
                device.request = lambda *args: (_ for _ in ()).throw(OSError('stop failed'))
        self.assertFalse(clock.result['passed'])

    def clock(self, jitter=0, send_delay=0, fail=False):
        self.now = 0.0
        sent = []
        class Event:
            stopped = False
            def is_set(event):
                return event.stopped
            def set(event):
                event.stopped = True
            def wait(event, delay):
                self.now += delay+jitter
                return event.stopped
        event = Event()
        def send(message):
            sent.append(message.type)
            if message.type == 'clock':
                self.now += send_delay
                if fail:
                    raise OSError('lost clock port')
                if sent.count('clock') == 8:
                    event.set()
        clock = sequence.USBClock(SimpleNamespace(send=send), 120, 2)
        clock.stop_event = event
        class Thread:
            def __init__(thread, **options):
                thread.target = options['target']
            def start(thread):
                thread.target()
            def join(thread, timeout):
                pass
            def is_alive(thread):
                return False
        with patch.object(sequence.time, 'monotonic', lambda: self.now), patch.object(sequence.threading, 'Thread', Thread):
            if fail:
                with self.assertRaisesRegex(RuntimeError, 'lost clock port'):
                    with clock:
                        pass
            else:
                with clock:
                    pass
        return clock.result, sent

    def test_clock_24ppqn_and_clean_shutdown(self):
        result, sent = self.clock()
        self.assertTrue(result['passed'])
        self.assertEqual(result['pulses'], 8)
        self.assertEqual(sent, ['start']+['clock']*8+['stop'])
        self.assertAlmostEqual(self.now, 7*60/(120*24))

    def test_jitter_driver_delay_and_skipped_pulses_are_visible(self):
        for jitter, delay in ((.003, 0), (0, .003), (.03, 0), (0, .03)):
            result, sent = self.clock(jitter, delay)
            self.assertFalse(result['passed'])
            self.assertGreater(result['max_lateness_ms'], 2)
            if max(jitter, delay) > .02:
                self.assertGreater(result['missed_pulses'], 0)
            self.assertTrue(result['stopped'])
            self.assertEqual(sent[-1], 'stop')

    def test_clock_send_failure_stops_and_propagates(self):
        result, sent = self.clock(fail=True)
        self.assertFalse(result['passed'])
        self.assertEqual(sent[-1], 'stop')

    def test_clock_parameters_rejected_before_start(self):
        for bpm, jitter in ((39, 2), (241, 2), (float('nan'), 2), (120, 0), (120, 50), (120, float('inf'))):
            with self.assertRaises(ValueError):
                sequence.USBClock(None, bpm, jitter)

    def test_real_fixture_is_bounded_and_stays_in_memory(self):
        source = sequence.ROOT/'firmware/src/core.h'
        manifest = dict(firmware_sha256={str(source): hashlib.sha256(source.read_bytes()).hexdigest()})
        raw, info = sequence.build_fixture(1024, 'free', 'all', 'long', '0124', manifest)
        self.assertEqual(raw[:4], b'FBKP')                    # (16 patterns a part: the FBKP record)
        self.assertEqual(int.from_bytes(raw[4:8], 'little'), len(raw))
        self.assertEqual(info['sha256'], hashlib.sha256(raw).hexdigest())
        self.assertEqual(info['events'], 1024)
        self.assertFalse(info['snapshot_on_disk'])
        self.assertLess(len(raw), 256*1024)

    def test_builder_checks_source_and_configuration_before_compilation(self):
        path = sequence.ROOT/'firmware/src/core.h'
        for manifest in ({}, dict(firmware_sha256={str(path): 'mismatch'})):
            with patch.object(sequence.subprocess, 'run') as run, self.assertRaisesRegex(RuntimeError, 'source'):
                sequence.build_fixture(64, 'grid', 'single', 'short', '0124', manifest)
            run.assert_not_called()
        with self.assertRaises(ValueError):
            sequence.build_fixture(65, 'grid', 'single', 'short', '0124', {})

    def test_armed_or_unknown_track_schema_rejected(self):
        for reply in ([], [0, 4]+[0]*27, [0, 4]+[0]*5+[1]+[0]*22):
            with self.assertRaisesRegex(RuntimeError, 'disarmed'):
                sequence.require_disarmed(SimpleNamespace(request=lambda command: reply))
        sequence.require_disarmed(SimpleNamespace(request=lambda command: [0, 4]+[0]*28))

    def test_playback_collects_variable_voices_and_does_not_send_held_notes(self):
        import audio_cpu_stress_test as normal
        now = 0.0
        rows = []
        def sleep(delay):
            nonlocal now
            now += delay
        def stats(d, reset=False):
            row = normal.snapshot(len(rows))
            row.update(resource_peak=1000, resource_capacity=2000, voices_active=0 if not len(rows)%2 else 3)
            rows.append(row)
            return row
        device = SimpleNamespace(diagnostic_retries=0, cpu_sequence_bpm=120, cpu_clock_jitter_ms=2,
                                 outgoing=None, request=lambda command: [0, 4]+[0]*28)
        clock = SimpleNamespace(result=dict(passed=True, pulses=16))
        for clock_ok in (True, False):
            rows.clear();now = 0;clock.result['passed'] = clock_ok
            with patch.object(sequence.time, 'monotonic', lambda: now), patch.object(sequence.time, 'sleep', sleep), \
                 patch.object(sequence, 'stats', stats), patch.object(sequence, 'metadata', return_value=normal.metadata()), \
                 patch.object(sequence, 'profile', return_value={}), patch.object(sequence, 'setup', return_value=[]) as setup, \
                 patch.object(sequence, 'USBClock', return_value=contextlib.nullcontext(clock)):
                result = sequence.run_sequence(device, [0, 1, 2, 4], [2]*4, 0, .25, False)
            self.assertEqual(result['passed'], clock_ok)
            self.assertEqual(result['voices_min'], 0)
            self.assertEqual(result['voices_max'], 3)
            self.assertIsNone(result['voices_expected'])
            self.assertIsNone(result['voices'])
            setup.assert_called_once_with(device, [0, 1, 2, 4], [0]*4, 0)
        with self.assertRaises(ValueError):
            sequence.run_sequence(device, [0]*4, [1]*4, 0, 1, True)

    def test_internal_playback_samples_running_state_and_rejects_lost_transport(self):
        import audio_cpu_stress_test as normal
        now = 0
        def sleep(delay):
            nonlocal now
            now += delay
        device = SimpleNamespace(diagnostic_retries=0, cpu_sequence_clock='internal', request=lambda cmd: [0, 4]+[0]*28)
        good = dict(playing=1, counting=0, rec=0, pending=0, clock=0)
        for playing in (0, 1):
            now = 0
            clock = SimpleNamespace(result=dict(passed=True, source='internal'))
            row = normal.snapshot(0);row.update(voices_active=3, resource_peak=1000, resource_capacity=2000)
            with patch.object(sequence.time, 'monotonic', lambda: now), patch.object(sequence.time, 'sleep', sleep), \
                 patch.object(sequence, 'stats', return_value=row), patch.object(sequence, 'metadata', return_value=normal.metadata()), \
                 patch.object(sequence, 'profile', return_value={}), patch.object(sequence, 'setup', return_value=[]), \
                 patch.object(sequence, 'InternalClock', return_value=contextlib.nullcontext(clock)), \
                 patch.object(sequence, 'USBClock') as usb, patch.object(sequence, 'lab_control', return_value=dict(good, playing=playing)):
                result = sequence.run_sequence(device, [0]*4, [0]*4, 0, .25, False)
            self.assertEqual(result['passed'], bool(playing))
            self.assertGreater(result['native_transport_samples'], 0)
            self.assertIsNone(result['external_usb_clock'])
            usb.assert_not_called()

    def test_screen_cli_sends_fixture_only_inside_guard_and_restores(self):
        import audio_cpu_stress_test as normal
        import audio_cpu_device_test as baseline
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory);package = root/'lab.fwsc';package.write_bytes(b'lab fixture')
            manifest = root/'manifest.json'
            manifest.write_text(json.dumps(dict(package=str(package), package_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
                editor_identity='lab fixture', flags=dict(MELODEE_AUDIO_PROFILE=1, MELODEE_AUDIO_BENCH=1, MELODEE_ADMISSION=0))))
            inside = False;transfers = [];values = []
            def request(command, args=()):
                if command == 1:
                    return list(b'lab fixture\0')
                if command == 27:
                    return [0, 4]+[0]*28
                values.append(args);return []
            device = SimpleNamespace(request=request, silence=lambda: None, close=lambda: None)
            class Session:
                captured = True
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
            row = baseline.CpuBaselineTest().row()
            row.update(metadata=meta, voices_min=0, profile=dict(half=dict(ticks=100, calls=2, max_ticks=60)))
            globals_ = {key: k for k, key in enumerate(('G_BUDGET', 'G_CLOCK', 'G_ROUTE', 'G_MFXTYPE', 'G_MFX2TYPE', 'G_MFX3TYPE', 'G_BPM'))}
            with patch.object(sys, 'argv', ['cpu', '--build-manifest', str(manifest), '--output', str(root/'run'), '--repeat', '1', '--recording-events', '1024']), \
                 patch.object(sys.stdin, 'isatty', return_value=True), patch.object(cpu, 'ReliableDevice', return_value=device), \
                 patch.object(cpu, 'RuntimeSession', return_value=Session()), patch.object(cpu, 'metadata', return_value=meta), \
                 patch.object(cpu, 'param_ids', return_value=({}, globals_)), patch.object(cpu, 'run_sequence', return_value=row) as run, \
                 patch.object(cpu, 'run') as held, patch.object(cpu, 'write_ram', side_effect=transfer), \
                 patch.object(cpu, 'build_fixture', return_value=(b'synthetic FBKK', dict(events=1024, snapshot_on_disk=False))), \
                 patch.object(cpu, 'histogram', return_value=row['histogram']), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(cpu.main(), 0)
            self.assertEqual(transfers, [(0, b'synthetic FBKK')])
            self.assertFalse(inside)
            run.assert_called_once();held.assert_not_called()
            report = json.loads((root/'run/run.json').read_text())
            self.assertEqual(report['recording_fixture']['events'], 1024)
            self.assertFalse(report['recording_fixture']['snapshot_on_disk'])
            self.assertTrue(json.loads((root/'run/summary.json').read_text())['restoration']['verified'])
            clock_write = next(args for args in values if args[:2] == [1, globals_['G_CLOCK']])
            self.assertEqual((clock_write[2] | clock_write[3]<<7)-8192, 1)


if __name__ == '__main__':
    unittest.main()
