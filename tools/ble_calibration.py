#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Recover this FM-1's radio trims from its own full-flash backup.

Inspection is read-only. --restore imports only a checked 100-byte calibration
copy through USB editor command 81. Never restores a flash image or VM sector.
The connected firmware must include the calibration-recovery command.
"""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import time

CANDIDATES = [(0xE8000, 0x1000, 0xAC), (0xE7000, 0x1000, 0xAD),
              (0x93000, 0x2000, 0xAA), (0x95000, 0x2000, 0xAB)]
LENGTHS = {106: 2, 107: 7, 108: 20, 187: 68}
COMMAND = 81


def crc16(data):
    return binascii.crc_hqx(data, 0)


def copy_valid(copy):
    return (len(copy) == 100 and copy[0] in {c[2] for c in CANDIDATES} and
            crc16(copy[:98]) == int.from_bytes(copy[98:], 'little') and
            crc16(copy[30:94]) == int.from_bytes(copy[94:96], 'little'))


def scan(image, offset, size, mark):
    area = image[offset:offset + size]
    if area[:4] != b'\x55\xaa\xaa\x55':
        return None
    records, pos = {}, 4
    while pos + 4 <= len(area):
        h = area[pos:pos+4]
        ident, length = h[1] | (h[2] & 15) << 8, h[2] >> 4 | h[3] << 4
        if not length or pos + 4 + length > len(area):
            break
        data = area[pos+4:pos+4+length]
        if crc16(data) & 255 != h[0]:
            break
        if ident in LENGTHS:
            if length == LENGTHS[ident]:
                records[ident] = data
            else:
                records.pop(ident, None)
        pos += 4 + length
    if set(records) != set(LENGTHS):
        return None
    raw = bytes([mark]) + b''.join(records[i] for i in LENGTHS)
    copy = raw + crc16(raw).to_bytes(2, 'little')
    return copy if copy_valid(copy) else None


def extract(image):
    if len(image) != 0x100000:
        raise ValueError('Expected a complete 1 MiB flash backup, not a firmware package or musical archive')
    found = [(offset, copy) for offset, size, mark in CANDIDATES
             if (copy := scan(image, offset, size, mark)) is not None]
    if not found:
        raise ValueError('No complete checksum-valid radio calibration in this backup')
    if any(copy[1:98] != found[0][1][1:98] for _, copy in found):
        raise ValueError('Backup contains conflicting calibration sets; inspect before choosing one')
    return found[0]


def status(device, args=(0,)):
    b = device.request(COMMAND, args)
    if len(b) != 9 or b[1] != 1:
        raise RuntimeError('Firmware does not support calibration recovery protocol 1')
    return dict(rc=b[0], calibrated=bool(b[2]), radio_on=bool(b[3]),
                midi_ready=bool(b[4]), saved=bool(b[5]), crc=b[6] | b[7] << 7 | b[8] << 14)


def restore(device, copy):
    if not copy_valid(copy):
        raise ValueError('Invalid calibration copy')
    before = status(device)
    if before['radio_on']:
        raise RuntimeError('Switch Bluetooth Radio OFF before recovering calibration')
    # Editor transfers use groups of seven low bytes preceded by their MSBs.
    # The firmware updater's continuous bitstream encoding is different.
    payload = [1]
    for i in range(0, len(copy), 7):
        group = copy[i:i+7]
        payload += [sum((x >> 7) << j for j, x in enumerate(group))]
        payload += [x & 127 for x in group]
    after = status(device, payload)
    if after['rc'] not in (0, 4):
        raise RuntimeError(f"Calibration import refused: {after['rc']} (1 invalid, 2 existing different calibration, 3 save failed)")
    if not after['calibrated']:
        raise RuntimeError('Stop playback, release notes and close USB audio streams, then retry')
    end = time.monotonic() + 10
    while not after['saved'] and time.monotonic() < end:
        time.sleep(.2)
        after = status(device)
    if not after['saved'] or after['crc'] != int.from_bytes(copy[98:], 'little'):
        raise RuntimeError('Calibration was not confirmed saved with the expected checksum')
    return before, after


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('backup', type=Path, help="this FM-1's original full-flash backup")
    p.add_argument('--restore', action='store_true', help='import calibration into the connected device settings')
    p.add_argument('--port', help='exact connected FM-1 USB MIDI port; required with --restore')
    p.add_argument('--receipt', type=Path, help='save validation/installation metadata without calibration bytes')
    args = p.parse_args()
    image = args.backup.read_bytes()
    area, copy = extract(image)
    result = dict(backup=str(args.backup.resolve()), backup_sha256=hashlib.sha256(image).hexdigest(),
                  vm_area=hex(area), calibration_bytes=len(copy),
                  calibration_sha256=hashlib.sha256(copy).hexdigest(), restored=False)
    if args.restore:
        if not args.port:
            p.error('--restore requires --port')
        from prophet_device import Device
        device = Device(args.port)
        try:
            before, after = restore(device, copy)
        finally:
            device.close()
        result.update(restored=True, before=before, after=after)
    if args.receipt:
        args.receipt.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
