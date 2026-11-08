// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The kit maker's WebAssembly (build/kit/melodee_kit.wasm) against the firmware's Record kit: the .mel fixture's three
// kits (tests/mel_fixture.c: the firmware's own 909, 808 and CR-78 voices recorded through kit_take_core.c kt_keep,
// natively) made again here from the takes it fed them come out byte for byte the same. Then: a kit given back as the
// template builds to itself; a name, a pad's settings and a cleared pad change only those; a 48 kHz take is resampled
// to 44.1 kHz; a quiet take is refused, a long cymbal is cut to fit, a faded one plays once; the decoder plays every
// take as the device would.
//   web/kit/build.sh && node web/kit/check.mjs        (env MEL_DIR: the fixture's files, default build/kit/mel)
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';

const root = fileURLToPath(new URL('../../', import.meta.url));
const MEL = process.env.MEL_DIR || join(root, 'build/kit/mel');
const WASM = process.env.KIT_WASM || join(root, 'build/kit/melodee_kit.wasm');
const PADS = ['BD', 'SD', 'LT', 'MT', 'HT', 'RS', 'CP', 'CH', 'OH', 'CR', 'RD'];
const KM = { 0: 'untouched', 1: 'kept', 2: 'quiet', 3: 'no room', 5: 'cleared' };
for (const f of [WASM, join(MEL, 'kit-909.bin')])
    if (!existsSync(f)) { console.log(`kit: no ${f} (web/kit/build.sh first)`); process.exit(2); }

const { instance } = await WebAssembly.instantiate(readFileSync(WASM), { env: { emscripten_notify_memory_growth() {} } });
const w = instance.exports;
w._initialize();
const u8 = () => new Uint8Array(w.memory.buffer);
const MAX = w.kit_max();
const kit = {
    begin(rec) {
        if (!rec) return w.kit_begin(0, 0);
        const p = w.malloc(rec.length); u8().set(rec, p);
        const rc = w.kit_begin(p, rec.length); w.free(p); return rc;
    },
    name(s) {
        const p = w.malloc(s.length + 1); u8().set([...Buffer.from(s, 'latin1'), 0], p);
        const rc = w.kit_name(p); w.free(p); return rc;
    },
    pad(d, pcm, { rate = 44100, a = 0, b = pcm ? pcm.length : 0, fade = 0, level = 0, decay = 0, tone = 0, n } = {}) {
        if (!pcm) return w.kit_pad(d, 0, n, rate, 0, 0, fade, level, decay, tone);
        const p = w.malloc(pcm.length * 2 || 2);
        new Int16Array(w.memory.buffer, p, pcm.length).set(pcm);
        const rc = w.kit_pad(d, p, pcm.length, rate, a, b, fade, level, decay, tone); w.free(p); return rc;
    },
    build() {
        const p = w.malloc(MAX), n = w.kit_build(p, MAX);
        const out = n > 0 ? u8().slice(p, p + n) : n; w.free(p); return out;
    },
    report(d) { return [...new Int32Array(w.memory.buffer, w.kit_report(d), 8)]; },
    decode(d, max) {
        const p = w.malloc(max * 2), n = w.kit_decode(d, p, max);
        const out = n >= 0 ? new Int16Array(w.memory.buffer, p, n).slice() : null; w.free(p); return out;
    },
};

let fails = 0;
const check = (what, ok, more = '') => { console.log(`KIT WASM: ${what.padEnd(88)} ${ok ? 'ok' : 'FAIL'}${more && !ok ? '  ' + more : ''}`); fails += !ok; };
const same = (a, b) => {
    if (!(a instanceof Uint8Array)) return `build returned ${a}`;
    if (a.length !== b.length) return `${a.length} bytes, the device's ${b.length}`;
    const i = a.findIndex((v, k) => v !== b[k]);
    return i < 0 ? '' : `first difference at byte ${i}`;
};
const take = (kit, d) => {
    const f = join(MEL, `take-${kit}-${d}.s16`);
    if (!existsSync(f)) return null;
    const b = readFileSync(f);
    return new Int16Array(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
};
const header = (rec) => {
    const v = new DataView(rec.buffer, rec.byteOffset), map = [];
    for (let d = 0; d < 11; d++) {
        const o = 20 + 10 * d;
        map.push({ sample: rec[o], alt: rec[o + 1], out: v.getUint16(o + 4, true), ms: v.getUint16(o + 6, true), pivot: v.getUint16(o + 8, true) });
    }
    return { name: Buffer.from(rec.subarray(8, 20)).toString('latin1').replace(/\0+$/, ''), nsmp: rec[6], map };
};
const rms = (x) => Math.sqrt(x.reduce((s, v) => s + v * v, 0) / Math.max(1, x.length));

// 1. the fixture's kits, pad by pad in order as it recorded them: the device's bytes
const device = {};
for (const [name, slot] of [['909', 'KIT 1'], ['808', 'KIT 2'], ['cr78', 'KIT 3']]) {
    const want = new Uint8Array(readFileSync(join(MEL, `kit-${name}.bin`)));
    device[name] = want;
    kit.begin(null);
    if (slot !== 'KIT 1') kit.name(slot);                // (a new kit is KIT 1 unless named: the fixture's slot 1)
    const fed = [];
    for (let d = 0; d < 11; d++) { const x = take(name, d); if (x) { kit.pad(d, x); fed.push(PADS[d]); } }
    const got = kit.build(), why = same(got, want);
    check(`${name}: ${fed.length} takes through kt_keep == the device's kit (${want.length} B)`, !why, why);
    console.log('          ' + PADS.map((p, d) => { const r = kit.report(d); return r[0] === 1 ? `${p} ${r[1]}dB${r[2] ? ' cut' : ''}${r[6] ? ' loop' : ''} ${r[5]}B` : `${p} ${KM[r[0]]}`; }).join(', '));
}

// 2. a kit given back as the template, nothing changed: itself
check('the 909 kit as the template, built again unchanged: the same bytes', kit.begin(device['909']) === 0 && !same(kit.build(), device['909']));
check('a record that is not a kit is refused', kit.begin(device['909'].slice(0, 600)) < 0 && kit.begin(Uint8Array.from(device['909'], (v, i) => i < 4 ? 0 : v)) < 0);

// 3. the 808 kit renamed, SD's settings changed, RS cleared: everything else as it was
kit.begin(device['808']);
const before = Object.fromEntries([0, 3, 9].map((d) => [d, kit.decode(d, 20000)]));
kit.name('DRUMS'); kit.pad(1, null, { n: -1, level: 8000, decay: 500, tone: 2000 }); kit.pad(5, null, { n: 0 });
const ed = kit.build(), h0 = header(device['808']), h1 = ed instanceof Uint8Array ? header(ed) : null;
check('renamed DRUMS, SD level 8000 / -40 dB 500 ms / TONE 2000 Hz, RS cleared',
    h1 && h1.name === 'DRUMS' && h1.map[1].out === 8000 && h1.map[1].ms === 500 && h1.map[1].pivot === 2000 &&
    h1.map[5].sample === 0xFF && h1.nsmp === h0.nsmp - 1 && kit.report(5)[0] === 5);
check('the other pads play as before (BD, MT, CR decoded the same)',
    h1 && [0, 3, 9].every((d) => { const a = before[d], b = kit.decode(d, 20000); return a && b && a.length === b.length && a.every((v, i) => v === b[i]); }) &&
    [0, 2, 3, 4, 6, 7, 8, 9, 10].every((d) => ['out', 'ms', 'pivot'].every((k) => h1.map[d][k] === h0.map[d][k])));

// 4. takes of the page's own: 48 kHz, quiet, a long cymbal, a faded one
const tone = (rate, hz, secs, amp = 20000, decay = 6) => Int16Array.from({ length: Math.round(rate * secs) }, (_, i) =>
    Math.round(amp * Math.sin(2 * Math.PI * hz * i / rate) * Math.exp(-decay * i / rate)));
let lcg = 1;
const noise = (n, amp, decay) => Int16Array.from({ length: n }, (_, i) => { lcg = (lcg * 1664525 + 1013904223) >>> 0; return Math.round(((lcg >>> 8) / 8388608 - 1) * amp * Math.exp(-decay * i / 44100)); });
kit.begin(null);
kit.pad(1, tone(48000, 1000, 0.4), { rate: 48000 });     // SD: a 1 kHz tone at 48 kHz
kit.pad(2, tone(44100, 200, 0.5, 200));                   // LT: under -40 dBFS
kit.pad(9, noise(44100 * 3, 20000, 0.3));                 // CR: 3 s of noise, ringing to the end
kit.pad(8, noise(44100, 20000, 4), { fade: 50 });         // OH: faded out, so a one-shot
const mine = kit.build(), sd = kit.decode(1, 44100);
let zc = 0;
for (let i = 1; sd && i < sd.length; i++) zc += (sd[i - 1] < 0) !== (sd[i] < 0);
const hz = sd ? zc / 2 / (sd.length / 44100) : 0;
check(`a 48 kHz take resampled: kept at 44.1 kHz, its 1 kHz tone plays at ${hz.toFixed(0)} Hz`,
    mine instanceof Uint8Array && kit.report(1)[0] === 1 && kit.report(1)[4] === 44100 && Math.abs(hz - 1000) < 15);
check('a take under -40 dBFS is refused as quiet', kit.report(2)[0] === 2);
const cr = kit.report(9), oh = kit.report(8);
check(`3 s of crash: cut to fit at 12 dB and looped (${cr[3]} of ${cr[7]} samples, ${cr[5]} B)`, cr[0] === 1 && cr[1] === 12 && cr[2] === 1 && cr[6] === 1);
check('a faded open hat plays once (no loop)', oh[0] === 1 && oh[6] === 0);
const crd = kit.decode(9, 44100 * 4), ohd = kit.decode(8, 44100 * 4);
check('the decoder: a looped crash fills what is asked, a one-shot ends', crd && crd.length === 44100 * 4 && ohd && ohd.length < 44100 * 4 && rms(crd.subarray(0, 4410)) > 1000);

// 5. the device's takes as the player plays them: each one sounds, the one-shots end
kit.begin(device['909']);
const lens = PADS.map((_, d) => { const x = kit.decode(d, 44100 * 3); return x ? x.length : -1; });
check(`the 909 kit decoded at TUNE 0: every pad sounds (${lens.join(' ')})`, lens.every((n) => n > 441));

console.log(fails ? `kit: ${fails} FAILED` : 'kit: all ok');
process.exit(fails ? 1 : 0);
