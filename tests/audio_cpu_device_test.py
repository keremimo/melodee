#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Exercise restoration failure paths without a device or persistent fixture."""
import sys
import unittest
import zlib
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import audio_cpu_device as cpu
import audio_performance_device as legacy


class Device:
    def __init__(self):
        self.guarded = False
        self.objects = {0: bytes(range(256))*3, 1: bytes(cpu.PREFERENCE_BYTES)}
        self.stored = [[1, 604, 123, 5]]
        self.staging = bytearray()
        self.fail_restore = False
        self.corrupt_read = False

    def silence(self):
        pass

    def inventory(self):
        return [(i, len(b), zlib.crc32(b)) for i, b in self.objects.items()]

    def request(self, cmd, a=()):
        if cmd == 98 and a[0] == 3:
            if len(a) == 2:
                self.guarded = bool(a[1])
            return [0, 3, int(self.guarded)]
        if cmd == 98 and a[0] == 4:
            return [0, 4, len(self.stored)] + [v for present, *rest in self.stored
                for v in [present, *(x for value in rest for x in cpu.u32(value))]]
        if cmd == 66:
            ident, off, count = a[0], cpu.r32(a[1:6]), a[6] + (a[7] << 7)
            raw = self.objects[ident][off:off+count]
            if self.corrupt_read:
                raw = bytes([raw[0] ^ 1])+raw[1:]
            return [ident, 0, *cpu.u32(off), count & 127, count >> 7, *cpu.pack(raw)]
        if cmd == 67:
            if self.fail_restore:
                raise RuntimeError('injected restoration failure')
            op, ident = a[:2]
            if op == 0:
                self.staging = bytearray()
            elif op == 1:
                self.staging.extend(cpu.unpack(a[7:]))
            elif op == 2:
                self.objects[ident] = bytes(self.staging)
            return [op, ident, 0]
        raise AssertionError((cmd, a))


class CpuBaselineTest(unittest.TestCase):
    def session(self, d):
        return patch.object(cpu, 'inventory', lambda device: device.inventory())

    def test_restoration_on_workload_exception(self):
        d = Device(); original = dict(d.objects)
        with self.session(d):
            session = cpu.RuntimeSession(d)
            with self.assertRaisesRegex(ValueError, 'workload'):
                with session:
                    d.objects[0] = b'temporary music'
                    d.objects[1] = b'changed preferences'
                    raise ValueError('workload failed')
        self.assertEqual(d.objects, original)
        self.assertFalse(d.guarded)
        self.assertTrue(session.result['verified'])

    def test_restore_failure_keeps_persistence_guarded(self):
        d = Device()
        with self.session(d):
            with self.assertRaisesRegex(RuntimeError, 'restoration failure'):
                with cpu.RuntimeSession(d):
                    d.fail_restore = True
        self.assertTrue(d.guarded)

    def test_committed_storage_change_retains_guard(self):
        d = Device()
        with self.session(d):
            with self.assertRaisesRegex(RuntimeError, 'Committed storage changed'):
                with cpu.RuntimeSession(d):
                    d.stored[0][2] ^= 1
        self.assertTrue(d.guarded)

    def test_capture_failure_releases_unmodified_session(self):
        d = Device(); d.corrupt_read = True
        with self.session(d):
            with self.assertRaisesRegex(RuntimeError, 'CRC failed'):
                with cpu.RuntimeSession(d):
                    self.fail('must not enter workload')
        self.assertFalse(d.guarded)

    def test_existing_guard_is_not_taken_over(self):
        d = Device(); d.guarded = True
        with self.assertRaisesRegex(RuntimeError, 'Existing guarded session'):
            cpu.RuntimeSession(d).__enter__()
        self.assertTrue(d.guarded)

    def test_oversize_snapshot_and_persistent_write_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'bounded snapshot'):
            cpu.read_object(Device(), (0, cpu.MAX_RUNTIME_BYTES+1, 0))
        with self.assertRaises(ValueError):
            cpu.write_ram(Device(), 2, b'project')

    def test_lost_guard_never_sends_preferences_to_persistence(self):
        with self.assertRaisesRegex(RuntimeError, 'Persistence guard lost'):
            cpu.write_ram(Device(), 1, bytes(cpu.PREFERENCE_BYTES))

    def test_histogram_upper_edges_and_overflow(self):
        bins = [0]*64; bins[1] = 98; bins[63] = 2
        class Histogram:
            def request(self, *args):
                return [0, 2, 64, *cpu.u32(1536), *cpu.u32(120000),
                        *(x for n in bins for x in cpu.u32(n))]
        h = cpu.histogram(Histogram(), 240)
        self.assertEqual(h['samples'], 100)
        self.assertEqual(h['complete_percentile_upper_us']['95'], (3072+336)/24)
        self.assertEqual(h['complete_percentile_upper_us']['99'], (120000+336)/24)
        self.assertEqual(h['overflow_samples'], 2)

    def test_old_and_extended_span_decoding(self):
        for count in (len(legacy.SPANS), len(legacy.SPANS+legacy.PANEL_SPANS), len(legacy.SPANS+legacy.PANEL_SPANS+legacy.BATCH_SPANS)):
            class Profile:
                def request(self, *args):
                    return [0, 1, count]+[x for v in range(count*3+5) for x in cpu.u32(v)]
            result = legacy.profile(Profile())
            self.assertEqual(result['half'], dict(ticks=0, calls=1, max_ticks=2))
            self.assertEqual('input_shift' in result, count > len(legacy.SPANS))
            self.assertEqual('batch_prepare' in result, count > len(legacy.SPANS+legacy.PANEL_SPANS))

    def test_runtime_chunk_read_retries_only_idempotent_requests(self):
        d = legacy.ReliableDevice.__new__(legacy.ReliableDevice)
        d.diagnostic_retries = 0
        with patch.object(legacy.Device, 'request', side_effect=[TimeoutError('read'), [0, 0]]), \
                patch.object(legacy.time, 'sleep'), patch('builtins.print'):
            self.assertEqual(d.request(66, [0]), [0, 0])
        self.assertEqual(d.diagnostic_retries, 1)
        with patch.object(legacy.Device, 'request', side_effect=TimeoutError('write')) as request:
            with self.assertRaises(TimeoutError): d.request(67, [0])
            self.assertEqual(request.call_count, 1)

    def row(self):
        return dict(passed=True, peak_complete_upper_us=1800,
            histogram=dict(samples=20, complete_max_upper_us=1900),
            usb_play_alt=0, usb_capture_alt=0, normal_budget_fades=0,
            delta=dict.fromkeys(('capacity_steals', 'missed_frames', 'play_underruns',
                'play_overruns', 'cap_underruns', 'cap_overruns', 'bad_packets'), 0) | dict(rx_packets=100, tx_packets=100))

    def test_histogram_max_controls_acceptance_and_headroom(self):
        r = self.row()
        r['histogram']['complete_max_upper_us'] = cpu.DEADLINE_US+1
        cpu.assess_row(r, 'inactive')
        self.assertFalse(r['passed'])
        self.assertFalse(r['deadline_passed'])
        self.assertFalse(r['target_passed'])
        self.assertLess(r['headroom_pct'], 0)
        self.assertEqual(r['stats_peak_complete_upper_us'], 1800)

    def test_screen_keeps_capacity_usb_and_histogram_requirements(self):
        r = self.row(); cpu.assess_row(r, 'inactive')
        self.assertTrue(r['passed'])
        for field in (key for key in r['delta'] if key not in ('rx_packets', 'tx_packets')):
            r = self.row(); r['delta'][field] = 1
            cpu.assess_row(r, 'inactive'); self.assertFalse(r['passed'], field)
        r = self.row(); r['histogram']['samples'] = 0
        cpu.assess_row(r, 'inactive'); self.assertFalse(r['passed'])
        r = self.row(); cpu.assess_row(r, 'stereo'); self.assertFalse(r['passed'])
        r = self.row(); r['usb_play_alt'] = r['usb_capture_alt'] = 1
        cpu.assess_row(r, 'stereo'); self.assertTrue(r['passed'])
        r = self.row(); r['usb_play_alt'] = r['usb_capture_alt'] = 1
        r['host_audio_statuses'] = ['output underflow']
        cpu.assess_row(r, 'stereo'); self.assertFalse(r['passed'])

    def test_engine_modulation_and_source_churn_fixture(self):
        class Fixture:
            cpu_modulation_routing = 'engine'
            cpu_event_bursts = False
            cpu_source_churn = True
            def __init__(self): self.values = [{} for _ in range(4)]
            def params(self, track, values): self.values[track].update(values)
        d = Fixture(); notes = [(0, 60)]
        self.assertIs(cpu.configure_modulation(d, [0, 1, 2, 3], 0, notes), notes)
        for values in d.values:
            self.assertEqual([values[f'P_M{k+1}DST'] for k in range(4)], [12, 13, 14, 15])
        seen = set()
        for tick in range(10):
            cpu.control_updates(d, [0, 1, 2, 3], notes, tick)
            for values in d.values:
                seen.update(values[f'P_M{k+1}SRC'] for k in range(4))
        self.assertEqual(seen, set(range(1, 11)))

    def test_profile_ranks_overlapping_groups_without_summing(self):
        profile = dict(half=dict(ticks=4800, calls=2, max_ticks=2600),
            prophet=dict(ticks=2400, calls=4, max_ticks=800),
            input_shift=dict(ticks=1200, calls=100, max_ticks=20), worker_ticks=600)
        r = cpu.ranked_profile(dict(profile=profile))
        self.assertEqual([s['span'] for s in r['audio_inclusive']], ['half', 'prophet'])
        self.assertEqual([s['span'] for s in r['panel_all_ticks']], ['input_shift'])
        self.assertEqual(r['audio_inclusive'][1]['us_per_half'], 50)
        self.assertEqual(r['panel_all_ticks'][0]['mean_us'], .5)

    def test_single_circuit_fixture_retains_melodic_notes_and_count(self):
        d = SimpleNamespace(performance_modulation=False, cpu_drum_note=49)
        notes = [(0, 60), (1, 36), (3, 38)]
        self.assertEqual(cpu.configure_workload(d, [0, 5, 2, 4], 0, notes),
                         [(0, 60), (1, 49), (3, 49)])

    def test_setup_refuses_track_not_in_use_before_loading_presets(self):
        # Firmware ignores selection of a track beyond song.ntrk; the preset
        # would otherwise load into the still-selected track.
        sent = []
        def request(cmd, args=()):
            sent.append((cmd, tuple(args)))
            if cmd == 27:
                return [0, 4]+[0]*28
            raise AssertionError(f'unexpected command {cmd}')
        d = SimpleNamespace(request=request, silence=lambda: None, params=lambda *args: None,
                            performance_categories=[3, None, None, None])
        with self.assertRaisesRegex(RuntimeError, 'Track 2 is not in use'):
            legacy.setup(d, [3, 0, 1, 2], [1]*4, 0)
        self.assertEqual(sent, [(27, (1,))])


if __name__ == '__main__':
    unittest.main()
