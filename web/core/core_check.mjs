// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The virtual FM-1 (web/core/melodee_core.c in WebAssembly) driven from Node as Melodee Studio's worker drives it.
//   1. Protocol transcripts from a new device's power-on: INFO (its 'S' tag), CAPS, TRACKS, TRANSPORT, PROJECTS,
//      STORAGE, SONG2, SONG_SOUND, PUNCH, PATTERN_DATA, NOTES, MOTION on a bank, PATTERN, SETTINGS, MLEARN, DRUM; the same frames through the same core built natively must answer the same
//      bytes. Then a project (tests/mel_fixture.c's song) loaded into project 6 and the Studio commands again.
//      --transcripts FILE writes them as JSON lines (tools/studio_bundle.py puts them in the bundle).
//   2. A render of the song: speed, the time per 128-frame quantum, memory; the same render natively, sample for
//      sample; the taps (core_render_taps: the master the same as core_render's, each track and the FX return);
//      the MIDI events (core_midi_events) the song's notes; a kit (fixtures' kit-*.bin) into a user slot; the NOR
//      kept and booted again (core_boot(1)) with the project still there; core_midi_in playing a note.
//   3. The five synths as modules (CORE_MODULES): each power-on puts the device's module files in the pool and links
//      their WebAssembly (mod_link.mjs: build/modules/wasm/*.mmod, tools/mod_build.py --wasm) into the core, as the
//      native core links the module objects; a chord on two presets of each synth (SID, DRUM, CZ-1, FM6, the Prophet)
//      renders sample for sample what the native core (the device's configuration) renders.
//   web/core/build.sh; tools/studio_bundle.py (or tests/run_tests.sh: the fixtures); node web/core/core_check.mjs
// options: --core WASM (build/core/melodee_core.wasm), --native BIN (build/core/core_native), --mel DIR (the fixtures:
// build/host/studio/mel, else build/host/mel), --transcripts FILE, --transcripts-only, --seconds N (10), --json FILE,
// --modules DIR (build/modules/wasm: the five synths' files with their WebAssembly)
import { readFileSync, readdirSync, writeFileSync, existsSync, mkdtempSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { BACKUP_CMD } from "../fm1backup.js";
import { pjExpand } from "../mel_file.js";
import { installModules } from "./mod_link.mjs";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "../..");
const opt = (name, def) => { const i = process.argv.indexOf(`--${name}`); return i > 0 ? process.argv[i + 1] : def; };
const flag = (name) => process.argv.includes(`--${name}`);
const WASM = resolve(opt("core", join(ROOT, "build/core/melodee_core.wasm")));
const NATIVE = resolve(opt("native", join(ROOT, "build/core/core_native")));
const MEL = opt("mel", [join(ROOT, "build/host/studio/mel"), join(ROOT, "build/host/mel")].find((d) => existsSync(join(d, "song.rec"))));
const SECONDS = Number(opt("seconds", 10));
const ABI = 4;
const MODDIR = resolve(opt("modules", join(ROOT, "build/modules/wasm")));
const SYNTHS = [["sid", "SID"], ["drum", "DRUM"], ["cz", "CZ-1"], ["fm6", "FM6"], ["prophet", "PROPHET"]];
/* a synth's handle on this core by its name (INFO: the version, five bytes, then each handle's name; Melodee 2.0's
   handles are the device's own, never a synth's identity) */
const handleOf = (c, name) => {
  const a = c.ask(1);
  let i = a.indexOf(0) + 1;
  const n = a[i];
  i += 5;
  for (let e = 0; e < n; e++) {
    const j = a.indexOf(0, i);
    if (String.fromCharCode(...a.slice(i, j)) === name.slice(0, 8)) return e;
    i = j + 1;
  }
  return -1;
};
const modFiles = SYNTHS.map(([s]) => readFileSync(join(MODDIR, `${s}.mmod`)));
const hex = (a) => Array.from(a, (b) => b.toString(16).toUpperCase().padStart(2, "0")).join(" ");
const report = {};
let failures = 0;
const check = (what, ok, detail = "") => {
  console.log(`core: ${what.padEnd(78)} ${ok ? "ok" : "FAIL"}${detail ? `  ${detail}` : ""}`);
  if (!ok) failures++;
  return ok;
};

// ------------------------------------------------------------------ the core ---
const module = await WebAssembly.compile(readFileSync(WASM));
const imports = WebAssembly.Module.imports(module);
async function core() {
  const inst = await WebAssembly.instantiate(module, {});
  const x = inst.exports;
  x._initialize?.();
  const s = x.core_scratch(), size = x.core_scratch_size();
  const u8 = () => new Uint8Array(x.memory.buffer);
  return {
    x, u8,
    exchange(frame) {                                   // one frame in, every frame that came back
      u8().set(frame, s);
      x.core_sysex_in(s, frame.length);
      return this.take();
    },
    take() {
      const n = x.core_sysex_out(s, size), b = u8().slice(s, s + n), out = [];
      for (let i = 0, a = -1; i < b.length; i++) { if (b[i] === 0xF0) a = i; else if (b[i] === 0xF7 && a >= 0) { out.push(b.slice(a, i + 1)); a = -1; } }
      return out;
    },
    ask(cmd, args = []) {                               // the payload of the reply with the same command
      const f = this.exchange(req(cmd, args)).find((r) => r[4] === cmd);
      return f ? Array.from(f.slice(5, -1)) : null;
    },
    put(bytes) { u8().set(bytes, s); return s; },
  };
}
const req = (cmd, args = []) => Uint8Array.from([0xF0, 0x7D, 0x46, 0x4C, cmd, ...args, 0xF7]);
/* power-on as an installed FM-1's (core_native's boot_modules): a new device's NOR, the synths' files in its pool
   (their WebAssembly linked), booted again. keep: the NOR has them (written to core_nor_ptr): linked, booted. 1 =
   the flash works */
let installed = null;
async function power(c, keep = false) {
  if (keep) { await installModules(c.x, modFiles, false); return c.x.core_boot(1); }
  c.x.core_boot(0); c.x.core_poll(100);
  const rc = await installModules(c.x, modFiles);
  installed ??= rc;
  c.x.core_poll(100);
  const ok = c.x.core_boot(1);
  c.x.core_poll(100);
  return ok;
}

check("no imports (a standalone module: no glue)", imports.length === 0, imports.map((i) => `${i.module}.${i.name}`).join(", "));
const t0 = performance.now();
const a = await core();
check(`ABI ${ABI}`, a.x.core_abi() === ABI, `core says ${a.x.core_abi()}`);
const flashOk = await power(a);
check("the five synths' modules: their WebAssembly linked, the device's files in the pool (MS_LOADED, MS_STORED or MS_NORAM)",
  installed.length === 5 && installed.every((rc) => rc === 1 || rc === 2 || rc === 4), installed.join(" "));
report.boot = { ms: +(performance.now() - t0).toFixed(1), flashOk, wasmBytes: readFileSync(WASM).length };
check("power-on with an erased NOR: the flash works", flashOk === 1, `${report.boot.ms} ms with instantiation`);

// ------------------------------------------------------------------ 1. transcripts ---
const FRESH = [
  ["INFO", 1], ["CAPS", 39], ["TRACKS", 40], ["TRANSPORT", 41], ["PROJECTS status", 42], ["PROJECTS list", 42, [0, 0, 32]],
  ["STORAGE", 43], ["SONG2 head", 47], ["SONG2 sections", 47, [0, 0, 41]], ["SONG2 timing", 47, [1]],
  ["SONG_SOUND list", 48, [0, 0, 50]], ["PUNCH state", 49], ["PUNCH names", 49, [0]],
  ["PATTERN_DATA", 44, [0, 0, 0]], ["NOTES list", 45, [0, 0, 0, 0, 0, 127]], ["MOTION bank 2", 64, [0, 5, 1]],
  ["PATTERN query", 73, [0, 0]],
  ["SETTINGS", 50], ["SETTINGS list", 50, [0, 0, 127]], ["SETTINGS set LIGHTS", 50, [1, 3, 4, 0]], ["MLEARN", 51],
  ["DRUM track 4", 53, [3]], ["DRUM write", 53, [3, 0, 1, 30, 70, 60, 100]],
];
const transcripts = [];
const run = (c, phase, list) => {
  for (const [name, cmd, args = []] of list) {
    const r = req(cmd, args), replies = c.exchange(r);
    transcripts.push({ phase, name, request: hex(r), replies: replies.map(hex) });
  }
};
run(a, "power-on", FRESH);
const at = (name, phase = "power-on") => transcripts.find((t) => t.name === name && t.phase === phase);
const bytes = (h) => h.split(" ").map((v) => parseInt(v, 16));
{
  const info = bytes(at("INFO").replies[0]);
  const tag = (t, v, c) => info.some((x, i) => i >= 25 && x === t && info[i + 1] === v && info[i + 2] === c);   /* (as Studio reads it) */
  check("INFO: the Studio tag 'S' v1, CAPS at 39", tag(0x53, 1, 39), hex(info.slice(-7, -1)));
  check("INFO: the synths' tag 0x59 v1, SCHEMA at 85", tag(0x59, 1, 85), hex(info.slice(-7, -1)));
  const caps = bytes(at("CAPS").replies[0]);
  check("CAPS: revision 1 and the song's tag 'S'", caps[5] === 1 && caps.includes(0x53), `${caps.length} bytes`);
  const tracks = bytes(at("TRACKS").replies[0]).slice(5, -1);
  check("TRACKS: NTRK 8, four tracks in use", tracks[2] === 8 && tracks[3] === 4, `op ${tracks[0]} rc ${tracks[1]}`);
  check("CAPS: the patterns' tag 'D'", caps.includes(0x44), `${caps.length} bytes`);
  check("CAPS: the FM-1 page's tag 'C' (no Bluetooth here)", caps.includes(0x43), `${caps.length} bytes`);
  for (const name of ["TRANSPORT", "PROJECTS status", "PROJECTS list", "STORAGE", "SONG2 head", "SONG2 sections", "SONG2 timing", "SONG_SOUND list", "PUNCH state", "PUNCH names",
                      "PATTERN_DATA", "NOTES list", "MOTION bank 2", "PATTERN query", "SETTINGS", "SETTINGS list", "SETTINGS set LIGHTS",
                      "MLEARN", "DRUM track 4", "DRUM write"])
    check(`${name}: answered`, at(name).replies.length === 1, `${bytes(at(name).replies[0] ?? "").length} bytes`);
}
if (existsSync(NATIVE)) {
  const wire = Buffer.concat(FRESH.map(([, cmd, args = []]) => req(cmd, args)));
  const r = spawnSync(NATIVE, [], { input: wire, maxBuffer: 1 << 26 });
  const lines = r.stdout.toString().trim().split("\n").filter((l) => l[0] === "<").map((l) => l.slice(2));
  const mine = transcripts.flatMap((t) => t.replies);
  check("the same frames natively: the same replies, byte for byte", r.status === 0 && lines.join("|") === mine.join("|"),
    `${mine.length} replies`);
} else console.log(`core: (no ${NATIVE}: native comparison skipped)`);

let rec = null;
if (MEL) {
  const stored = readFileSync(join(MEL, "song.hpj")), whole = readFileSync(join(MEL, "song.rec"));
  rec = pjExpand(new Uint8Array(stored));
  check("song.hpj expanded (mel_file.js pjExpand) equals song.rec", rec.length === whole.length && rec.every((b, i) => b === whole[i]));
  const rc = a.x.core_load_record(a.put(rec), rec.length, 5);
  a.x.core_poll(30);
  check("core_load_record: the song into project 6 (slots past the host mirrors' four)", rc === 0, `rc ${rc}`);
  run(a, "song in project 6", [["PROJECTS status", 42], ["PROJECTS list", 42, [0, 0, 32]], ["TRACKS", 40], ["STORAGE", 43],
    ["SONG2 head", 47], ["SONG2 sections", 47, [0, 0, 41]], ["SONG2 timing", 47, [1]], ["SONG_SOUND list", 48, [0, 0, 50]], ["PUNCH events", 49, [3]],
    ["PATTERN_DATA", 44, [0, 0, 0]], ["NOTES list", 45, [0, 0, 0, 0, 0, 127]], ["MOTION", 64, [0]], ["DRUM track 4", 53, [3]]]);
  const st = bytes(at("PROJECTS status", "song in project 6").replies[0]).slice(5, -1);
  check("PROJECTS: the music is project 6", st[2] === 5, `op ${st[0]} rc ${st[1]} slot ${st[2]}`);
  const head = bytes(at("SONG2 head", "song in project 6").replies[0]).slice(5, -1);
  check("SONG2: the song's sections", head[2] > 0, `${head[2]} sections, BPM ${head[4] | head[5] << 7}`);
} else console.log("core: (no fixtures: tools/studio_bundle.py or tests/run_tests.sh makes them; the song checks skipped)");
const tfile = opt("transcripts");
if (tfile) {
  writeFileSync(tfile, transcripts.map((t) => JSON.stringify(t)).join("\n") + "\n");
  console.log(`core: ${transcripts.length} transcripts -> ${tfile}`);
}
if (flag("transcripts-only") || !rec) finish();

// ------------------------------------------------------------------ 2. sound ---
const FS = a.x.core_fs(), frames = SECONDS * FS, NTRK = a.x.core_ntrk(), TAPS = a.x.core_taps_count();
async function songCore({ taps = false, midi = 0 } = {}) {                       // a fresh power-on, the song loaded
  const c = await core();
  await power(c);
  c.x.core_load_record(c.put(rec), rec.length, 0); c.x.core_poll(30);
  if (taps) c.x.core_taps(1);
  if (midi) c.x.core_midi_tap(midi);
  const rc = c.x.core_song_prepare(0);
  return { c, rc };
}
{
  const { c, rc } = await songCore();
  check("core_song_prepare(0)", rc === 0, `rc ${rc}`);
  const songFrames = c.x.core_song_samples(0);
  const wav = new Float32Array(2 * frames);
  const tr = performance.now();
  for (let f = 0; f < frames; f += 1024) {
    const k = Math.min(1024, frames - f);
    wav.set(new Float32Array(c.x.memory.buffer, c.x.core_render(k), 2 * k), 2 * f);
  }
  const ms = performance.now() - tr;
  let peak = 0, sum = 0;
  for (const v of wav) { peak = Math.max(peak, Math.abs(v)); sum += v * v; }
  report.render = { seconds: SECONDS, ms: +ms.toFixed(1), xRealtime: +((SECONDS * 1000) / ms).toFixed(1), peak: +peak.toFixed(4),
    rmsDb: +(10 * Math.log10(sum / wav.length || 1e-24)).toFixed(1), songSeconds: +(songFrames / FS).toFixed(2), memory: c.x.memory.buffer.byteLength,
    staticBytes: c.x.__heap_base?.value ?? null };
  check(`render: ${SECONDS} s of the song`, peak > 0.01 && peak <= 1.0,
    `${report.render.xRealtime}x real time, peak ${report.render.peak}, RMS ${report.render.rmsDb} dBFS, song ${report.render.songSeconds} s`);
  // quanta as the AudioWorklet asks for them (the worker renders ahead; this is the JIT-warm cost)
  const q = [];
  for (let i = 0; i < 2000; i++) { const t = performance.now(); c.x.core_render(128); q.push(performance.now() - t); }
  q.sort((u, v) => u - v);
  const pct = (p) => +(q[Math.floor(q.length * p)] * 1000).toFixed(0);
  report.quantum = { p50us: pct(0.5), p99us: pct(0.99), maxUs: +(q[q.length - 1] * 1000).toFixed(0), budgetUs: +((128 / FS) * 1e6).toFixed(0) };
  check("128-frame quanta well inside their time", report.quantum.p99us < report.quantum.budgetUs,
    `p50 ${report.quantum.p50us} us, p99 ${report.quantum.p99us} us, max ${report.quantum.maxUs} us of ${report.quantum.budgetUs} us`);
  check("memory: never grew past 16 MiB", c.x.memory.buffer.byteLength === 16 << 20, `${c.x.memory.buffer.byteLength >> 20} MiB, static ${((report.render.staticBytes ?? 0) / 1048576).toFixed(2)} MiB`);

  if (existsSync(NATIVE)) {
    const dir = mkdtempSync(join(tmpdir(), "core-")), rp = join(dir, "rec.bin"), op = join(dir, "out.f32");
    writeFileSync(rp, rec);
    const r = spawnSync(NATIVE, ["render", rp, String(SECONDS), op]);
    const nf = r.status === 0 ? new Float32Array(readFileSync(op).buffer.slice(0)) : new Float32Array(0);
    let first = -1;
    for (let i = 0; i < wav.length && first < 0; i++) if (wav[i] !== nf[i]) first = i;
    check("render: WASM == native, every sample", r.status === 0 && first < 0 && nf.length === wav.length, first >= 0 ? `first difference at frame ${first >> 1}` : "");
    rmSync(dir, { recursive: true, force: true });
  }

  // the taps: the same sound, each track's share and the FX return beside it; the notes as events
  const t = await songCore({ taps: true, midi: 1 | 4 });
  const master = new Float32Array(2 * frames), parts = Array.from({ length: TAPS }, () => new Float32Array(2 * frames));
  const evs = [];
  const evBuf = t.c.x.core_scratch();
  const tt = performance.now();
  for (let f = 0; f < frames; f += 1024) {
    const k = Math.min(1024, frames - f), p = t.c.x.core_render_taps(k);
    const all = new Float32Array(t.c.x.memory.buffer, p, 2 * k * (1 + TAPS));
    master.set(all.subarray(0, 2 * k), 2 * f);
    for (let j = 0; j < TAPS; j++) parts[j].set(all.subarray(2 * k * (1 + j), 2 * k * (2 + j)), 2 * f);
    for (let n; (n = t.c.x.core_midi_events(evBuf, 1024)) > 0;) {
      const b = t.c.u8().slice(evBuf, evBuf + 8 * n), v = new DataView(b.buffer);
      for (let i = 0; i < n; i++) evs.push({ frame: v.getUint32(8 * i, true), tag: b[8 * i + 4], note: b[8 * i + 5], vel: b[8 * i + 6], hi: b[8 * i + 7] });
    }
  }
  const tms = performance.now() - tt;
  let same = master.length === wav.length;
  for (let i = 0; same && i < wav.length; i++) same = master[i] === wav[i];
  check("taps on: the master is core_render's, every sample", same, `${((SECONDS * 1000) / tms).toFixed(1)}x real time with taps`);
  const tapPeak = parts.map((p) => p.reduce((m, v) => Math.max(m, Math.abs(v)), 0));
  check("taps: the tracks in use sound on their own taps", tapPeak.slice(0, 4).filter((p) => p > 0.001).length >= 2,
    tapPeak.map((p, i) => `${i < TAPS - 1 ? `T${i + 1}` : "FX"} ${p.toFixed(3)}`).join(", "));
  let err = 0, energy = 0;
  for (let i = 0; i < wav.length; i++) { let s = 0; for (const p of parts) s += p[i]; err += (s - wav[i]) ** 2; energy += wav[i] ** 2; }
  report.taps = { peaks: tapPeak.map((p) => +p.toFixed(4)), sumVsMasterDb: +(10 * Math.log10(err / (energy || 1e-24))).toFixed(1) };
  console.log(`core: (the taps' sum against the master, before punch, limiter and DC blocker: ${report.taps.sumVsMasterDb} dB residual)`);
  const moves = evs.filter((e) => (e.tag & 0xF0) === 0x40), notes = evs.filter((e) => e.tag < 8);
  const ons = notes.filter((e) => e.vel > 0), offs = notes.filter((e) => e.vel === 0);
  const perTrack = Array.from({ length: NTRK }, (_, k) => ons.filter((e) => e.tag === k).length);
  check("MIDI events: the song's notes, on and off, in frame order", ons.length > 10 && offs.length > 0 &&
    evs.every((e, i) => !i || e.frame >= evs[i - 1].frame), `${ons.length} on, ${offs.length} off; per track ${perTrack.join(" ")}`);
  report.events = { on: ons.length, off: offs.length, perTrack, motion: moves.length };
  // motion over the whole song (tests/mel_fixture.c's song moves E1 at random steps of some patterns): its values
  {
    const m = await songCore({ midi: 4 }), all = m.c.x.core_song_samples(0), got = [];
    for (let f = 0; f < all; f += 4096) {
      m.c.x.core_render(Math.min(4096, all - f));
      for (let n; (n = m.c.x.core_midi_events(evBuf, 1024)) > 0;) {
        const b = m.c.u8().slice(evBuf, evBuf + 8 * n);
        for (let i = 0; i < n; i++) got.push({ tag: b[8 * i + 4], id: b[8 * i + 5], v: (b[8 * i + 6] | b[8 * i + 7] << 8) << 16 >> 16 });
      }
    }
    report.events.motion = got.length;
    check("MIDI events: the values motion sets (core_midi_tap bit 2), int16, audio tracks only", got.length > 0 &&
      got.every((e) => (e.tag & 0xF0) === 0x40 && (e.tag & 15) < 4 && e.id < 128 && e.v >= -64 && e.v <= 127) && !moves.some((e) => e.tag < 8),
      `${got.length} values over the song; params ${[...new Set(got.map((e) => e.id))].join(" ")}; ${moves.length} in its first ${SECONDS} s`);
  }

  // the master without its limiter (an export's choice): the same until the limiter would act, never quieter
  const r = await songCore();
  r.c.x.core_master_raw(1);
  const raw = new Float32Array(2 * frames);
  for (let f = 0; f < frames; f += 1024) {
    const k = Math.min(1024, frames - f);
    raw.set(new Float32Array(r.c.x.memory.buffer, r.c.x.core_render(k), 2 * k), 2 * f);
  }
  let rawPeak = 0, before = -1;
  for (let i = 0; i < raw.length; i++) { rawPeak = Math.max(rawPeak, Math.abs(raw[i])); if (before < 0 && raw[i] !== wav[i]) before = i; }
  const was = r.c.x.core_master_raw(0);
  report.masterRaw = { peak: +rawPeak.toFixed(4), limited: +report.render.peak, firstDifference: before >> 1 };
  check("core_master_raw(1): the limiter and soft clip off, the sound as loud or louder", was === 1 && rawPeak >= report.render.peak - 1e-6,
    `peak ${rawPeak.toFixed(4)} (limited ${report.render.peak})${before < 0 ? ", never limited here" : `, first difference at frame ${before >> 1}`}`);
}

// a kit, the NOR kept, a note from core_midi_in
{
  const kits = readdirSync(MEL).filter((f) => /^kit-.*\.bin$/.test(f)).sort();
  const c = (await songCore()).c;
  if (kits.length) {
    const kit = readFileSync(join(MEL, kits[0]));
    const rc = c.x.core_load_kit(c.put(kit), kit.length, 2);
    const list = c.ask(BACKUP_CMD.LIST);
    let size = -1;
    for (let i = 0, k = (list.length - 3) / 11; i < k; i++) if (list[3 + i * 11] === 94) size = list[4 + i * 11] | list[5 + i * 11] << 7 | list[6 + i * 11] << 14;
    check(`core_load_kit: ${kits[0]} into KIT 11 (BACKUP LIST id 94)`, rc === 0 && size === kit.length, `rc ${rc}, listed ${size} B of ${kit.length}`);
    check("core_load_kit: a record that is not a kit refused", c.x.core_load_kit(c.put(new Uint8Array(4096)), 4096, 3) === 1);
  }
  c.ask(42, [3, 5, ...Array.from("CHECK", (ch) => ch.charCodeAt(0))]);
  const nor = c.u8().slice(c.x.core_nor_ptr(), c.x.core_nor_ptr() + c.x.core_nor_len());
  const d = await core();
  d.u8().set(nor, d.x.core_nor_ptr());
  const ok = await power(d, true);
  d.x.core_poll(100);
  const list = d.ask(42, [0, 0, 32]);
  const text = String.fromCharCode(...(list ?? []).filter((b) => b >= 32 && b < 127));
  check("the NOR kept and booted again: projects 1 and 6 still there", ok === 1 && text.includes("CHECK"), `${nor.length} B`);
  // a note in, as from a keyboard on USB: track 1 sounds
  d.x.core_midi_tap(1);
  const sel = d.ask(40, [1, 0]);
  d.u8().set([0x90, 60, 100], d.x.core_scratch());
  const taken = d.x.core_midi_in(d.x.core_scratch(), 3);
  let loud = 0;
  for (let i = 0; i < 20; i++) { const p = new Float32Array(d.x.memory.buffer, d.x.core_render(1024), 2048); for (const v of p) loud = Math.max(loud, Math.abs(v)); }
  const n = d.x.core_midi_events(d.x.core_scratch(), 64);
  check("core_midi_in: a note on channel 1 plays the selected track", taken === 1 && loud > 0.001 && n > 0 && sel !== null,
    `peak ${loud.toFixed(3)}, ${n} event(s)`);
}

// ------------------------------------------------------------------ 3. the synths ---
for (const [name, label] of SYNTHS) for (const p of [0, 5]) {
  const c = await core();
  await power(c);
  const e = handleOf(c, label);
  c.exchange(req(8, [e, p]));
  c.x.core_poll(30);
  const s = c.x.core_scratch(), on = [0x90, 48, 100, 0x90, 55, 90, 0x90, 64, 80], off = [0x80, 48, 0, 0x80, 55, 0, 0x80, 64, 0];
  c.u8().set(on, s); c.x.core_midi_in(s, on.length);
  const wasm = new Float32Array(3 * FS * 2 + 2048);
  let at = 0;
  for (let k = 0; k < 3 * FS; k += 1024) {
    if (k === FS - FS % 1024) { c.u8().set(off, s); c.x.core_midi_in(s, off.length); }
    wasm.set(new Float32Array(c.x.memory.buffer, c.x.core_render(1024), 2048), at); at += 2048;
  }
  const dir = mkdtempSync(join(tmpdir(), "core-synth-")), out = join(dir, "synth.raw");
  const r = spawnSync(NATIVE, ["synth", String(e), String(p), out], { encoding: "utf8", env: { ...process.env, CORE_MODULES_DIR: join(ROOT, "build/modules") } });
  const native = r.status === 0 ? new Float32Array(readFileSync(out).buffer.slice(0)) : new Float32Array(0);
  rmSync(dir, { recursive: true, force: true });
  let peak = 0, same = native.length === at;
  for (let i = 0; i < at; i++) { peak = Math.max(peak, Math.abs(wasm[i])); if (same && Object.is(wasm[i], native[i]) === false && wasm[i] !== native[i]) same = false; }
  check(`${name.toUpperCase()} preset ${p}: the module in WebAssembly == the native core's module, sample for sample`, same && peak > 0.001,
    `${at / 2} frames, peak ${peak.toFixed(3)}${r.status ? `, native: ${r.stdout}${r.stderr}` : ""}`);
}
finish();

function finish() {
  const j = opt("json");
  if (j) writeFileSync(j, JSON.stringify(report, null, 1));
  console.log(failures ? `core: ${failures} FAILED` : "core: all checks passed");
  process.exit(failures ? 1 : 0);
}
