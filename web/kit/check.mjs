// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The kit maker's WebAssembly (build/kit/melodee_kit.wasm) against the firmware's Record kit: the .mel fixture's three
// kits (tests/mel_fixture.c: the firmware's own 909, 808 and CR-78 voices on eleven pads, each take streamed into a pad
// record through kit_take_core.c, natively) made again here from the takes it fed them come out byte for byte the same.
// Then: a kit given back as the template builds to itself; a name, a pad's settings and a cleared pad change only those;
// a 48 kHz take is resampled to 44.1 kHz; a quiet take is refused, a long cymbal is kept whole, a faded one ends; the
// decoder plays every take as the device would.
//   web/kit/build.sh && node web/kit/check.mjs        (env MEL_DIR: the fixture's files, default build/kit/mel)
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';

const root = fileURLToPath(new URL('../../', import.meta.url));
const MEL = process.env.MEL_DIR || join(root, 'build/kit/mel');
const WASM = process.env.KIT_WASM || join(root, 'build/kit/melodee_kit.wasm');
const PADS = ['BD', 'SD', 'LT', 'MT', 'HT', 'RS', 'CP', 'CH', 'OH', 'CY', 'CB', 'LC', 'MC', 'HC', 'CL', 'MA'];
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
const header = (blob) => {                               // a KIT2 blob: its name, each pad's record header
    const v = new DataView(blob.buffer, blob.byteOffset), map = [];
    let at = 88, n = 0;
    for (let d = 0; d < 16; d++) {
        const len = v.getUint32(20 + 4 * d, true);
        map.push(len ? { len, out: v.getUint16(at + 24, true), ms: v.getUint16(at + 26, true), pivot: v.getUint16(at + 28, true), at } : null);
        n += len ? 1 : 0; at += len;
    }
    return { name: Buffer.from(blob.subarray(8, 20)).toString('latin1').replace(/\0+$/, ''), npad: n, map };
};
const rms = (x) => Math.sqrt(x.reduce((s, v) => s + v * v, 0) / Math.max(1, x.length));

// 1. the fixture's kits, pad by pad in order as it recorded them: the device's bytes
const device = {};
for (const name of ['909', '808', 'cr78']) {
    const want = new Uint8Array(readFileSync(join(MEL, `kit-${name}.bin`)));
    device[name] = want;
    kit.begin(null);
    kit.name(name);
    const fed = [];
    for (let d = 0; d < 16; d++) { const x = take(name, d); if (x) { kit.pad(d, x); fed.push(PADS[d]); } }
    const got = kit.build(), why = same(got, want);
    check(`${name}: ${fed.length} takes streamed into pad records == the device's kit (${want.length} B)`, !why, why);
    console.log('          ' + PADS.slice(0, fed.length).map((p, d) => { const r = kit.report(d); return r[0] === 1 ? `${p} ${r[3]} ${r[5]}B` : `${p} ${KM[r[0]]}`; }).join(', '));
}

// 2. a kit given back as the template, nothing changed: itself
check('the 909 kit as the template, built again unchanged: the same bytes', kit.begin(device['909']) === 0 && !same(kit.build(), device['909']));
check('a blob that is not a kit is refused', kit.begin(device['909'].slice(0, 600)) < 0 && kit.begin(Uint8Array.from(device['909'], (v, i) => i < 4 ? 0 : v)) < 0);

// 3. the 808 kit renamed, SD's settings changed, RS cleared: everything else as it was
kit.begin(device['808']);
const before = Object.fromEntries([0, 3, 9].map((d) => [d, kit.decode(d, 20000)]));
kit.name('DRUMS'); kit.pad(1, null, { n: -1, level: 8000, decay: 500, tone: 2000 }); kit.pad(5, null, { n: 0 });
const ed = kit.build(), h0 = header(device['808']), h1 = ed instanceof Uint8Array ? header(ed) : null;
check('renamed DRUMS, SD level 8000 / -40 dB 500 ms / TONE 2000 Hz, RS cleared',
    h1 && h1.name === 'DRUMS' && h1.map[1].out === 8000 && h1.map[1].ms === 500 && h1.map[1].pivot === 2000 &&
    !h1.map[5] && h1.npad === h0.npad - 1 && kit.report(5)[0] === 5);
check('the other pads play as before (BD, MT, CY decoded the same)',
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
kit.pad(9, noise(44100 * 3, 20000, 0.3));                 // CY: 3 s of noise, ringing to the end
kit.pad(8, noise(44100, 20000, 4), { fade: 50 });         // OH: faded out
const mine = kit.build(), sd = kit.decode(1, 44100);
let zc = 0;
for (let i = 1; sd && i < sd.length; i++) zc += (sd[i - 1] < 0) !== (sd[i] < 0);
const hz = sd ? zc / 2 / (sd.length / 44100) : 0;
check(`a 48 kHz take resampled: kept at 44.1 kHz, its 1 kHz tone plays at ${hz.toFixed(0)} Hz`,
    mine instanceof Uint8Array && kit.report(1)[0] === 1 && kit.report(1)[4] === 44100 && Math.abs(hz - 1000) < 15);
check('a take under -40 dBFS is refused as quiet', kit.report(2)[0] === 2);
const cy = kit.report(9), oh = kit.report(8);
check(`3 s of cymbal kept whole at 30 dB (${cy[3]} of ${cy[7]} samples, ${cy[5]} B), no loop`, cy[0] === 1 && cy[1] === 30 && cy[3] === cy[7] && cy[6] === 0);
check('a faded open hat is kept', oh[0] === 1 && oh[6] === 0);
const cyd = kit.decode(9, 44100 * 4), ohd = kit.decode(8, 44100 * 4);
check('the decoder: the takes end where they end', cyd && cyd.length < 44100 * 4 && cyd.length >= 44100 * 3 - 64 && ohd && ohd.length < 44100 * 2 && rms(cyd.subarray(0, 4410)) > 1000);

// 5. the device's takes as the player plays them: each one sounds, the one-shots end
kit.begin(device['909']);
const lens = PADS.slice(0, 11).map((_, d) => { const x = kit.decode(d, 44100 * 3); return x ? x.length : -1; });
check(`the 909 kit decoded at TUNE 0: every pad sounds (${lens.join(' ')})`, lens.every((n) => n > 441));

console.log(fails ? `kit: ${fails} FAILED` : 'kit: all ok');
process.exit(fails ? 1 : 0);
