#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Duplex/stem capture and final screening evidence without USB hardware."""
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
import wave
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import audio_cpu_usb as usb
import audio_cpu_device as cpu
import audio_cpu_stress as stress
import audio_cpu_device_test as baseline_test


class Driver:
    def __init__(self, channels, callbacks=True, status='', close_error=False, wrong_bytes=False):
        self.channels = channels
        self.callbacks = callbacks
        self.status = status
        self.close_error = close_error
        self.wrong_bytes = wrong_bytes
        self.opens = []
    def query_devices(self):
        return [dict(name='Melodee', max_input_channels=self.channels, max_output_channels=2)]
    def stream(self, capture, **options):
        @contextlib.contextmanager
        def stream():
            self.opens.append((capture, options))
            if self.callbacks:
                count = 8 * options['channels'] * (3 if options['dtype'] == 'int24' else 2)
                data = bytearray(range(count))
                if self.wrong_bytes and capture:
                    data = data[:-options['channels']*3]
                options['callback'](data, 8, {}, self.status)
            try:
                yield
            finally:
                if self.close_error and capture:
                    os.write(2, b'CoreAudio error -10863 at close\n')
                    raise RuntimeError('stream close failure')
        return stream()
    def RawInputStream(self, **options):
        return self.stream(True, **options)
    def RawOutputStream(self, **options):
        return self.stream(False, **options)


class UsbCaptureTest(unittest.TestCase):
    def capture(self, path, mode='stereo', driver=None):
        driver = driver or Driver(4 if mode == 'stems' else 2)
        with patch.dict(sys.modules, sounddevice=driver), patch.object(usb.time, 'sleep'):
            with usb.capture_driver_stderr(path.parent/'driver-stderr.log'), usb.usb_streams(path, 'int24', mode) as statuses:
                self.assertIsInstance(statuses, list)
        return driver, statuses, usb.capture_result(path, mode)

    def test_exact_layout_and_sample_interleaving(self):
        for mode, channels in (('stereo', 2), ('stems', 4)):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)/'capture.wav'
                driver, statuses, result = self.capture(path, mode)
                self.assertTrue(result['passed'])
                self.assertEqual(statuses, [])
                self.assertEqual(result['capture_frames'], 8)
                self.assertEqual(result['playback_frames'], 8)
                self.assertEqual(driver.opens[0][1]['channels'], channels)
                self.assertEqual(driver.opens[1][1]['channels'], 2)
                with wave.open(str(path), 'rb') as wav:
                    self.assertEqual(wav.getnchannels(), channels)
                    self.assertEqual(wav.getsampwidth(), 3)
                    self.assertEqual(wav.readframes(8), bytes(range(8*channels*3)))

    def test_layout_mismatch_rejected_before_any_stream(self):
        for mode, channels in (('stereo', 4), ('stems', 2), ('stereo', 1)):
            with tempfile.TemporaryDirectory() as directory:
                driver = Driver(channels)
                with self.assertRaisesRegex(RuntimeError, 'existing .*channel Melodee input'):
                    self.capture(Path(directory)/'capture.wav', mode, driver)
                self.assertEqual(driver.opens, [])

    def test_callback_status_or_absent_frames_never_passes(self):
        for driver in (Driver(4, status='input overflow'), Driver(4, callbacks=False), Driver(4, wrong_bytes=True)):
            with tempfile.TemporaryDirectory() as directory:
                _, _, result = self.capture(Path(directory)/'capture.wav', 'stems', driver)
                self.assertFalse(result['passed'])

    def test_close_error_is_captured_and_stream_stays_failed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory);path = root/'capture.wav'
            with self.assertRaisesRegex(RuntimeError, 'stream close failure'):
                self.capture(path, 'stems', Driver(4, close_error=True))
            result = usb.capture_result(path, 'stems')
            self.assertFalse(result['passed'])
            self.assertFalse(result['closed'])
            self.assertIn(b'-10863', (root/'driver-stderr.log').read_bytes())

    def test_missing_corrupt_or_wrong_capture_evidence_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'capture.wav'
            self.assertFalse(usb.capture_result(path, 'stems')['passed'])
            self.capture(path, 'stems')
            self.assertFalse(usb.capture_result(path, 'stereo')['passed'])
            path.with_suffix('.status.json').write_text('{')
            self.assertFalse(usb.capture_result(path, 'stems')['passed'])
        self.assertTrue(usb.capture_result(Path('/unused'), 'inactive')['passed'])

    def test_screen_finalizes_all_rows_after_driver_shutdown(self):
        for driver_error in (False, True):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.capture(root/'capture.wav', 'stems')
                if driver_error:
                    (root/'driver-stderr.log').write_bytes(b'native driver close error')
                row = baseline_test.CpuBaselineTest().row()
                row.update(usb_play_alt=1, usb_capture_alt=1, host_audio_pending=True)
                cpu.assess_row(row, 'stems')
                with (root/'cases-pending.jsonl').open('w') as f:
                    for _ in range(3):
                        f.write(json.dumps(row)+'\n')
                summary = cpu.finalize_cases(root, 'stems', [])
                self.assertEqual(summary['passed'], not driver_error)
                self.assertEqual(summary['cases'], 3)
                rows = [json.loads(line) for line in (root/'cases.jsonl').read_text().splitlines()]
                for final in rows:
                    self.assertFalse(final['host_audio_pending'])
                    self.assertEqual(final['passed'], not driver_error)
                    self.assertEqual(final['stats_peak_complete_upper_us'], 1800)

    def test_normal_observer_accepts_stems_as_active_duplex(self):
        from audio_cpu_stress_test import StressTest
        test = StressTest()
        def streaming(row, count):
            row['play_alt'] = row['cap_alt'] = 1
        result = test.run_observer(seconds=2, usb='stems', mutate=streaming)
        self.assertTrue(result['passed'])
        self.assertTrue(result['observed_active_voices'])
        self.assertNotIn('observed_audio', result)

    def test_stress_cli_records_clean_stems_and_does_not_edit_music(self):
        from audio_cpu_stress_test import metadata
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory);package = root/'normal.fwsc';package.write_bytes(b'normal fixture')
            manifest = root/'manifest.json'
            manifest.write_text(json.dumps(dict(package=str(package), package_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
                editor_identity='normal fixture', flags=dict(MELODEE_AUDIO_PROFILE=0, MELODEE_AUDIO_BENCH=0, MELODEE_ADMISSION=0))))
            commands = []
            device = SimpleNamespace(request=lambda command: commands.append(command) or list(b'normal fixture\0'), close=lambda: None)
            with patch.object(sys, 'argv', ['stress', '--build-manifest', str(manifest), '--output', str(root/'run'), '--usb', 'stems']), \
                 patch.object(stress, 'ReliableDevice', return_value=device), patch.object(stress, 'metadata', return_value=metadata()), \
                 patch.object(stress, 'observe', return_value=dict(passed=True)), patch.dict(sys.modules, sounddevice=Driver(4)), \
                 patch.object(usb.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(stress.main(), 0)
            result = json.loads((root/'run/summary.json').read_text())
            self.assertTrue(result['passed'])
            self.assertEqual(result['host_capture']['wave_channels'], 4)
            self.assertEqual(commands, [1])

    def test_duplex_without_device_packet_progress_fails(self):
        row = baseline_test.CpuBaselineTest().row()
        row.update(usb_play_alt=1, usb_capture_alt=1)
        for key in ('rx_packets', 'tx_packets'):
            for mode in ('stereo', 'stems'):
                test = dict(row, delta=dict(row['delta'], **{key: 0}))
                cpu.assess_row(test, mode)
                self.assertFalse(test['passed'])
        from audio_cpu_stress_test import StressTest
        def stagnant(row, count):
            row['play_alt'] = row['cap_alt'] = 1
            row['tx_packets'] = 0
        self.assertFalse(StressTest().run_observer(seconds=2, usb='stems', mutate=stagnant)['passed'])

    def test_truncated_wave_data_fails_even_with_valid_header(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'capture.wav'
            self.capture(path, 'stems')
            path.write_bytes(path.read_bytes()[:-1])
            self.assertFalse(usb.capture_result(path, 'stems')['passed'])

    def test_screen_cli_finalizes_after_restoration_or_preserves_provisional_failure(self):
        from audio_cpu_stress_test import metadata
        import hashlib
        for close_error in (False, True):
            with self.subTest(close_error=close_error), tempfile.TemporaryDirectory() as directory:
                root = Path(directory);package = root/'lab.fwsc';package.write_bytes(b'lab fixture')
                manifest = root/'manifest.json'
                manifest.write_text(json.dumps(dict(package=str(package), package_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
                    editor_identity='lab fixture', flags=dict(MELODEE_AUDIO_PROFILE=1, MELODEE_AUDIO_BENCH=1, MELODEE_ADMISSION=0))))
                device = SimpleNamespace(request=lambda command, args=(): list(b'lab fixture\0') if command == 1 else [],
                                         silence=lambda: None, close=lambda: None)
                class Session:
                    captured = True
                    def __init__(self):
                        self.d = device;self.result = dict(verified=False);self.exits = 0
                    def __enter__(self):
                        return self
                    def __exit__(self, *exc):
                        self.result['verified'] = True;self.exits += 1
                session = Session()
                meta = metadata();meta.update(profiling=1, benchmark_support=1)
                row = baseline_test.CpuBaselineTest().row()
                row.update(usb_play_alt=1, usb_capture_alt=1, metadata=meta, voices_min=3,
                           profile=dict(half=dict(ticks=100, calls=2, max_ticks=60)))
                histogram = row['histogram']
                globals_ = {key: k for k, key in enumerate(('G_BUDGET', 'G_CLOCK', 'G_ROUTE', 'G_MFXTYPE', 'G_MFX2TYPE', 'G_MFX3TYPE'))}
                with patch.object(sys, 'argv', ['cpu', '--build-manifest', str(manifest), '--output', str(root/'run'), '--usb', 'stems', '--repeat', '1']), \
                     patch.object(sys.stdin, 'isatty', return_value=True), patch.object(cpu, 'ReliableDevice', return_value=device), \
                     patch.object(cpu, 'RuntimeSession', return_value=session), patch.object(cpu, 'metadata', return_value=meta), \
                     patch.object(cpu, 'param_ids', return_value=({}, globals_)), patch.object(cpu, 'run', return_value=row), \
                     patch.object(cpu, 'histogram', return_value=histogram), patch.dict(sys.modules, sounddevice=Driver(4, close_error=close_error)), \
                     patch.object(usb.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
                    if close_error:
                        with self.assertRaisesRegex(RuntimeError, 'stream close failure'):
                            cpu.main()
                    else:
                        self.assertEqual(cpu.main(), 0)
                summary = json.loads((root/'run/summary.json').read_text())
                self.assertEqual(summary['passed'], not close_error)
                self.assertTrue(summary['restoration']['verified'])
                self.assertEqual(session.exits, 1)
                if close_error:
                    self.assertFalse((root/'run/cases.jsonl').exists())
                    provisional = json.loads((root/'run/cases-pending.jsonl').read_text())
                    self.assertTrue(provisional['host_audio_pending'])
                else:
                    final = json.loads((root/'run/cases.jsonl').read_text())
                    self.assertFalse(final['host_audio_pending'])
                    self.assertTrue(final['passed'])


if __name__ == '__main__':
    unittest.main()
