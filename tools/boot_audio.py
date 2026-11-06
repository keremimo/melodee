#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Portions Copyright (C) 2026 Charles Vestal (LZMA, every SRAM load image)
"""Pack the SRAM load images for boot and verify their decoder's XIP closure."""
import lzma
import re
import struct

MAGIC = 0x32414D4C  # LMA2: firmware/src/boot_lzma.h BOOT_LZMA_MAGIC
# Raw LZMA1, as boot_lzma.h decodes it: lc 3, lp 1, pb 1 (instructions are
# halfword aligned), with the end marker. The dictionary is the whole image.
FILTERS = [{'id': lzma.FILTER_LZMA1, 'lc': 3, 'lp': 1, 'pb': 1, 'dict_size': 1 << 20}]
# Encoder settings only, the decoder never sees them: the smallest wins. On this code the
# two-byte match finder beats preset 9e's (bt4, nice 273) by about 0.5 %.
EFFORT = [{'mode': lzma.MODE_NORMAL, 'mf': lzma.MF_BT2, 'nice_len': nice, 'depth': depth}
          for nice, depth in ((40, 0), (64, 1000), (128, 0), (128, 1000))]
# The sections fm1_cstart loads into SRAM, by their load addresses (firmware/app.ld).
SECTIONS = ('ramtext.bin', 'dsptext.bin', 'dsptables.bin', 'data.bin', 'audiotext.bin')
# Their order in the one stream, which shares its dictionary (firmware/src/main.c boot_images):
# .audio_text last, since it unpacks in place.
STREAM = ('dsptables.bin', 'data.bin', 'ramtext.bin', 'dsptext.bin', 'audiotext.bin')
ROOTS = ('boot_lzma', 'boot_take', 'boot_undelta', 'boot_uncall')


def delta2(raw):
    """words as second differences (boot_lzma.h boot_undelta sums them twice)"""
    if len(raw) % 4:
        raise ValueError('a table section is not whole words')
    out, a, b = bytearray(), 0, 0
    for (word,) in struct.iter_unpack('<I', raw):
        first = (word - b) & 0xFFFFFFFF
        out += struct.pack('<I', (first - a) & 0xFFFFFFFF)
        a, b = first, word
    return bytes(out)


def undelta2(raw):
    out, a, b = bytearray(), 0, 0
    for (word,) in struct.iter_unpack('<I', raw):
        a = (a + word) & 0xFFFFFFFF
        b = (b + a) & 0xFFFFFFFF
        out += struct.pack('<I', b)
    return bytes(out)


def _calls(raw, sign):
    code, i = bytearray(raw), 0
    while i + 4 <= len(code):
        if code[i + 1] == 0xEA and 0x80 <= code[i] <= 0xBF:
            d = ((code[i] & 0x3F) << 16 | code[i + 3] << 8 | code[i + 2]) + sign * (i >> 1)
            code[i], code[i + 2], code[i + 3] = 0x80 | (d >> 16) & 0x3F, d & 255, (d >> 8) & 255
            i += 4
        else:
            i += 2
    return bytes(code)


def calls_absolute(raw):
    """every call's target from the section's start, not from the call (boot_lzma.h boot_uncall undoes it)"""
    return _calls(raw, 1)


def calls_relative(raw):
    return _calls(raw, -1)


def pack(raw):
    """the load image: [MAGIC, packed bytes, raw bytes, 0], the stream, padded to a word"""
    data = min((lzma.compress(raw, format=lzma.FORMAT_RAW, filters=[{**FILTERS[0], **effort}]) for effort in EFFORT),
               key=len)
    if lzma.decompress(data, format=lzma.FORMAT_RAW, filters=FILTERS) != raw:
        raise ValueError('boot image compression round trip failed')
    return struct.pack('<4I', MAGIC, len(data), len(raw), 0) + data + b'\0' * (-len(data) % 4)


def stream_of(blobs):
    """the sections (SECTIONS order) as the stream holds them"""
    held = dict(zip(SECTIONS, blobs))
    held['dsptables.bin'] = delta2(held['dsptables.bin'])
    held['audiotext.bin'] = calls_absolute(held['audiotext.bin'])
    return b''.join(held[name] for name in STREAM)


def sections_of(stream, sizes):
    """the stream back into its sections (sizes and the result in SECTIONS order), with the filters undone"""
    held, at, size = {}, 0, dict(zip(SECTIONS, sizes))
    for name in STREAM:
        held[name] = stream[at:at + size[name]]
        at += size[name]
    if at != len(stream):
        raise ValueError('the boot image stream is not its sections')
    held['dsptables.bin'] = undelta2(held['dsptables.bin'])
    held['audiotext.bin'] = calls_relative(held['audiotext.bin'])
    return [held[name] for name in SECTIONS]


def pack_images(blobs):
    """every section (SECTIONS order; empty ones too) in the one load image; each comes back exact or the build stops"""
    if any(len(blob) % 4 for blob in blobs):
        raise ValueError('a boot section is not whole words')
    stream = stream_of(blobs)
    if sections_of(stream, [len(blob) for blob in blobs]) != list(blobs):
        raise ValueError('boot image filter round trip failed')
    return pack(stream)


def unpack_image(image):
    """the packager's record read back (the tests): (raw size, stream)"""
    magic, n, size, reserved = struct.unpack_from('<4I', image, 0)
    if magic != MAGIC or reserved:
        raise ValueError('no load image record')
    return size, image[16:16 + n]


def check_decoder(syms, dis, xip_start, xip_end, roots=ROOTS):
    addresses = {}
    for line in syms.splitlines():
        fields = line.split()
        if len(fields) >= 3 and re.fullmatch('[0-9a-f]+', fields[0]):
            addresses[fields[-1]] = int(fields[0], 16)
    functions = {}
    current = None
    for line in dis.splitlines():
        label = re.fullmatch(r'([\w.]+):', line)
        if label:
            current = label[1]
            functions[current] = []
        elif current:
            functions[current].append(line)
    errors, seen, pending = [], set(), list(roots)
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        if name not in functions or not xip_start <= addresses.get(name, 0) < xip_end:
            errors.append(f'boot decoder dependency {name} must execute from XIP')
            continue
        for line in functions[name]:
            call = re.search(r'\bcall\b', line)
            target = re.search(r'(?:\bcall|\bgoto) -?\d+ <([\w.]+)(?:\+[^ :]+)? :', line)
            if call and not target:
                errors.append(f'boot decoder {name} has an unresolved indirect call: {line.strip()}')
            if target and target[1] != name:
                pending.append(target[1])
    # Constant tables must also be available before executable SRAM is copied.
    for name, address in addresses.items():
        if name.startswith('boot_lzma.') and not xip_start <= address < xip_end:
            errors.append(f'boot decoder table {name} must be in XIP')
    return errors
