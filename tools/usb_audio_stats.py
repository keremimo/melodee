#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Read USB audio counters over MIDI without changing the sound or settings."""
import argparse
import json
import time
import mido

FIELDS = (
    'play_alt', 'cap_alt', 'play_rate', 'cap_rate', 'play_fill', 'cap_fill',
    'play_underruns', 'play_overruns', 'cap_underruns', 'cap_overruns',
    'bad_packets', 'rx_packets', 'tx_packets', 'missed_frames',
    'poll_max_us', 'service_max_us', 'audio_late', 'feedback_q14',
    'audio_max_us', 'cpu_q8',
)
VOICE_FIELDS = ('voices_active', 'voices_held', 'core1_rejected', 'core1_faults', 'voices_shed', 'voices_given_up')
CORE_FIELDS = ('core1_online', 'core1_jobs', 'core1_max_job_us', 'core1_max_wait_us', 'core1_timeouts', 'fm6_pairs')
MEMORY_FIELDS = ('cache_status', 'cache_con_before', 'cache_data_before', 'cache_instruction_before',
                 'cache_con_after', 'cache_data_after', 'cache_instruction_after', 'cache_failure_address',
                 'cache_test_us', 'resource_capacity', 'resource_used', 'resource_peak', 'resource_failures',
                 'cache_capacity', 'cache_used')
HEADER = [0x7D, 0x46, 0x4C, 72]


def snapshot(incoming, outgoing, window=False, voices=False, cores=False, memory=False):
    flags = int(window) | (2 if voices else 0) | (4 if cores else 0) | (8 if memory else 0)
    outgoing.send(mido.Message('sysex', data=HEADER + ([flags] if flags else [])))
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        for message in incoming.iter_pending():
            if message.type != 'sysex' or list(message.data[:4]) != HEADER:
                continue
            data = message.data[4:]
            fields = {2: FIELDS, 3: FIELDS + VOICE_FIELDS, 4: FIELDS + VOICE_FIELDS + CORE_FIELDS,
                      5: FIELDS + VOICE_FIELDS + CORE_FIELDS + MEMORY_FIELDS}.get(data[0] if data else 0, ())
            if not fields or len(data) != 1 + 5 * len(fields):
                raise RuntimeError('Unsupported audio diagnostics schema')
            if cores and data[0] < 4:
                raise RuntimeError('Core diagnostics require firmware with AUDIO_STATS schema 4')
            if memory and data[0] != 5:
                raise RuntimeError('Memory diagnostics require firmware with AUDIO_STATS schema 5')
            return {field: sum(data[1 + i * 5 + j] << (7 * j) for j in range(5))
                    for i, field in enumerate(fields)}
        time.sleep(0.005)
    raise TimeoutError('No USB audio diagnostics reply; requires a USB audio build (editor command 72)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='Melodee')
    parser.add_argument('--seconds', type=float, default=0)
    parser.add_argument('--interval', type=float, default=5)
    parser.add_argument('--window', action='store_true',
                        help='report the maxima of each interval instead of since boot')
    parser.add_argument('--voices', action='store_true', help='include active voices and overload shedding counters')
    parser.add_argument('--cores', action='store_true', help='include second-core worker and FM6 pairing counters')
    parser.add_argument('--memory', action='store_true', help='include SRAM capacity and cache-RAM boot self-test results')
    args = parser.parse_args()
    if args.interval < 1 or args.seconds < 0:
        parser.error('interval must be >= 1 s; seconds must be >= 0')
    with mido.open_input(args.port) as incoming, mido.open_output(args.port) as outgoing:
        start = time.monotonic()
        while True:
            stats = snapshot(incoming, outgoing, args.window, args.voices, args.cores, args.memory)
            stats['cpu_pct'] = round(stats['cpu_q8'] * 100 / 256, 1)
            stats['elapsed_s'] = round(time.monotonic() - start, 3)
            print(json.dumps(stats), flush=True)
            remaining = start + args.seconds - time.monotonic()
            if remaining <= 0:
                break
            time.sleep(min(args.interval, remaining))


if __name__ == '__main__':
    main()
