#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Normal-firmware stress observation without hardware or wall-clock waits."""
import contextlib
import hashlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import audio_cpu_stress as stress


def metadata():
    return dict(clock_mhz=360, half_frames=128, profiling=0, benchmark_support=0,
                admission=0, budget_points=32, timer5_max_ticks=240, caps=[16]*4)


def snapshot(count=0):
    return dict(dict.fromkeys((*stress.COUNTERS, 'bench_shed_requests'), 0),
                audio_full_max_us=1800, voices_active=3, core1_online=1,
                fixed_voice_bench=0, play_alt=0, cap_alt=0, rx_packets=count*1000, tx_packets=count*900)


class StressTest(unittest.TestCase):
    def run_observer(self, seconds=600, interval=1, usb='inactive', minimum=0, mutate=None, change_metadata=None):
        self.clock = 0
        self.reads = -1
        self.rows = []
        self.device = SimpleNamespace(diagnostic_retries=0)
        def sleep(amount):
            self.assertGreaterEqual(amount, 0)
            self.clock += amount
        def stats(device, reset=False):
            self.assertIs(device, self.device)
            self.reads += 1
            self.assertEqual(reset, self.reads == 0)
            row = snapshot(self.reads)
            if mutate:
                mutate(row, self.reads)
            return row
        def meta(device):
            row = metadata()
            if change_metadata:
                change_metadata(row, self.reads)
            return row
        with patch.object(stress.time, 'monotonic', lambda: self.clock), \
             patch.object(stress.time, 'sleep', sleep), patch.object(stress, 'stats', stats), \
             patch.object(stress, 'metadata', meta):
            return stress.observe(self.device, seconds, interval, usb, minimum, self.rows.append)

    def test_ten_minutes_continuous_no_profile_or_workload_edits(self):
        result = self.run_observer()
        self.assertTrue(result['passed'])
        self.assertEqual(result['seconds'], 600)
        self.assertEqual(result['samples'], 600)
        self.assertEqual(result['delta']['rx_packets'], 600000)
        self.assertEqual(result['delta']['tx_packets'], 540000)
        self.assertEqual(self.reads, 600)  # one reset, no per-window resets/restarts
        self.assertEqual(result['peak_complete_upper_us'], 1800)
        self.assertTrue(result['metadata_stable'])

    def test_short_window_and_last_partial_poll(self):
        result = self.run_observer(seconds=1.25, interval=1)
        self.assertEqual([r['elapsed_seconds'] for r in self.rows], [1, 1.25])
        self.assertTrue(result['passed'])

    def test_sequence_silence_and_static_voice_floor(self):
        def gaps(row, count):
            row['voices_active'] = count % 4
        self.assertTrue(self.run_observer(seconds=5, mutate=gaps)['passed'])
        self.assertFalse(self.run_observer(seconds=5, minimum=1, mutate=gaps)['passed'])
        def empty(row, count):
            row['voices_active'] = 0
        result = self.run_observer(seconds=2, mutate=empty)
        self.assertFalse(result['passed'])
        self.assertFalse(result['observed_active_voices'])

    def test_all_faults_and_complete_timing_bound(self):
        for fault in (*stress.FAULTS, 'bench_shed_requests'):
            with self.subTest(fault=fault):
                def change(row, count):
                    row[fault] = count > 0
                self.assertFalse(self.run_observer(seconds=2, mutate=change)['passed'])
        for field, value in (('audio_full_max_us', stress.DEADLINE_US*.81),
                             ('core1_online', 0), ('fixed_voice_bench', 1)):
            def change(row, count):
                if count:
                    row[field] = value
            self.assertFalse(self.run_observer(seconds=2, mutate=change)['passed'], field)

    def test_counter_wrap_is_accumulated_per_poll(self):
        def wrapping(row, count):
            row['rx_packets'] = (0xfffffff0+count*1000) & 0xffffffff
        result = self.run_observer(seconds=3, mutate=wrapping)
        self.assertTrue(result['passed'])
        self.assertEqual(result['delta']['rx_packets'], 3000)

    def test_wrong_build_rejected_before_reset(self):
        for field, value in (('profiling', 1), ('benchmark_support', 1), ('clock_mhz', 396),
                             ('half_frames', 64), ('timer5_max_ticks', 2400)):
            def change(row, count):
                row[field] = value
            with self.subTest(field=field), self.assertRaisesRegex(RuntimeError, 'Requires normal'):
                self.run_observer(seconds=2, change_metadata=change)
            self.assertEqual(self.reads, -1)

    def test_midrun_configuration_changes_fail(self):
        for field, value in (('budget_points', 16), ('profiling', 1), ('admission', 1), ('clock_mhz', 396)):
            def change(row, count):
                if count >= 1:
                    row[field] = value
            result = self.run_observer(seconds=2, change_metadata=change)
            self.assertFalse(result['passed'], field)
            self.assertFalse(result['metadata_stable'], field)

    def test_usb_alternates_and_final_host_status(self):
        self.assertFalse(self.run_observer(seconds=2, usb='stereo')['passed'])
        def streaming(row, count):
            row['play_alt'] = row['cap_alt'] = 1
        result = self.run_observer(seconds=2, usb='stereo', mutate=streaming)
        self.assertTrue(result['passed'])
        self.assertFalse(self.run_observer(seconds=2, mutate=streaming)['passed'])
        stress.finalize_host_audio(result, ['input overflow'], 0)
        self.assertFalse(result['passed'])
        result['passed'] = True
        stress.finalize_host_audio(result, [], 15)
        self.assertFalse(result['passed'])

    def test_native_stderr_is_saved_and_original_fd_restored(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'driver-stderr.log'
            with stress.capture_driver_stderr(path):
                os.write(2, b'CoreAudio error -10863\n')
            self.assertEqual(path.read_bytes(), b'CoreAudio error -10863\n')
            self.assertTrue(os.fstat(2))

    def test_cli_keeps_music_running_and_saves_failure_evidence(self):
        for fail in (False, True):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                package = root/'normal.fwsc';package.write_bytes(b'normal firmware fixture')
                manifest = root/'manifest.json'
                manifest.write_text(json.dumps(dict(package=str(package), package_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
                    editor_identity='MELODEE normal fixture', flags=dict(MELODEE_AUDIO_PROFILE=0, MELODEE_AUDIO_BENCH=0, MELODEE_ADMISSION=0))))
                class Device:
                    def __init__(self):
                        self.commands = [];self.closed = False
                    def request(self, command):
                        self.commands.append(command)
                        return list(b'MELODEE normal fixture\0')
                    def close(self):
                        self.closed = True
                device = Device()
                @contextlib.contextmanager
                def streaming(*args):
                    try:
                        yield []
                    finally:
                        # Native shutdown errors must also be captured.
                        os.write(2, b'CoreAudio error -10863\n')
                result = dict(passed=True)
                argv = ['stress', '--build-manifest', str(manifest), '--output', str(root/'run'), '--usb', 'stereo']
                with patch.object(sys, 'argv', argv), patch.object(stress, 'ReliableDevice', return_value=device), \
                     patch.object(stress, 'metadata', return_value=metadata()), patch.object(stress, 'usb_streams', streaming), \
                     patch.object(stress, 'observe', side_effect=RuntimeError('lost diagnostics') if fail else None, return_value=result), \
                     contextlib.redirect_stdout(io.StringIO()):
                    if fail:
                        with self.assertRaisesRegex(RuntimeError, 'lost diagnostics'):
                            stress.main()
                    else:
                        self.assertEqual(stress.main(), 1) # native driver error rejects success
                summary = json.loads((root/'run/summary.json').read_text())
                self.assertFalse(summary['passed'])
                self.assertGreater(summary['host_audio_stderr_bytes'], 0)
                self.assertEqual(device.commands, [1]) # no silence, transport, params or persistence
                self.assertTrue(device.closed)
                if fail:
                    self.assertIn('lost diagnostics', summary['error'])

    def test_invalid_duration_polling_and_mode(self):
        device = SimpleNamespace()
        for seconds, interval, usb, minimum in ((float('nan'), 1, 'inactive', 0),
                (0, 1, 'inactive', 0), (1, 0, 'inactive', 0), (1, 6, 'inactive', 0),
                (1, 1, 'unknown', 0), (1, 1, 'inactive', -1)):
            with self.assertRaises(ValueError):
                stress.observe(device, seconds, interval, usb, minimum)


if __name__ == '__main__':
    unittest.main()
