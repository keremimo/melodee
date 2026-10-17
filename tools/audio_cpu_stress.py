#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Observe an ongoing shared-budget performance on normal firmware.

No music, transport, preferences or flash writes; no backup/snapshot is needed.
Only diagnostic counters are reset. Start the desired song/clock/controllers
before running. Profiling/lab support must be compiled out, so ten-minute runs
cannot overflow profiling totals. This measures the supplied performance; it
neither constructs workloads nor establishes physical/listening acceptance.
"""
import argparse
import contextlib
import hashlib
import json
import math
import sys
import time
from pathlib import Path

from audio_performance_device import ReliableDevice, metadata, stats, COUNTERS, DEADLINE_US

from audio_cpu_usb import capture_driver_stderr, usb_streams, capture_result

FAULTS = tuple(k for k in COUNTERS if k not in ('rx_packets', 'tx_packets'))
STABLE_FIELDS = ('clock_mhz', 'half_frames', 'profiling', 'benchmark_support', 'admission', 'budget_points')


def normal_metadata(m):
    return (m.get('profiling') == 0 and m.get('benchmark_support') == 0 and
            m.get('clock_mhz') == 360 and m.get('half_frames') == 128 and
            'budget_points' in m and m.get('timer5_max_ticks', 2400) < 2400)


def observe(d, seconds, interval, usb, minimum_voices=0, on_sample=None):
    if not math.isfinite(seconds) or seconds <= 0 or not math.isfinite(interval) or not 0 < interval <= 5:
        raise ValueError('seconds positive and finite; poll interval >0 and <=5 seconds')
    if usb not in ('inactive', 'stereo', 'stems') or not 0 <= minimum_voices <= 64:
        raise ValueError('USB inactive/stereo/stems; minimum voices 0..64')
    initial = metadata(d)
    if not normal_metadata(initial):
        raise RuntimeError(f'Requires normal shared-budget 360-MHz/128-frame firmware: {initial}')
    previous = stats(d, True)
    totals = dict.fromkeys((*COUNTERS, 'bench_shed_requests'), 0)
    retries = d.diagnostic_retries
    start = time.monotonic()
    samples, peak, minimum, maximum = 0, 0, 64, 0
    all_ok, stable = True, True
    while not samples or time.monotonic()-start < seconds:
        time.sleep(min(interval, max(0, seconds-(time.monotonic()-start))))
        current = stats(d)
        m = metadata(d)
        delta = {key: (current[key]-previous[key]) & 0xffffffff for key in totals}
        for key, value in delta.items():
            totals[key] += value
        previous = current
        same = all(initial.get(key) == m.get(key) for key in STABLE_FIELDS)
        stable = stable and same
        usb_ok = (current['play_alt'] > 0 and current['cap_alt'] > 0 if usb != 'inactive'
                  else current['play_alt'] == current['cap_alt'] == 0)
        if usb != 'inactive':
            usb_ok = usb_ok and delta['rx_packets'] > 0 and delta['tx_packets'] > 0
        bound = current['audio_full_max_us']
        active = current['voices_active']
        passed = bool(normal_metadata(m) and same and usb_ok and current['core1_online'] and
                      not current['fixed_voice_bench'] and active >= minimum_voices and
                      bound <= .8*DEADLINE_US and not any(delta[key] for key in FAULTS + ('bench_shed_requests',)))
        samples += 1
        peak = max(peak, bound)
        minimum = min(minimum, active)
        maximum = max(maximum, active)
        all_ok = all_ok and passed
        row = dict(sample=samples, elapsed_seconds=time.monotonic()-start, passed=passed,
                   peak_complete_upper_us=bound, voices_active=active, delta=delta,
                   core1_online=bool(current['core1_online']), usb_play_alt=current['play_alt'],
                   usb_capture_alt=current['cap_alt'], metadata=m,
                   diagnostic_retries=d.diagnostic_retries-retries)
        if on_sample:
            on_sample(row)
    return dict(seconds=time.monotonic()-start, requested_seconds=seconds, samples=samples,
                passed=bool(all_ok and maximum > 0), observed_active_voices=maximum > 0,
                peak_complete_upper_us=peak, deadline_us=DEADLINE_US,
                headroom_pct=100*(1-peak/DEADLINE_US), voices_min=minimum, voices_max=maximum,
                minimum_requested_voices=minimum_voices, delta=totals, metadata_stable=stable,
                metadata_start=initial, metadata_end=m, diagnostic_retries=d.diagnostic_retries-retries,
                evidence='Observed supplied performance only; physical/listening and workload coverage require separate evidence.')


def finalize_host_audio(result, statuses, stderr_bytes):
    result['host_audio_statuses'] = list(statuses or [])
    result['host_audio_stderr_bytes'] = stderr_bytes
    # Unstructured native-driver text needs review even if callbacks report no
    # status. Keep it as evidence and never certify a clean streaming run.
    result['passed'] = bool(result['passed'] and not statuses and not stderr_bytes)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', default='Melodee')
    p.add_argument('--build-manifest', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--seconds', type=float, default=600)
    p.add_argument('--interval', type=float, default=.5)
    p.add_argument('--minimum-voices', type=int, default=0, help='Optional floor for a held-voice workload; sequenced music can legitimately reach zero')
    p.add_argument('--usb', choices=('inactive', 'stereo', 'stems'), default='inactive')
    p.add_argument('--label', default='ongoing user-supplied performance')
    a = p.parse_args()
    if not math.isfinite(a.seconds) or a.seconds <= 0 or not math.isfinite(a.interval) or not 0 < a.interval <= 5 or not 0 <= a.minimum_voices <= 64:
        p.error('seconds positive/finite; interval >0 and <=5; minimum voices 0..64')
    manifest = json.loads(a.build_manifest.read_text())
    if any(manifest['flags'][flag] != 0 for flag in ('MELODEE_AUDIO_PROFILE', 'MELODEE_AUDIO_BENCH')):
        p.error('Use a normal build manifest with profiling and lab support off')
    if hashlib.sha256(Path(manifest['package']).read_bytes()).hexdigest() != manifest['package_sha256']:
        p.error('Package no longer matches manifest')
    if a.output.exists():
        p.error('Output exists; preserve earlier evidence')
    a.output.mkdir(parents=True)
    d = ReliableDevice(a.port)
    result = dict(passed=False, error='Observation did not complete')
    statuses = []
    try:
        d.performance_metadata = d.performance_production = True
        identity = bytes(d.request(1)).split(b'\0', 1)[0].decode('ascii')
        m = metadata(d)
        if identity != manifest['editor_identity'] or not normal_metadata(m) or m['admission'] != manifest['flags']['MELODEE_ADMISSION']:
            raise RuntimeError(f'Installed build differs from normal manifest: {identity}, {m}')
        (a.output/'run.json').write_text(json.dumps(dict(build=manifest, identity=identity,
            metadata=m, workload=vars(a)), indent=2, default=str)+'\n')
        stream = usb_streams(a.output/'capture.wav', 'int24', a.usb) if a.usb != 'inactive' else contextlib.nullcontext([])
        with (a.output/'samples.jsonl').open('w') as samples, capture_driver_stderr(a.output/'driver-stderr.log'):
            def record(row):
                samples.write(json.dumps(row)+'\n')
                samples.flush()
                if row['sample'] == 1 or row['sample'] % max(1, round(30/a.interval)) == 0 or not row['passed']:
                    print(json.dumps({key: row[key] for key in ('sample', 'elapsed_seconds', 'passed', 'peak_complete_upper_us', 'voices_active')}), flush=True)
            with stream as statuses:
                result = observe(d, a.seconds, a.interval, a.usb, a.minimum_voices, record)
    except BaseException as error:
        result = dict(passed=False, error=f'{type(error).__name__}: {error}')
        raise
    finally:
        # The performance continues: no silence, transport or restore commands.
        try:
            d.close()
        except Exception as error:
            result.update(passed=False, close_error=f'{type(error).__name__}: {error}')
        stderr_path = a.output/'driver-stderr.log'
        finalize_host_audio(result, statuses, stderr_path.stat().st_size if stderr_path.exists() else 0)
        result['host_capture'] = capture_result(a.output/'capture.wav', a.usb)
        result['passed'] = bool(result['passed'] and result['host_capture']['passed'])
        (a.output/'summary.json').write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps(result), flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
