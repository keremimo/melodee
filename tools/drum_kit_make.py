#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""The recorded kits (firmware/src/drum_real.c): CC0 multi-mic stems (assets/drums/SOURCES.txt) -> one finished mono hit
per drum, mixed the way a session would be (mics aligned and blended, EQ, compression, tape-like saturation, a fade)
-> a stream at each hit's byte budget (tools/drum_codec.c) -> assets/drums/kits.bin + kits.json, which the build reads
(tools/gen_drum_kits.py). Not part of the build: it needs numpy, scipy, soundfile, soxr and the sources.

  tools/drum_kit_make.py SOURCES_DIR [--wav DIR]     SOURCES_DIR/<repo>/Samples/... as in SOURCES.txt
"""
import binascii, json, os, subprocess, sys, tempfile
from pathlib import Path
import numpy as np, scipy.signal as sg, soundfile as sf, soxr

ROOT = Path(__file__).resolve().parent.parent
R = 44100
SRC = Path(sys.argv[1])
BR = SRC / "karoryfer.big-rusty-drums/repo/Samples"
FN = SRC / "karoryfer.frankensnare/repo/Samples"


def load(p):
    x, sr = sf.read(str(p), always_2d=True)
    assert sr == R
    return x.mean(axis=1)


def onset(x, th=0.04):
    a = np.abs(x)
    return int(np.argmax(a > a.max() * th))


def trim(x, pre=0.0006):
    i = max(0, onset(x) - int(pre * R))
    y = x[i:].copy()
    n = int(0.0004 * R)
    y[:n] *= np.linspace(0, 1, n)
    return y


def align(ref, x, span=0.004):
    n = min(len(ref), len(x), int(0.05 * R))
    m = int(span * R)
    c = sg.correlate(ref[:n], x[:n], "full")
    mid = n - 1
    k = int(np.argmax(np.abs(c[mid - m:mid + m + 1]))) - m
    return (1.0 if c[mid + k] >= 0 else -1.0) * np.roll(x, k)


def mix(*stems):
    """(path, gain) .. : each aligned to the first, polarity included"""
    ref = load(stems[0][0])
    out = ref * stems[0][1]
    for p, g in stems[1:]:
        x = load(p)
        x = x[:len(ref)] if len(x) >= len(ref) else np.pad(x, (0, len(ref) - len(x)))
        out = out + align(ref, x) * g
    return out


def biq(x, k, f, q=0.707, db=0.0):
    A = 10 ** (db / 40)
    w = 2 * np.pi * f / R
    c, s = np.cos(w), np.sin(w)
    al = s / (2 * q)
    if k == "hp":
        b, a = [(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + al, -2 * c, 1 - al]
    elif k == "lp":
        b, a = [(1 - c) / 2, 1 - c, (1 - c) / 2], [1 + al, -2 * c, 1 - al]
    elif k == "pk":
        b, a = [1 + al * A, -2 * c, 1 - al * A], [1 + al / A, -2 * c, 1 - al / A]
    else:
        sa = 2 * np.sqrt(A) * al
        if k == "ls":
            b = [A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa)]
            a = [(A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa]
        else:
            b = [A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa)]
            a = [(A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa]
    return sg.lfilter(b, a, x)


def eq(x, *bands):
    for b in bands:
        x = biq(x, *b)
    return x


def comp(x, th_db, ratio, atk, rel, makeup=0.0):
    """feed-forward: atk lets the hit's front through, makeup brings the body up"""
    th = 10 ** (th_db / 20)
    ga, gr = np.exp(-1 / (atk * R)), np.exp(-1 / (rel * R))
    e = 0.0
    g = np.zeros(len(x))
    for i, v in enumerate(np.abs(x)):
        e = ga * e + (1 - ga) * v if v > e else gr * e + (1 - gr) * v
        g[i] = (e / th) ** (1 / ratio - 1) if e > th else 1.0
    return x * g * 10 ** (makeup / 20)


def sat(x, drive_db):
    """tanh at 4x, small signals untouched"""
    d = 10 ** (drive_db / 20)
    return sg.resample_poly(np.tanh(sg.resample_poly(x, 4, 1) * d) / d, 1, 4)[:len(x)]


def sub(x, tau, db):
    """a sine under the kick, falling from a third above: the body a tight damped kick lacks"""
    f0 = 57.0                                    # a fifth over this kick's 38 Hz: speakers that small can play it
    t = np.arange(int(0.5 * R)) / R
    hz = f0 * (1 + 0.33 * np.exp(-t / 0.018))
    y = np.sin(2 * np.pi * np.cumsum(hz) / R) * np.exp(-t / tau)
    y[:int(0.002 * R)] *= np.linspace(0, 1, int(0.002 * R))
    y = align(x[:len(y)], y, 0.008) if len(x) >= len(y) else y
    return x + np.pad(y, (0, max(0, len(x) - len(y))))[:len(x)] * 10 ** (db / 20) * np.abs(x).max()


def fit(x, keep, fade):
    n, m = int(keep * R), int(fade * R)
    y = x[:n + m].copy()
    if len(y) < n + m:
        y = np.pad(y, (0, n + m - len(y)))
    y[n:] *= np.cos(np.linspace(0, np.pi / 2, m)) ** 2
    return y


def peak(x, db=-1.0):
    return x / np.abs(x).max() * 10 ** (db / 20)


def hits():
    V = {}
    # the big damped 24" kick: its mic and the overheads, the box scooped, the beater forward
    k = BR / "kick_24/kick/%s/k_vl14_rr1.flac"
    x = mix((str(k) % "kick", 1.0), (str(k) % "oh", 0.45))
    x = eq(trim(x), ("hp", 32, 0.8), ("pk", 58, 0.9, 1.5), ("pk", 340, 1.0, -3.0), ("pk", 3200, 0.8, 5.0), ("hs", 9000, 0.7, 3.0))
    x = sub(peak(comp(x, -18, 4.0, 0.007, 0.10, 6.0), 0), 0.075, -8.0)
    V["kick_a"] = peak(fit(sat(peak(x, 0), 4.0), 0.20, 0.09))
    # the 14" snare, top, bottom and overheads: crack, wires, room
    s = BR / "snare_14/center/%s/sn_center_vl10_rr1.flac"
    x = mix((str(s) % "top", 1.0), (str(s) % "btm", 0.40), (str(s) % "oh", 0.75))
    x = eq(trim(x), ("hp", 85, 0.8), ("pk", 205, 1.0, 3.5), ("pk", 620, 1.0, -1.5), ("pk", 3400, 0.8, 3.5), ("hs", 8500, 0.7, 4.0))
    V["snare_a"] = peak(fit(sat(peak(comp(x, -24, 4.0, 0.004, 0.09, 9.0), 0), 6.0), 0.20, 0.10))
    # the undamped kick, driven and rolled off: a sampler's boom
    k = BR / "kick_24_nodamp/kick/%s/k_nodamp_vl6_rr1.flac"
    x = mix((str(k) % "kick", 1.0), (str(k) % "oh", 0.25))
    x = eq(trim(x), ("hp", 30, 0.8), ("pk", 62, 0.8, 4.0), ("pk", 420, 0.9, -6.0), ("pk", 2600, 0.9, 3.5))
    x = biq(sat(peak(comp(x, -24, 4.5, 0.008, 0.14, 9.0), 0), 6.5), "lp", 10500, 0.7)
    V["kick_b"] = peak(fit(x, 0.24, 0.12))
    # the deep 13" birch snare, taped
    s = FN / "13b_basic/%s/sn_vl6_rr1.flac"
    x = mix((str(s) % "top", 1.0), (str(s) % "oh", 0.70))
    x = eq(trim(x), ("hp", 90, 0.8), ("pk", 160, 1.0, 2.0), ("pk", 560, 0.9, -3.0), ("pk", 2400, 0.9, 4.0), ("hs", 6000, 0.7, 3.0))
    x = biq(sat(peak(comp(x, -26, 4.5, 0.003, 0.08, 10.0), 0), 8.0), "lp", 10500, 0.7)
    V["snare_b"] = peak(fit(x, 0.16, 0.09))
    # the 12" steel snare, short and bright
    s = FN / "12_basic/%s/sn_vl7_rr1.flac"
    x = mix((str(s) % "top", 1.0), (str(s) % "oh", 0.50))
    x = eq(trim(x), ("hp", 140, 0.8), ("pk", 240, 1.0, 2.0), ("pk", 800, 1.0, -2.5), ("pk", 3800, 0.8, 3.0), ("hs", 9000, 0.7, 3.0))
    V["snare_c"] = peak(fit(sat(peak(comp(x, -24, 4.0, 0.002, 0.06, 8.0), 0), 6.0), 0.09, 0.07))
    # hats: two takes of the tip, a quarter-open
    for name, rr in (("hatc", 1), ("hatc2", 2)):
        h = BR / ("hihat_14/tc/%%s/ht_tc_vl8_rr%d.flac" % rr)
        x = mix((str(h) % "cl", 1.0), (str(h) % "oh", 0.5))
        x = eq(trim(x), ("hp", 420, 0.7), ("pk", 900, 1.0, -3.0), ("hs", 7500, 0.7, 4.0))
        V[name] = peak(fit(sat(peak(comp(x, -22, 3.0, 0.002, 0.04, 5.0), 0), 3.0), 0.045, 0.035), -1.0)
    h = BR / "hihat_14/qo/%s/ht_qo_vl4_rr1.flac"
    x = mix((str(h) % "cl", 1.0), (str(h) % "oh", 0.6))
    V["hato"] = peak(fit(eq(trim(x), ("hp", 420, 0.7), ("pk", 900, 1.0, -3.0), ("hs", 7500, 0.7, 4.0)), 0.42, 0.16), -1.5)
    t = BR / "tom_18/center/%s/t18_vl8_rr1.flac"
    x = mix((str(t) % "cl", 1.0), (str(t) % "oh", 0.55))
    x = eq(trim(x), ("hp", 55, 0.8), ("pk", 95, 0.9, 1.0), ("pk", 400, 1.0, -2.5), ("pk", 3600, 0.8, 4.0))
    V["tom"] = peak(fit(sat(peak(comp(x, -22, 3.5, 0.006, 0.12, 7.0), 0), 4.0), 0.26, 0.14), -1.5)
    s = BR / "snare_14/sidestick/%s/sn_ss_vl4_rr1.flac"
    x = mix((str(s) % "top", 1.0), (str(s) % "oh", 0.7))
    V["rim"] = peak(fit(eq(trim(x), ("hp", 180, 0.7), ("pk", 1400, 1.0, 2.0)), 0.05, 0.04), -2.0)
    x = eq(trim(load(FN / "xtra_claps/clap_stack_rr4.flac")), ("hp", 260, 0.7), ("pk", 1100, 0.9, 2.0), ("hs", 6500, 0.7, 2.5))
    V["clap"] = peak(fit(sat(peak(comp(x, -24, 3.5, 0.002, 0.07, 7.0), 0), 4.0), 0.12, 0.09), -1.0)
    c = BR / "crash_17/cr/%s/cr_vl5_rr1.flac"
    x = mix((str(c) % "oh", 1.0), (str(c) % "cl", 0.3))
    x = eq(trim(x), ("hp", 750, 0.7), ("hp", 750, 0.7), ("pk", 1300, 1.0, -2.0), ("pk", 4200, 0.8, 3.0), ("hs", 7000, 0.7, 6.0))
    V["crash"] = peak(fit(x, 0.9, 0.5), -2.0)
    c = BR / "ride_22/rd/%s/rd_vl10_rr1.flac"
    x = mix((str(c) % "oh", 1.0), (str(c) % "cl", 0.5))
    x = eq(trim(x), ("hp", 650, 0.7), ("hp", 650, 0.7), ("pk", 1100, 1.2, -3.0), ("pk", 3800, 0.8, 4.0), ("hs", 6000, 0.7, 7.0))
    V["ride"] = peak(fit(x, 0.7, 0.4), -2.0)
    return V


# stream: bytes, rate (Hz), noise feedback (Q8: the coding noise leaves through 1 + a z^-1, under a falling spectrum),
# loop (attack s, loop s, crossfade s), dark (a looped tail's darkening per block)
PLAN = {
    "kick_a": dict(bytes=2400, nf=(128, 0)),
    "snare_a": dict(bytes=3800),
    "kick_b": dict(bytes=1700, rate=22050, nf=(128, 0)),
    "snare_b": dict(bytes=2200, rate=22050),
    "hatc": dict(bytes=1200),
    "hatc2": dict(bytes=1200),
    "hato": dict(bytes=3000, loop=(0.11, 0.10, 0.03), dark=4),
    "tom": dict(bytes=1600, rate=22050, nf=(128, 0)),
    "rim": dict(bytes=800),
    "clap": dict(bytes=2500),
    "crash": dict(bytes=4800, loop=(0.22, 0.16, 0.06), dark=2),
    "ride": dict(bytes=3900, loop=(0.16, 0.14, 0.05), dark=2),
    "snare_c": dict(bytes=1900),
}
# a kit: drum -> stream, alternate (a closed hat's other take, KICK SWAP's kick), semitones, tone, level (Q12), ms, pivot Hz
KITS = {
    "STUDIO": dict(
        BD=("kick_a", "kick_b", 0, 0, 4700, 1200, 4000), SD=("snare_a", None, 0, 0, 4800, 900, 6000),
        LT=("tom", None, -3, 0, 4700, 1300, 3000), MT=("tom", None, 1, 0, 4700, 1200, 3000), HT=("tom", None, 5, 0, 4700, 1100, 3000),
        RS=("rim", None, 0, 0, 5000, 400, 5000), CP=("clap", None, 0, 0, 5000, 700, 5000),
        CH=("hatc", "hatc2", 0, 0, 4600, 260, 10000), OH=("hato", None, 0, 0, 4200, 620, 10000),
        CR=("crash", None, 0, 0, 4000, 2400, 10000), RD=("ride", None, 0, 0, 3600, 2000, 10000)),
    "BOOM": dict(
        BD=("kick_b", "kick_a", 0, 0, 5000, 1500, 3000), SD=("snare_b", None, 0, 0, 5000, 700, 4000),
        LT=("tom", None, -5, -6, 4900, 1500, 3000), MT=("tom", None, -1, -6, 4900, 1400, 3000), HT=("tom", None, 3, -6, 4900, 1300, 3000),
        RS=("rim", None, -2, -6, 5500, 400, 5000), CP=("clap", None, -2, -6, 5500, 800, 5000),
        CH=("hatc", "hatc2", -1, -8, 5200, 240, 9000), OH=("hato", None, -1, -8, 4800, 560, 9000),
        CR=("crash", None, -1, -8, 4600, 2000, 9000), RD=("ride", None, -1, -8, 4200, 1800, 9000)),
    "TIGHT": dict(
        BD=("kick_a", "kick_b", 1, 4, 4700, 500, 4000), SD=("snare_c", None, 0, 0, 4800, 500, 6000),
        LT=("tom", None, -1, 4, 4700, 700, 3000), MT=("tom", None, 3, 4, 4700, 650, 3000), HT=("tom", None, 7, 4, 4700, 600, 3000),
        RS=("rim", None, 1, 4, 5000, 300, 5000), CP=("clap", None, 1, 4, 5000, 450, 5000),
        CH=("hatc", "hatc2", 1, 2, 4800, 180, 10000), OH=("hato", None, 1, 2, 4400, 360, 10000),
        CR=("crash", None, 1, 2, 4000, 1500, 10000), RD=("ride", None, 1, 2, 3600, 1400, 10000)),
}
DRUMS = ("BD", "SD", "LT", "MT", "HT", "RS", "CP", "CH", "OH", "CR", "RD")   # drum909.h D9_* order


def flat_loop(x, a, b, xf):
    """x from a on holds its level (the player's envelope decays it); x[b - xf .. b) crossfades into x[a - xf .. a)"""
    w = int(0.02 * R)
    e = np.sqrt(np.convolve(x ** 2, np.ones(w) / w, "same")) + 1e-5
    y = x[:b].copy()
    g = np.ones(b)
    g[a:b] = np.minimum(e[a] / e[a:b], 6.0)
    y *= g
    t = np.linspace(0, np.pi / 2, xf)
    y[b - xf:b] = y[b - xf:b] * np.cos(t) + y[a - xf:a] * np.sin(t)
    return y


def main():
    work = Path(tempfile.mkdtemp())
    codec = work / "drum_codec"
    subprocess.run(["cc", "-O2", "-o", str(codec), str(ROOT / "tools/drum_codec.c"), "-lm"], check=True)
    wavs = Path(sys.argv[sys.argv.index("--wav") + 1]) if "--wav" in sys.argv else None
    if wavs:
        wavs.mkdir(parents=True, exist_ok=True)

    def run(st, snr, la, nf):
        np.clip(np.round(st * 32767), -32768, 32767).astype(np.int16).tofile(work / "a.s16")
        o = subprocess.run([str(codec), str(work / "a.s16"), str(work / "a.bin"), str(work / "a.dec"), str(snr), "0", str(la),
                            str(nf[0]), str(nf[1])], capture_output=True, text=True, check=True).stdout.split()
        return [int(v) for v in o]

    V = hits()
    blob, streams = b"", []
    for name, cfg in PLAN.items():
        rate = cfg.get("rate", R)
        x = V[name] if rate == R else soxr.resample(V[name], R, rate, quality="VHQ")
        nf, loop = cfg.get("nf", (0, 0)), cfg.get("loop")
        best, lo = None, 0.0                     # the highest snr within the budget: bytes are not monotonic, a
        for snr in np.arange(40.0, -6.5, -0.5):  # starved predictor can wander and cost more
            if loop:
                a, n, xf = int(loop[0] * rate), int(loop[1] * rate), int(loop[2] * rate)
                la = run(flat_loop(x, a, a + n, xf), snr, a, nf)[2]      # the slice boundary at a ..
                o = run(flat_loop(x, la, la + n, xf), snr, la, nf)        # .. the tail built on it
                if o[2] != la:
                    continue
            else:
                o = run(x, snr, 0, nf)
            if o[1] <= cfg["bytes"]:
                lo, best = snr, (o, (work / "a.bin").read_bytes(), np.fromfile(work / "a.dec", dtype=np.int16))
                break
        if not best:
            raise SystemExit(f"{name}: no stream within {cfg['bytes']} B (at snr -6: {run(x, -6.0, 0, nf)[1]} B)")
        o, data, dec = best
        print(f"{name:8} {o[1]:5} B {o[0]:6} samples at {rate}, snr {lo:.1f}")
        if wavs:
            sf.write(str(wavs / f"{name}.wav"), dec, rate, subtype="PCM_16")
        streams.append(dict(name=name, at=len(blob) // 8, n=o[0], loop_at=o[2], loop_slice=o[3] // 8, state=o[4:12],
                            inc=round(rate / R * 32768), dark=cfg.get("dark", 0), crc=binascii.crc32(dec.astype("<i2").tobytes())))
        blob += data
    (ROOT / "assets/drums").mkdir(exist_ok=True)
    (ROOT / "assets/drums/kits.bin").write_bytes(blob)
    (ROOT / "assets/drums/kits.json").write_text(json.dumps(dict(streams=streams, kits=KITS, drums=DRUMS), indent=1) + "\n")
    print(f"{len(blob)} B of streams")


main()
