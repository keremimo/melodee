#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Host-only lossless recording codec experiment. Never reads/writes project slots.

Input: explicit 10-byte canonical note dumps from recording_bench.c. Codec:
32 bank counts; onset deltas; seven-bit pitch with an optional-attributes flag;
changed velocity/duration; nondefault overview rounding and drum articulation.
The firmware's existing 8-byte/event format and fixed allocation are unchanged.
"""
import argparse
import csv
import random
import struct
import sys
from pathlib import Path

NOTE = struct.Struct('<HHBBBBbB')
MAGIC = b'RCP1'


def uleb(n):
    assert 0 <= n < (1 << 25)
    out = bytearray()
    while n >= 128:
        out.append((n & 127) | 128)
        n >>= 7
    out.append(n)
    return out


def ordered(notes):
    # Match the existing saver: group banks, preserve their event order.
    # Equal-time ordering can affect velocity, chance and voice stealing.
    return sorted(notes, key=lambda r: r[4] & 31)


def valid(r):
    on, duration, note, vel, owner, step, pitch, length = r
    return (0 <= on <= 65535 and 1 <= duration <= 65535 and 0 <= note <= 127
            and 1 <= vel <= 127 and 0 <= owner <= 255 and 0 <= step <= 255
            and -24 <= pitch <= 24 and length in (0, 1)
            and ((step & 128) or (step & 63) + bool(step & 64) < 64))


def encode(notes):
    if len(notes) > 1024 or not all(valid(r) for r in notes):
        raise ValueError('invalid canonical notes')
    notes = ordered(notes)
    out = bytearray(MAGIC)
    for bank in range(32):
        rows = [r for r in notes if (r[4] & 31) == bank]
        out += uleb(len(rows))
        last_on, last_vel, last_duration = 0, 100, 65535 << 3
        for on, duration, note, vel, owner, step, pitch, length in rows:
            start = ((step & 63) << 16) | on
            dur = (duration << 3) | (owner >> 5)
            flags = ((vel != last_vel) | ((dur != last_duration) << 1)
                     | (bool(step & 192) << 2) | (bool(pitch or length) << 3))
            delta = start - last_on
            out += uleb(delta * 2 if delta >= 0 else -delta * 2 - 1)
            out.append(note | (128 if flags else 0))
            if flags:
                out.append(flags)
            if flags & 1:
                out.append(vel)
            if flags & 2:
                out += uleb(dur)
            if flags & 4:
                out.append(step & 192)
            if flags & 8:
                out.append(((pitch + 24) << 1) | length)
            last_on, last_vel, last_duration = start, vel, dur
    return bytes(out)


def decode(blob):
    if not blob.startswith(MAGIC):
        raise ValueError('bad magic')
    at = len(MAGIC)

    def byte():
        nonlocal at
        if at >= len(blob):
            raise ValueError('truncated codec')
        v = blob[at]
        at += 1
        return v

    def integer():
        value = 0
        for shift in range(0, 28, 7):
            b = byte()
            value |= (b & 127) << shift
            if not (b & 128):
                return value
        raise ValueError('oversized integer')

    notes = []
    for bank in range(32):
        count = integer()
        if count > 1024 - len(notes):
            raise ValueError('too many notes')
        start, vel, duration = 0, 100, 65535 << 3
        for _ in range(count):
            delta = integer()
            start += -(delta // 2) - 1 if delta & 1 else delta // 2
            note = byte()
            flags = byte() if note & 128 else 0
            if flags > 15:
                raise ValueError('unknown attributes')
            if flags & 1:
                vel = byte()
            if flags & 2:
                duration = integer()
            rounding = byte() if flags & 4 else 0
            drum = byte() if flags & 8 else 48
            r = (start & 65535, duration >> 3, note & 127, vel,
                 bank | ((duration & 7) << 5), (start >> 16) | rounding,
                 (drum >> 1) - 24, drum & 1)
            if not 0 <= start < (64 << 16) or rounding & 63 or not valid(r):
                raise ValueError('invalid decoded note')
            notes.append(r)
    if at != len(blob):
        raise ValueError('trailing data')
    return notes


def self_test():
    rng = random.Random(0xF1027)
    fixtures = [[], [(0, 1, 0, 1, 0, 0, -24, 0)],
                [(65535, 65535, 127, 127, 255, 255, 24, 1)]]
    for count in (1, 64, 256, 1024):
        fixtures.append([(rng.randrange(65536), rng.randrange(1, 65536),
                          rng.randrange(128), rng.randrange(1, 128), rng.randrange(256),
                          rng.randrange(64), rng.randrange(-24, 25), rng.randrange(2))
                         for _ in range(count)])
    for rows in fixtures:
        blob = encode(rows)
        assert decode(blob) == ordered(rows)
        # Truncation at every boundary, including empty input and bank counts.
        for cut in range(len(blob)):
            try:
                decode(blob[:cut])
            except ValueError:
                pass
            else:
                raise AssertionError(f'accepted truncated input {cut}')
    for blob in (b'bad!', encode([]) + b'\0', MAGIC + b'\x80' * 8,
                 MAGIC + uleb(1025)):
        try:
            decode(blob)
        except ValueError:
            pass
        else:
            raise AssertionError('accepted malformed input')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', type=Path)
    args = parser.parse_args()
    self_test()
    if args.directory is None:
        print('Recording storage probe: lossless roundtrip and malformed-input checks passed')
        return
    writer = csv.writer(sys.stdout)
    writer.writerow(('fixture', 'events', 'occupied_current_bytes', 'reserved_current_bytes',
                     'prototype_bytes', 'vs_occupied_percent', 'vs_reserved_percent'))
    for path in sorted(args.directory.glob('*.bin')):
        data = path.read_bytes()
        if len(data) % NOTE.size:
            raise ValueError(f'incomplete note in {path}')
        rows = list(NOTE.iter_unpack(data))
        blob = encode(rows)
        assert decode(blob) == ordered(rows), path
        occupied, reserved = len(rows) * 8 + 44, 1024 * 8 + 44
        writer.writerow((path.stem, len(rows), occupied, reserved, len(blob),
                         round(100 * (1 - len(blob) / occupied), 1),
                         round(100 * (1 - len(blob) / reserved), 1)))


if __name__ == '__main__':
    main()
