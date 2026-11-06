// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The virtual FM-1 spike (web/core/melodee_core.c in WebAssembly) driven from Node as Melodee Studio's worker would:
// the editor's own SysEx in and out (INFO, DUMP, BACKUP LIST/GET through web/fm1backup.js, PROJECT save/query/load),
// a project record from tests/mel_fixture.c emit (its stored form expanded by web/mel_file.js pjExpand) loaded and its
// song rendered to a WAV; PROJECT save while that song plays (editor.c ed_flash_stop waiting through fm1_wdt_feed).
// Then the same frames through the same core built natively and through tests/editor_test.c's harness
// (build/core/core_native, build/core/editor_replay), and the WASM render against the native one, sample for sample.
//   web/core/build.sh; build/host/mel_fixture emit build/host/mel; node web/core/spike.mjs
// env: MEL_DIR (default build/host/mel), SONG (default song), SECONDS (default 30)
import { readFileSync, writeFileSync, existsSync, mkdirSync, statSync, mkdtempSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { tmpdir } from "node:os";
import { captureBackup, BACKUP_CMD } from "../fm1backup.js";
import { pjExpand } from "../mel_file.js";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "../..");
const OUT = join(ROOT, "build/core");
const MEL = process.env.MEL_DIR || join(ROOT, "build/host/mel");
const SONG = process.env.SONG || "song", SECONDS = Number(process.env.SECONDS || 30);
const WASM = join(OUT, "melodee_core.wasm");
const hex = (a) => Array.from(a, (b) => b.toString(16).toUpperCase().padStart(2, "0")).join(" ");
const ms = (t) => `${t.toFixed(1)} ms`;
const report = {};

// ------------------------------------------------------------------ the core ---
const t0 = performance.now();
const module = await WebAssembly.compile(readFileSync(WASM));
const imported = WebAssembly.Module.imports(module).map((i) => `${i.module}.${i.name}`);
const calls = {};
let mem;
const imports = {};
for (const i of WebAssembly.Module.imports(module)) {
  if (i.kind !== "function") continue;
  (imports[i.module] ??= {})[i.name] = i.name === "fd_write" ? (fd, iov, n, nw) => {   // printf (none expected)
    const v = new DataView(mem.buffer); let s = "", k = 0;
    for (let j = 0; j < n; j++) { const p = v.getUint32(iov + 8 * j, true), l = v.getUint32(iov + 8 * j + 4, true);
      s += Buffer.from(mem.buffer, p, l).toString(); k += l; }
    process.stdout.write(`[core fd ${fd}] ${s}`); v.setUint32(nw, k, true); return 0;
  } : i.name === "proc_exit" ? (c) => { throw new Error(`core exited ${c}`); } : () => { calls[i.name] = (calls[i.name] || 0) + 1; return 52; };
}
async function core() {
  const inst = await WebAssembly.instantiate(module, imports);
  const x = inst.exports;
  mem = x.memory;
  x._initialize?.();
  return x;
}
const x = await core();
const tInst = performance.now() - t0;
const tb = performance.now();
const flashOk = x.core_boot(0);
x.core_poll(100);
const tBoot = performance.now() - tb;
const heapBase = x.__heap_base ? x.__heap_base.value : 0;   // stack (first) + data + bss: what the core uses at most
report.wasm = { bytes: statSync(WASM).size, imports: imported, compileInstantiateMs: +tInst.toFixed(1), bootMs: +tBoot.toFixed(1),
  memoryAfterBoot: x.memory.buffer.byteLength, staticBytes: heapBase, flashOk };
console.log(`core: ${report.wasm.bytes} B wasm, imports [${imported.join(", ") || "none"}], compile+instantiate ${ms(tInst)}, boot ${ms(tBoot)}, ` +
  `flash_ok ${flashOk}, memory ${x.memory.buffer.byteLength >> 20} MiB, static ${(heapBase / 1048576).toFixed(2)} MiB (1 MiB stack, no heap use)`);

const scr = x.core_scratch(), scrN = x.core_scratch_size();
const u8 = () => new Uint8Array(x.memory.buffer);
function exchange(frame) {                     // one frame in, every frame out
  u8().set(frame, scr);
  x.core_sysex_in(scr, frame.length);
  const n = x.core_sysex_out(scr, scrN), b = u8().slice(scr, scr + n), frames = [];
  for (let i = 0, s = -1; i < b.length; i++) { if (b[i] === 0xF0) s = i; else if (b[i] === 0xF7 && s >= 0) { frames.push(b.slice(s, i + 1)); s = -1; } }
  return frames;
}
const ask = (cmd, args = []) => {              // the payload of the reply with the same cmd (BackupConnection's)
  const f = exchange(Uint8Array.from([0xF0, 0x7D, 0x46, 0x4C, cmd, ...args, 0xF7])).find((r) => r[4] === cmd);
  return f ? Array.from(f.slice(5, -1)) : null;
};
const request = async ([cmd, args]) => { const r = ask(cmd, args); if (!r) throw new Error(`no reply to ${cmd}`); return r; };

// ------------------------------------------------------------------ the protocol ---
const info = ask(1), at = info.indexOf(0);
console.log(`INFO: "${String.fromCharCode(...info.slice(0, at))}", ${info.length} bytes`);
const dump = ask(4);
console.log(`DUMP: ${dump.length} bytes (${dump[0]} params?), first ${hex(dump.slice(0, 12))}`);
let tbk = performance.now();
const backup = await captureBackup(request, "core");
tbk = performance.now() - tbk;
const used = backup.objects.filter((o) => o.size);
console.log(`BACKUP LIST/GET: ${backup.objects.length} objects, ${used.length} nonempty (${used.map((o) => `${o.id}:${o.size}`).join(" ")}), ` +
  `${used.reduce((n, o) => n + o.size, 0)} B, CRCs checked, ${ms(tbk)}`);
report.backup = { objects: backup.objects.length, nonempty: used.map((o) => ({ id: o.id, size: o.size })), ms: +tbk.toFixed(1) };
const pq = ask(9, [2, 0]), ps = ask(9, [1, 0]), pq2 = ask(9, [2, 0]), pl = ask(9, [0, 0]);
console.log(`PROJECT: query 0 -> [${pq}], save 0 -> [${ps}], query 0 -> [${pq2}], load 0 -> [${pl}]`);
report.project = { query: pq, save: ps, queryAfter: pq2, load: pl };

// ------------------------------------------------------------------ a project, its song ---
const stored = readFileSync(join(MEL, `${SONG}.hpj`)), whole = readFileSync(join(MEL, `${SONG}.rec`));
const rec = pjExpand(new Uint8Array(stored));
const same = rec.length === whole.length && rec.every((b, i) => b === whole[i]);
console.log(`${SONG}.hpj: ${stored.length} B stored -> pjExpand ${rec.length} B, ${same ? "equals" : "DIFFERS FROM"} ${SONG}.rec`);
u8().set(rec, scr);
const lr = x.core_load_record(scr, rec.length, 3);
x.core_poll(30);
console.log(`core_load_record(project 4): ${lr}; PROJECT query 3 -> [${ask(9, [2, 3])}], BACKUP LIST size of project 4 (id 5): ${bkSize(5)} B`);
report.loadRecord = { rc: lr, storedBytes: bkSize(5) };
function bkSize(id) { const a = ask(BACKUP_CMD.LIST); const k = (a.length - 3) / 11; for (let i = 0; i < k; i++) if (a[3 + i * 11] === id) return a[4 + i * 11] | a[5 + i * 11] << 7 | a[6 + i * 11] << 14; return -1; }

// ed_flash_stop while the song plays: PROJECT save waits for the ISR's stop (fm1_wdt_feed = a millisecond of it)
let rc = x.core_song_prepare(0);
for (let i = 0; i < 44; i++) x.core_render(1000);   // ~1 s
const playing = x.core_song_state();
const st0 = Array.from(new Uint32Array(x.memory.buffer, x.core_stats(), 8));
const save = ask(9, [1, 1]);
const st1 = Array.from(new Uint32Array(x.memory.buffer, x.core_stats(), 8));
const after = x.core_song_state();
report.flashStop = { prepare: rc, stateBefore: playing, reply: save, stateAfter: after, msWaited: st1[0] - st0[0], feeds: st1[1] - st0[1] };
console.log(`ed_flash_stop: song prepare ${rc}, state ${playing.toString(16)} (playing), PROJECT save 1 while playing -> ` +
  `${save ? `[${save}]` : "NO REPLY"}, state after ${after.toString(16)}, the wait took ${st1[0] - st0[0]} device ms / ${st1[1] - st0[1]} watchdog feeds`);

// the render: SECONDS of the song from row 0
rc = x.core_song_prepare(0);
const FS = x.core_fs(), frames = SECONDS * FS, wav = new Float32Array(2 * frames);
let ended = -1, started = -1, peak = 0, sum = 0;
const tr = performance.now();
for (let f = 0; f < frames; f += 1024) {
  const k = Math.min(1024, frames - f), p = x.core_render(k);
  wav.set(new Float32Array(x.memory.buffer, p, 2 * k), 2 * f);
  const s = x.core_song_state();
  if (started < 0 && (s & 1)) started = f;
  if (started >= 0 && ended < 0 && (s & 8)) ended = f + k;
}
const tRender = performance.now() - tr;
for (const v of wav) { peak = Math.max(peak, Math.abs(v)); sum += v * v; }
const rms = Math.sqrt(sum / wav.length);
report.render = { song: SONG, seconds: SECONDS, ms: +tRender.toFixed(1), xRealtime: +(SECONDS * 1000 / tRender).toFixed(2), prepare: rc,
  startedAtFrame: started, endedAtFrame: ended, peak: +peak.toFixed(4), rmsDb: +(20 * Math.log10(rms || 1e-12)).toFixed(1),
  memoryAfterRender: x.memory.buffer.byteLength };
console.log(`render: ${SONG} ${SECONDS} s in ${ms(tRender)} = ${report.render.xRealtime}x real time; song from frame ${started}, ` +
  `${ended < 0 ? "still playing at the end" : `ended at ${(ended / FS).toFixed(2)} s`}; peak ${peak.toFixed(3)}, RMS ${report.render.rmsDb} dBFS`);
mkdirSync(OUT, { recursive: true });
writeFileSync(join(OUT, `${SONG}.wav`), wavFile(wav, FS));
writeFileSync(join(OUT, `${SONG}.f32`), Buffer.from(wav.buffer));
console.log(`wrote build/core/${SONG}.wav; memory now ${x.memory.buffer.byteLength >> 20} MiB (${x.memory.buffer.byteLength} B)`);
function wavFile(s, fs) {
  const b = Buffer.alloc(44 + s.length * 2);
  b.write("RIFF", 0); b.writeUInt32LE(36 + s.length * 2, 4); b.write("WAVEfmt ", 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(fs, 24); b.writeUInt32LE(fs * 4, 28); b.writeUInt16LE(4, 32);
  b.writeUInt16LE(16, 34); b.write("data", 36); b.writeUInt32LE(s.length * 2, 40);
  for (let i = 0; i < s.length; i++) b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(s[i] * 32767))), 44 + 2 * i);
  return b;
}

// ------------------------------------------------------------------ the same frames elsewhere ---
const req = (cmd, args = []) => [0xF0, 0x7D, 0x46, 0x4C, cmd, ...args, 0xF7];
const transcript = [req(1), req(25), req(4), req(5, [0, 0]), req(5, [0, 1]), req(10, [0]), req(34), req(33, [0]),
  req(9, [2, 0]), req(9, [1, 0]), req(9, [2, 0]), req(9, [0, 0]), req(65), req(66, [0, 0, 0, 0, 0, 0, 0, 2]),
  req(66, [1, 0, 0, 0, 0, 0, 0, 2]), req(4)];
const wire = Uint8Array.from(transcript.flat());
const fresh = await core();                     // a new instance: the same power-on as core_native's
fresh.core_boot(0); fresh.core_poll(100);
const lines = [];
{
  const s = fresh.core_scratch(), m = () => new Uint8Array(fresh.memory.buffer);
  for (const f of transcript) {
    lines.push(`> ${hex(f)}`);
    m().set(f, s); fresh.core_sysex_in(s, f.length);
    const n = fresh.core_sysex_out(s, fresh.core_scratch_size()), b = m().slice(s, s + n);
    let line = "";
    for (const v of b) { if (v === 0xF0) line = "<"; line += ` ${hex([v])}`; if (v === 0xF7) { lines.push(line); line = ""; } }
  }
}
const wasmText = lines.join("\n") + "\n";
writeFileSync(join(OUT, "transcript-wasm.txt"), wasmText);
const native = (bin, args = []) => {
  const p = join(OUT, bin);
  if (!existsSync(p)) return null;
  const r = spawnSync(p, args, { input: wire, maxBuffer: 1 << 26 });
  return r.status === 0 ? r.stdout.toString() : null;
};
const nat = native("core_native"), ed = native("editor_replay");
const pairs = (t) => { const out = []; let cur = null; for (const l of t.trim().split("\n")) { if (l[0] === ">") out.push(cur = { req: l, rep: [] }); else cur.rep.push(l); } return out; };
const name = { 1: "INFO", 25: "PING", 4: "DUMP", 5: "DESC", 10: "NAMES", 34: "UI_STATE", 33: "SONG", 9: "PROJECT", 65: "BACKUP LIST", 66: "BACKUP GET" };
if (nat !== null) {
  writeFileSync(join(OUT, "transcript-native.txt"), nat);
  report.parityNative = nat === wasmText;
  console.log(`transcript: WASM ${nat === wasmText ? "== " : "!= "}native core, ${transcript.length} requests, ${wasmText.length} chars`);
}
if (ed !== null) {
  writeFileSync(join(OUT, "transcript-editor-test.txt"), ed);
  const a = pairs(wasmText), b = pairs(ed), diff = [];
  for (let i = 0; i < a.length; i++) {
    const cmd = parseInt(a[i].req.split(" ")[5], 16), args = a[i].req.split(" ").slice(6, -1).join(" ");
    const ra = a[i].rep.join("|"), rb = (b[i]?.rep || []).join("|");
    if (ra !== rb) {
      const x1 = ra.split(" "), x2 = rb.split(" ");
      let k = 0; while (k < x1.length && x1[k] === x2[k]) k++;
      diff.push(`${name[cmd] || cmd} [${args}]: core ${x1.length - 1} bytes, editor_test ${rb ? x2.length - 1 : 0} bytes, first difference at byte ${k - 1}`);
    }
  }
  report.parityEditorTest = { same: a.length - diff.length, differ: diff };
  console.log(`transcript: WASM vs tests/editor_test.c's harness: ${a.length - diff.length} of ${a.length} replies identical`);
  for (const d of diff) console.log(`  differs: ${d}`);
}

// the render natively, sample for sample
const dir = mkdtempSync(join(tmpdir(), "core-")), recPath = join(dir, "rec.bin"), rawPath = join(dir, "out.f32");
writeFileSync(recPath, rec);
const nr = existsSync(join(OUT, "core_native")) ? spawnSync(join(OUT, "core_native"), ["render", recPath, String(SECONDS), rawPath]) : null;
if (nr && nr.status === 0) {
  const tn = performance.now();
  spawnSync(join(OUT, "core_native"), ["render", recPath, String(SECONDS), rawPath]);
  const tNative = performance.now() - tn;
  // (the WASM render above ran after other requests: render again from a fresh power-on, as core_native does)
  const y = await core(); y.core_boot(0); y.core_poll(100);
  new Uint8Array(y.memory.buffer).set(rec, y.core_scratch()); y.core_load_record(y.core_scratch(), rec.length, 0); y.core_poll(30); y.core_song_prepare(0);
  const w2 = new Float32Array(2 * frames);
  for (let f = 0; f < frames; f += 1024) { const k = Math.min(1024, frames - f); w2.set(new Float32Array(y.memory.buffer, y.core_render(k), 2 * k), 2 * f); }
  const nf = new Float32Array(readFileSync(rawPath).buffer.slice(0));
  let first = -1, maxd = 0;
  for (let i = 0; i < w2.length; i++) { const d = Math.abs(w2[i] - nf[i]); if (d > 0 && first < 0) first = i; maxd = Math.max(maxd, d); }
  report.renderParity = { nativeMs: +tNative.toFixed(1), nativeXRealtime: +(SECONDS * 1000 / tNative).toFixed(2), identical: first < 0,
    firstDiffFrame: first < 0 ? null : first >> 1, maxAbsDiff: maxd };
  console.log(`render parity: WASM ${first < 0 ? "== native, every sample" : `!= native from frame ${first >> 1} (max |d| ${maxd})`}; native ${ms(tNative)} = ${report.renderParity.nativeXRealtime}x real time`);
}
rmSync(dir, { recursive: true, force: true });
report.unhandledImportCalls = calls;
writeFileSync(join(OUT, "spike.json"), JSON.stringify(report, null, 1));
console.log(`report: build/core/spike.json${Object.keys(calls).length ? `; stubbed imports called: ${JSON.stringify(calls)}` : ""}`);
