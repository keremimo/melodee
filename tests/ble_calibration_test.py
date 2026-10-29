#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import ble_calibration as cal


def record(ident, data):
    n = len(data)
    return bytes([cal.crc16(data) & 255, ident & 255, (ident >> 8) | (n & 15) << 4, n >> 4]) + data


def image_at(offset=0xE8000):
    im = bytearray(b'\xff' * 0x100000)
    records = {i: bytes(range(n)) for i, n in cal.LENGTHS.items()}
    records[187] = records[187][:64] + cal.crc16(records[187][:64]).to_bytes(2, 'little') + b'\0\0'
    vm = b'\x55\xaa\xaa\x55' + b''.join(record(i, d) for i, d in records.items())
    im[offset:offset+len(vm)] = vm
    return im, records


class Calibration(unittest.TestCase):
    def test_extract_and_checks(self):
        image, rec = image_at()
        area, copy = cal.extract(image)
        self.assertEqual(area, 0xE8000)
        self.assertEqual(copy[1:98], b''.join(rec.values()))
        self.assertTrue(cal.copy_valid(copy))
        self.assertFalse(cal.copy_valid(copy[:-1]))
        bad = bytearray(copy); bad[94] ^= 1
        bad[98:] = cal.crc16(bad[:98]).to_bytes(2, 'little')
        self.assertFalse(cal.copy_valid(bad))

    def test_invalid_backups(self):
        for image in [bytes(1024), bytes(0x100000), b'\xff' * 0x100000]:
            with self.assertRaises(ValueError): cal.extract(image)
        image, _ = image_at(); image[0xE8008] ^= 1
        with self.assertRaises(ValueError): cal.extract(image)

    def test_partial_and_conflicting_sets(self):
        image, rec = image_at()
        start = 0xE8004 + sum(4+len(rec[i]) for i in [106,107,108])
        image[start] ^= 1
        with self.assertRaises(ValueError): cal.extract(image)
        image, rec = image_at()
        rec[106] = b'\x40\x41'
        vm = b'\x55\xaa\xaa\x55' + b''.join(record(i,d) for i,d in rec.items())
        image[0xE7000:0xE7000+len(vm)] = vm
        with self.assertRaisesRegex(ValueError, 'conflicting'): cal.extract(image)

    def test_restore_protocol(self):
        _, copy = cal.extract(image_at()[0])
        crc = int.from_bytes(copy[98:], 'little')
        class Device:
            def __init__(self): self.calls=[]
            def request(self, cmd, args):
                self.calls.append((cmd,args))
                if len(args)==1: return [0,1,0,0,0,0,0,0,0]
                wire=args[1:]
                decoded=bytes(x | ((wire[i] >> j & 1) << 7)
                              for i in range(0,len(wire),8)
                              for j,x in enumerate(wire[i+1:i+8]))
                assert decoded == copy
                return [0,1,1,0,0,1,crc&127,(crc>>7)&127,crc>>14]
        device = Device()
        _, after = cal.restore(device, copy)
        self.assertTrue(after['saved'])
        self.assertEqual(len(device.calls),2)
        self.assertEqual(len(device.calls[1][1]),116)
        self.assertTrue(all(x<128 for x in device.calls[1][1]))


if __name__ == '__main__': unittest.main()
