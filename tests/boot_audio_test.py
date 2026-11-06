#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Independent LZMA fixtures, malformed decoder inputs, the filters and the real packed image."""
import lzma
import os
from pathlib import Path
import random
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import boot_audio


def elf_symbol(path, wanted):
    """Independent ELF32 reader: verify the load address the startup uses."""
    data = path.read_bytes()
    assert data[:6] == b'\x7fELF\x01\x01'
    offset = struct.unpack_from('<I', data, 32)[0]
    stride, count = struct.unpack_from('<HH', data, 46)
    sections = [struct.unpack_from('<10I', data, offset + i * stride) for i in range(count)]
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
        for pos in range(section[4], section[4] + section[5], section[9]):
            name, value = struct.unpack_from('<II', data, pos)
            if strings[name:strings.find(b'\0', name)].decode() == wanted:
                return value
    raise AssertionError('missing ELF symbol ' + wanted)


def call_like_bytes(rng, n):
    """words that are often calls, near the ends of the 22-bit range too, among others that nearly are"""
    out = bytearray()
    while len(out) < n:
        out += bytes((rng.choice((0x80, 0xBF, 0xA2, 0x7F, 0xC0)), rng.choice((0xEA, 0xEA, 0xEB)),
                      rng.randrange(256), rng.randrange(256)))
    return bytes(out[:n])


class BootAudio(unittest.TestCase):
    def test_record_and_round_trip(self):
        for raw in (b'', b'A', bytes(random.Random(741).randbytes(512))):
            data = boot_audio.pack(raw)
            magic, n, size, reserved = struct.unpack('<4I', data[:16])
            self.assertEqual((magic, size, reserved), (boot_audio.MAGIC, len(raw), 0))
            self.assertEqual(len(data), 16 + n + (-n % 4))
            self.assertEqual(lzma.decompress(data[16:16 + n], format=lzma.FORMAT_RAW, filters=boot_audio.FILTERS), raw)
        sections = [b'abcd', b'', bytes(range(256)) * 4, b'wxyz' * 2, b'\x80\xea\x10\x00' * 8]
        size, stream = boot_audio.unpack_image(boot_audio.pack_images(sections))
        self.assertEqual(size, sum(map(len, sections)))
        raw = lzma.decompress(stream, format=lzma.FORMAT_RAW, filters=boot_audio.FILTERS)
        self.assertEqual(boot_audio.sections_of(raw, [len(s) for s in sections]), sections)
        with self.assertRaisesRegex(ValueError, 'no load image'):
            boot_audio.unpack_image(b'\0' * 16)
        with self.assertRaisesRegex(ValueError, 'not whole words'):
            boot_audio.pack_images([b'abc', b'', b'', b'', b''])

    def test_filters(self):
        rng = random.Random(1200)
        for raw in (b'', b'\xff' * 8, bytes(rng.randbytes(1024)), struct.pack('<4I', 0xFFFFFFFF, 0, 0xFFFFFFFF, 1)):
            self.assertEqual(boot_audio.undelta2(boot_audio.delta2(raw)), raw)
        for raw in (b'', b'\x80\xea', b'\x80\xea\x00\x00', b'\xbf\xea\xff\xff\xbf\xea\xff\xff', call_like_bytes(rng, 4096),
                    call_like_bytes(rng, 4098), bytes(rng.randbytes(4096))):
            self.assertEqual(boot_audio.calls_relative(boot_audio.calls_absolute(raw)), raw)
        # two calls to one target read the same; each call stays a call
        call = lambda at, target: bytes(((target - at - 4) >> 17 & 0x3F | 0x80, 0xEA,
                                         (target - at - 4) >> 1 & 255, (target - at - 4) >> 9 & 255))
        code = call(0, 0x1000) + b'\0' * 12 + call(16, 0x1000)
        made = boot_audio.calls_absolute(code)
        self.assertEqual(made[:4], made[16:20])
        self.assertEqual(boot_audio.calls_relative(made), code)

    def test_dependency_gate(self):
        syms = '02001000 l F .text 00000020 boot_lzma\n02001040 l F .text 00000020 helper\n'
        dis = 'boot_lzma:\n 2001000: 00 00 \tcall 64 <helper : 2001040 >\nhelper:\n 2001040: 00 00 \trts\n'
        roots = ('boot_lzma',)
        self.assertEqual(boot_audio.check_decoder(syms, dis, 0x2001000, 0x208d000, roots), [])
        self.assertTrue(boot_audio.check_decoder(syms.replace('02001040', '01c01040'), dis, 0x2001000, 0x208d000, roots))
        self.assertTrue(boot_audio.check_decoder(syms, dis.replace('call 64 <helper : 2001040 >', 'call r3'), 0x2001000, 0x208d000, roots))
        self.assertTrue(boot_audio.check_decoder(syms + '01c01000 l O .data 00000020 boot_lzma.table\n', dis, 0x2001000, 0x208d000, roots))
        self.assertTrue(boot_audio.check_decoder(syms, dis, 0x2001000, 0x208d000), 'the default roots are all required')

    def test_decoder(self):
        rng = random.Random(9025)
        payloads = [b'', b'A', bytes(range(256)), b'ABCD' * 32768,
                    bytes(rng.randrange(256) for _ in range(8192))]
        with tempfile.TemporaryDirectory(prefix='melodee-lzma-') as temp:
            root = Path(temp)
            exe = root / 'lzma'
            flags = shlex.split(os.environ.get('BOOT_SANITIZE', '-fsanitize=address,undefined'))
            subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-O1', '-g', *flags, '-fno-omit-frame-pointer',
                            'tests/boot_lzma_test.c', '-o', str(exe)], check=True)

            def decode(stream, raw):
                (root / 'packed').write_bytes(stream)
                (root / 'raw').write_bytes(raw)
                subprocess.run([str(exe), str(root / 'packed'), str(root / 'raw'), str(len(raw))], check=True)

            def filtered(kind, stored, raw):
                (root / 'stored').write_bytes(stored)
                (root / 'raw').write_bytes(raw)
                subprocess.run([str(exe), '-' + kind, str(root / 'stored'), str(root / 'raw')], check=True)

            for raw in payloads:
                data = boot_audio.pack(raw)
                decode(data[16:16 + struct.unpack_from('<I', data, 4)[0]], raw)
            # the C filters against the Python ones, on bytes made to look like calls
            for n in (0, 2, 4, 6, 4096, 4098):
                raw = call_like_bytes(rng, n)
                filtered('c', boot_audio.calls_absolute(raw), raw)
            raw = bytes(rng.randbytes(1024))
            filtered('d', boot_audio.delta2(raw), raw)
            # Check bytes actually emitted by the packager, not another compressor invocation.
            sections = [Path('build') / name for name in boot_audio.SECTIONS]
            if all(p.exists() for p in sections) and Path('build/melodee.bin').exists():
                image = Path('build/melodee.bin').read_bytes()
                # ROM entry maps app-file offset zero to 0x02000120.
                offset = elf_symbol(Path('build/melodee.elf'), '_rt_load') - 0x02000120
                self.assertGreaterEqual(offset, 3808)
                if image[offset:offset + 4] != struct.pack('<I', boot_audio.MAGIC):
                    # MELODEE_COMPRESS_AUDIO=0 is the raw-copy reference profile: each at its load address.
                    for name, sym in zip(boot_audio.SECTIONS, ('_rt_load', '_dsp_load', '_dt_load', '_data_load', '_audio_load')):
                        at = elf_symbol(Path('build/melodee.elf'), sym) - 0x02000120
                        raw = (Path('build') / name).read_bytes()
                        self.assertEqual(image[at:at + len(raw)], raw)
                    return
                blobs = [p.read_bytes() for p in sections]
                size, data = boot_audio.unpack_image(image[offset:])
                self.assertLessEqual(offset + 16 + len(data), len(image))
                self.assertEqual(size, sum(map(len, blobs)))
                stream = lzma.decompress(data, format=lzma.FORMAT_RAW, filters=boot_audio.FILTERS)
                # the proof: what the image holds comes back as the linker's sections, byte for byte
                self.assertEqual(boot_audio.sections_of(stream, [len(b) for b in blobs]), blobs)
                decode(data, stream)
                held = dict(zip(boot_audio.SECTIONS, blobs))
                filtered('d', boot_audio.delta2(held['dsptables.bin']), held['dsptables.bin'])
                filtered('c', boot_audio.calls_absolute(held['audiotext.bin']), held['audiotext.bin'])


if __name__ == '__main__':
    unittest.main()
