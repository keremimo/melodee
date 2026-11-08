// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// .mel: one project and the user drum kits it plays, in one file. The device never reads this file: it keeps its
// own records (a project: project_stream.c / pattern_store.c, stored packed; a kit: drum_real.c KIT1), which this page
// takes and gives back over the backup protocol (fm1backup.js: ids 2..5 and 64..91 the projects, 92..99 the kits).
// The file's coding is here, so the firmware carries none of it.
//
// The file (little-endian, no padding):
//    0  4  "MEL\0"
//    4  2  version 2             6  2  header bytes 32          8  4  flags 0
//   12  4  the file's bytes     16  4  the sections' decoded bytes together
//   20  2  sections             22  2  0
//   24  4  CRC-32 of bytes 32..end                             28  4  CRC-32 of bytes 0..27
// then the sections, each 16 bytes and its stored bytes:
//    0  1  type: 1 the project, 2 a kit           1  1  coding: 0 as it is, 1 bytes, 2 kit (below)
//    2  2  a kit: the KIT value the project names it by (9..16); the project: 0
//    4  4  stored bytes          8  4  decoded bytes           12  4  CRC-32 of the decoded bytes
// The project first, once: its record whole (27,904 bytes, the device's stored form expanded). Then the kits the
// project names, ascending: the KIT1 record, or no bytes for a kit that was empty.
//
// Codings 1 and 2 are one binary arithmetic coder under a small context-mixing model (integer arithmetic only: every
// JavaScript engine writes and reads the same bytes). Coding 1 predicts a byte from the bytes before it (none, 1, 2,
// 3), the bytes one and two rows of 8 above with the place in the row, and the byte that followed the last four bytes
// the time before. Coding 2 is coding 1 (rows of 4) for a kit's 616-byte header and any odd bytes at its end, and for
// its slices (64 bits: 2 of mode, 4 of scale, then residuals of mode + 1 bits) the slice's own fields. A section is
// stored as it is when coding it does not make it smaller.
import { BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc, bkManifest, bkCheck } from "./fm1backup.js";

// ------------------------------------------------------------------------------------------- the coder ---
const SQ = [1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101, 1546, 2047, 2549, 2994, 3348, 3607, 3785,
  3901, 3975, 4022, 4050, 4068, 4079, 4085, 4089, 4092, 4093, 4094];
const squash = (d) => {                          // a logistic of d / 256, 12 bits
  if (d > 2047) return 4095;
  if (d < -2047) return 1;
  const w = d & 127, i = (d >> 7) + 16;
  return (SQ[i] * (128 - w) + SQ[i + 1] * w + 64) >> 7;
};
const STRETCH = new Int16Array(4096);            // its inverse
{
  let at = 0;
  for (let x = -2047; x <= 2047; x++) { const v = squash(x); for (let i = at; i <= v; i++) STRETCH[i] = x; at = v + 1; }
  for (let i = at; i < 4096; i++) STRETCH[i] = 2047;
}
const RATE = Array.from({ length: 128 }, (_, n) => Math.floor(131072 / (2 * n + 3)));   // 1 / (n + 1.5), 16 bits
const TABLE_BITS = 18;
const hash = (a, b) => (Math.imul((a + 0x9E3779B1) | 0, 0x85EBCA6B) ^ Math.imul((b + 0x7F4A7C15) | 0, 0xC2B2AE35)) >>> 0;
const slot = (h) => ((h ^ (h >>> 15)) >>> 0) & ((1 << TABLE_BITS) - 1);

// nin predictions, each a 16-bit probability with a count (it moves by 1 / (count + 1.5)), mixed in the logistic
// domain by one of nsel weight sets
class Mix {
  constructor(nin, nsel, weight) {
    const size = 1 << TABLE_BITS;
    this.nin = nin; this.size = size;
    this.p = new Uint16Array(size * nin).fill(32768);
    this.n = new Uint8Array(size * nin);
    this.w = new Int32Array(nin * nsel).fill(weight);
    this.at = new Int32Array(nin); this.st = new Int32Array(nin);
    this.set = 0; this.pr = 2048;
  }
  predict(sel) {                                 // this.at[]: each input's place in its table -> P(1), 12 bits
    const o = sel * this.nin;
    let dot = 0;
    for (let i = 0; i < this.nin; i++) {
      const s = STRETCH[this.p[i * this.size + this.at[i]] >> 4];
      this.st[i] = s; dot += this.w[o + i] * s;
    }
    this.set = o;
    return this.pr = squash(Math.floor(dot / 65536));
  }
  update(bit) {
    const err = (bit << 12) - this.pr;
    for (let i = 0; i < this.nin; i++) {
      this.w[this.set + i] += (this.st[i] * err + 512) >> 10;
      const k = i * this.size + this.at[i], n = this.n[k], p = this.p[k];
      this.p[k] = bit ? p + (((65535 - p) * RATE[n] + 65535) >>> 16) : p - ((p * RATE[n] + 65535) >>> 16);
      if (n < 127) this.n[k] = n + 1;
    }
  }
}
// coding 1's model: the byte at i of d, the bytes before it known
class ByteModel {
  constructor(row) {
    this.row = row; this.mix = new Mix(7, 768, 14000);
    this.seen = new Int32Array(1 << 16).fill(-1); this.match = -1; this.run = 0;
    this.h = new Uint32Array(6); this.expect = -1;
  }
  begin(d, i) {
    const g = (k) => (i >= k ? d[i - k] : 0), b1 = g(1), b2 = g(2), b3 = g(3), b4 = g(4), r = this.row, h = this.h;
    h[0] = hash(1, 0); h[1] = hash(2, b1); h[2] = hash(3, b1 | b2 << 8); h[3] = hash(4, b1 | b2 << 8 | b3 << 16);
    h[4] = hash(5, g(r) | (i % r) << 8); h[5] = hash(6, g(r) | g(2 * r) << 8 | (i % r) << 16);
    if (this.run && d[this.match] === b1) { this.run++; this.match++; } else this.run = 0;
    if (i >= 4) {
      const k = slot(hash(7, (b1 | b2 << 8 | b3 << 16 | b4 << 24) >>> 0)) & 65535;
      if (!this.run && this.seen[k] >= 0) { this.match = this.seen[k]; this.run = 1; }
      this.seen[k] = i;
    }
    this.expect = this.run ? d[this.match] : -1;
  }
  predict(node) {                                // node: the byte's bits so far under a leading 1 (1..255)
    const m = this.mix;
    for (let j = 0; j < 6; j++) m.at[j] = slot(hash(this.h[j], node));
    let mc = 0;
    if (this.expect >= 0) {
      const nb = 31 - Math.clz32(node);
      if (((this.expect | 256) >> (8 - nb)) === node) mc = 1 + (this.expect >> (7 - nb) & 1) + 2 * Math.min(this.run, 31);
    }
    m.at[6] = mc;
    return m.predict(node + 256 * (mc ? 2 - (mc & 1) : 0));
  }
}
// coding 2's model for a kit's slices: bit k of a slice whose bits before it are v (the field's own, since its start)
const SLICE_N = [58, 29, 19, 14];                // (drum_real.c DRK_NS)
class SliceModel {
  constructor() { this.mix = new Mix(3, 64, 22000); this.mode = 0; this.scale = 0; this.lastMode = 0; this.lastScale = 0; this.prev = 0; }
  predict(k, v) {
    const m = this.mix;
    if (k < 6) {                                 // mode and scale: after the slice before
      m.at[0] = slot(hash(10, v | k << 8 | this.lastMode << 12));
      m.at[1] = slot(hash(11, v | k << 8 | this.lastMode << 12 | this.lastScale << 16));
      m.at[2] = slot(hash(12, v | k << 8 | this.lastScale << 16));
      return m.predict(k);
    }
    const bits = this.mode + 1, r = k - 6;
    if (r >= SLICE_N[this.mode] * bits) { m.at[0] = m.at[1] = m.at[2] = slot(hash(13, 0)); return m.predict(63); }   // spare bits
    const j = r % bits, part = 1 << j | (v & ((1 << j) - 1));
    m.at[0] = slot(hash(14, part | this.mode << 8));
    m.at[1] = slot(hash(15, part | this.mode << 8 | this.scale << 12));
    m.at[2] = slot(hash(16, part | this.mode << 8 | this.prev << 12));
    return m.predict(8 + this.mode * 4 + j);
  }
  update(k, v, bit) {                            // -> v for the next bit
    this.mix.update(bit);
    v = (v << 1 | bit) & 0xffff;
    if (k === 1) this.mode = v & 3;
    if (k === 5) { this.scale = v & 15; this.prev = 0; return 0; }
    if (k >= 6) {
      const bits = this.mode + 1, r = k - 6;
      if (r < SLICE_N[this.mode] * bits && r % bits === bits - 1) { this.prev = v & ((1 << bits) - 1); v = 0; }
    }
    if (k === 63) { this.lastMode = this.mode; this.lastScale = this.scale; }
    return v;
  }
}
class Encoder {
  constructor() { this.x1 = 0; this.x2 = 0xffffffff; this.out = []; }
  code(bit, p) {
    const mid = this.x1 + Math.floor((this.x2 - this.x1) / 4096) * p;
    if (bit) this.x2 = mid; else this.x1 = mid + 1;
    while (((this.x1 ^ this.x2) & 0xff000000) === 0) {
      this.out.push(this.x2 >>> 24);
      this.x1 = (this.x1 << 8) >>> 0; this.x2 = ((this.x2 << 8) | 255) >>> 0;
    }
    return bit;
  }
  end() { this.out.push(this.x1 >>> 24, 255, 255, 255); return Uint8Array.from(this.out); }
}
class Decoder {
  constructor(src) {
    this.x1 = 0; this.x2 = 0xffffffff; this.src = src; this.at = 0; this.x = 0;
    for (let i = 0; i < 4; i++) this.x = ((this.x << 8) | this.next()) >>> 0;
  }
  next() { if (this.at >= this.src.length) throw new Error("Damaged project file"); return this.src[this.at++]; }
  code(_, p) {
    const mid = this.x1 + Math.floor((this.x2 - this.x1) / 4096) * p, bit = this.x <= mid ? 1 : 0;
    if (bit) this.x2 = mid; else this.x1 = mid + 1;
    while (((this.x1 ^ this.x2) & 0xff000000) === 0) {
      this.x1 = (this.x1 << 8) >>> 0; this.x2 = ((this.x2 << 8) | 255) >>> 0;
      this.x = ((this.x << 8) | this.next()) >>> 0;
    }
    return bit;
  }
  end() { if (this.at !== this.src.length) throw new Error("Damaged project file"); }
}
const KIT_HEAD = 616;                            // (drum_real.c drk_kit_t)
// d (encoding: the bytes; decoding: its length, filled as it goes) through c
function cmRun(c, d, kit, encoding) {
  const bytes = new ByteModel(kit ? 4 : 8);
  const byteAt = (i) => {
    bytes.begin(d, i);
    let node = 1;
    for (let k = 7; k >= 0; k--) { const bit = c.code(encoding ? d[i] >> k & 1 : 0, bytes.predict(node)); bytes.mix.update(bit); node = node << 1 | bit; }
    d[i] = node & 255;
  };
  const head = kit ? Math.min(d.length, KIT_HEAD) : d.length, slices = kit ? (d.length - head) >> 3 : 0;
  for (let i = 0; i < head; i++) byteAt(i);
  if (slices) {
    const m = new SliceModel(), view = new DataView(d.buffer, d.byteOffset, d.byteLength);
    for (let s = 0; s < slices; s++) {           // two little-endian words, the high one first
      const at = head + s * 8, w = encoding ? [view.getUint32(at, true), view.getUint32(at + 4, true)] : [0, 0];
      for (let k = 0, v = 0; k < 64; k++) {
        const bit = c.code(w[k >> 5] >>> (31 - (k & 31)) & 1, m.predict(k, v));
        v = m.update(k, v, bit);
        if (!encoding) w[k >> 5] = (w[k >> 5] << 1 | bit) >>> 0;
      }
      if (!encoding) { view.setUint32(at, w[0], true); view.setUint32(at + 4, w[1], true); }
    }
  }
  for (let i = head + slices * 8; i < d.length; i++) byteAt(i);
}
export function cmEncode(d, kit = false) { const c = new Encoder(); cmRun(c, d, kit, true); return c.end(); }
export function cmDecode(src, n, kit = false) { const c = new Decoder(src), d = new Uint8Array(n); cmRun(c, d, kit, false); c.end(); return d; }

// ---------------------------------------------------------------------------------- the device's records ---
// A project record (pattern_store.c): 8 bytes (an FBK magic, its 27,904 bytes), the FUN record (project.c
// proj_pack), the bank tail, FNV-1a of all before it last.
export const REC_BYTES = 27904, KIT_MAX = 28392;
export const KIT_FIRST = 9, KIT_COUNT = 8;       // KIT 1..8 are the drum engine's E0 values 9..16 (eng_drum.c DK_USER0)
const FUN_AT = 8, FUN_BYTES = 13212, TAIL_AT = FUN_AT + FUN_BYTES;
const FUN_MAGIC = 0x46554E49, FBKP = 0x504B4246, FBKQ = 0x514B4246, FBKR = 0x524B4246, KIT_MAGIC = 0x3154494B, PJ_MAGIC = 0x314A5048, PZ_MAGIC = 0x324A5048, PZ_RAW = 0x80000000;
const NPART = 4, NTRK = 8, NPAT = 16, NSTEP = 64, MOTION_MAX = 64, CHAIN_ROWS = 64, CHAIN_OLD_ROWS = 16, CHAIN_OLD_BYTES = 36, FM6_NFN = 16;
const ENGI_DRUM = 10, NAME_LEN = 12;
// the kits' synth by its UID (firmware/modules/shipped.h: DRUM, "melodee/DRUM"; the engine map's default number 10)
const KIT_OWNER_UID = 0x86C271CF, EMX_TAG = 0x31584D45, EMX_TRAIL = 12, EMAP_ENTRY = 24;
// FBKR (pattern_store.c): FBKP / FBKQ plus an end block before the sum, the engine map's entries for the numbers the
// project names that are not Melodee's defaults (eng_map.c), the patches of parts whose synth has no 1.x format
export function recMap(rec) {
  const t = REC_BYTES - 4 - EMX_TRAIL, plen = rec[t] | rec[t + 1] << 8, n = rec[t + 2];
  if (u32(rec, 0) !== FBKR || u32(rec, t + 8) !== EMX_TAG || n * EMAP_ENTRY > plen || rec[t + 4] > 1)
    return { entries: [], patterns: u32(rec, 0) === FBKQ, end: REC_BYTES - 4 };
  const entries = [];
  for (let k = 0, at = t - plen; k < n; k++, at += EMAP_ENTRY)
    entries.push({ number: rec[at], uid: u32(rec, at + 4), name: String.fromCharCode(...rec.subarray(at + 8, at + 24)).replace(/\0+$/, "") });
  return { entries, patterns: rec[t + 4] === 1, end: t - plen };
}
// the engine numbers a record's kits are played by: DRUM's number there (its entries'; none: the default 10)
function kitEngines(map) {
  const out = new Set([ENGI_DRUM]);
  for (const e of map.entries) {
    if (e.uid === KIT_OWNER_UID) out.add(e.number);
    else out.delete(e.number);
  }
  return out;
}
// where things are in a record whose FUN record keeps np parameters a track (its byte 66; E0 is np - 8: common
// parameters are only ever added before it). tests/mel_fixture.c prints the firmware's own for test_mel.mjs
export function recLayout(np) {
  const track = np + 2 + NSTEP * 8, motion = FUN_AT + 68 + NPART * track + CHAIN_OLD_BYTES;
  const tags = TAIL_AT + NTRK + NTRK * NPAT * 4 + CHAIN_OLD_ROWS * NTRK;               // FBKP's
  const song = tags + MOTION_MAX + 4 + NPART * FM6_NFN + Math.ceil(NTRK * NPAT * 11 / 8) + (NTRK - NPART) * (np + 1);
  return { np, e0: np - 8, track, tracks: FUN_AT + 68, motion, funSum: FUN_AT + FUN_BYTES - 4, name: FUN_AT + FUN_BYTES - 4 - NAME_LEN,
    tags, song, sounds: song + 4 + CHAIN_ROWS * (NTRK + 1) + CHAIN_ROWS * 4 * 3, soundHead: 4 + CHAIN_ROWS * NTRK, soundBytes: np + 22 + 156 };
}
export function fnv(bytes, from, to) {
  let h = 2166136261;
  for (let i = from; i < to; i++) h = Math.imul(h ^ bytes[i], 16777619);
  return h >>> 0;
}
const u32 = (b, at) => (b[at] | b[at + 1] << 8 | b[at + 2] << 16 | b[at + 3] << 24) >>> 0;
const put32 = (b, at, v) => { b[at] = v; b[at + 1] = v >>> 8; b[at + 2] = v >>> 16; b[at + 3] = v >>> 24; };
const isKit = (v) => v >= KIT_FIRST && v < KIT_FIRST + KIT_COUNT;

// The device's packing of a block (dsp.c lz_unpack's format; project_stream.c packs with it): c < 0x80: c + 1 bytes
// follow as they are; else (c & 0x7F) + 3 bytes copied from d + 1 back, d in the next two bytes
export function lzUnpack(src, n) {               // -> the block's n bytes; anything that does not add up throws
  const out = new Uint8Array(n);
  let at = 0, k = 0;
  while (k < n) {
    if (at >= src.length) throw new Error("Damaged project record");
    let c = src[at++], d = 0;
    if (c < 0x80) c++;
    else { if (at + 2 > src.length) throw new Error("Damaged project record"); d = (src[at] | src[at + 1] << 8) + 1; at += 2; c = (c & 0x7F) + 3; }
    if (d > k || c > n - k || (!d && at + c > src.length)) throw new Error("Damaged project record");
    for (; c; c--, k++) out[k] = d ? out[k - d] : src[at++];
  }
  if (at !== src.length) throw new Error("Damaged project record");
  return out;
}
export function lzPack(d) {                      // the longest run found among the last places its three bytes began,
  const n = d.length, out = [], head = new Int32Array(1 << 15).fill(-1), prev = new Int32Array(n);   // unless a longer one begins at the next byte
  const hash = (i) => Math.imul(d[i] | d[i + 1] << 8 | d[i + 2] << 16, 0x9E3779B1) >>> 17;
  const note = (i) => { if (i + 3 <= n) { const h = hash(i); prev[i] = head[h]; head[h] = i; } };
  const find = (i) => {
    let best = 0, from = 0;
    if (i + 3 <= n) for (let c = head[hash(i)], tries = 256; c >= 0 && tries && i - c <= 65536 && best < 130; c = prev[c], tries--) {
      let len = 0;
      while (len < 130 && i + len < n && d[c + len] === d[i + len]) len++;
      if (len > best) { best = len; from = c; }
    }
    return [best, from];
  };
  const lits = (end, lit) => { for (; lit; lit -= Math.min(lit, 128)) out.push(Math.min(lit, 128) - 1, ...d.subarray(end - lit, end - lit + Math.min(lit, 128))); };
  let i = 0, lit = 0;
  while (i < n) {
    let [len, from] = find(i);
    note(i);
    if (len >= 3 && find(i + 1)[0] > len) len = 0;
    if (len >= 3) {
      lits(i, lit); lit = 0;
      out.push(0x80 | (len - 3), (i - from - 1) & 255, (i - from - 1) >> 8);
      for (len--, i++; len; len--, i++) note(i);
    } else { lit++; i++; }
  }
  lits(i, lit);
  return Uint8Array.from(out);
}
// A project as the device stores it. Packed (project_stream.c HPJ2, what it writes): a 32-byte head (magic, 27,904,
// the record's first word, its last (the sum), the two blocks' stored bytes, | PZ_RAW: as it is), the bank tail
// without its sum, then the FUN record, each packed as above. Compact (HPJ1, what firmware before that wrote and every
// firmware reads): a 32-byte head (magic, 27,904, four map words), then the tail's 256-byte pages that are not all zero
// (its last always), then the first 13,220 bytes' (map bits 64..). Raw: the record itself
const T_PAGES = Math.ceil((REC_BYTES - TAIL_AT) / 256), S_PAGES = Math.ceil(TAIL_AT / 256);
const regions = [[TAIL_AT, REC_BYTES, 0, T_PAGES], [0, TAIL_AT, 64, S_PAGES]];
const blocks = [[TAIL_AT, REC_BYTES - 4], [FUN_AT, TAIL_AT]];
export function pjPacked(rec) {
  if (rec.length !== REC_BYTES) throw new Error("Not a project record");
  const out = new Uint8Array(32 + REC_BYTES);
  let at = 32;
  put32(out, 0, PZ_MAGIC); put32(out, 4, REC_BYTES); out.set(rec.subarray(0, 4), 8); out.set(rec.subarray(REC_BYTES - 4), 12);
  blocks.forEach(([from, to], b) => {
    const packed = lzPack(rec.subarray(from, to)), raw = packed.length >= to - from, bytes = raw ? rec.subarray(from, to) : packed;
    put32(out, 16 + 4 * b, raw ? (bytes.length + PZ_RAW) >>> 0 : bytes.length);
    out.set(bytes, at); at += bytes.length;
  });
  return out.slice(0, at);
}
export function pjCompact(rec) {
  if (rec.length !== REC_BYTES) throw new Error("Not a project record");
  const out = new Uint8Array(32 + REC_BYTES), map = [0, 0, 0, 0];
  let at = 32;
  for (const [from, to, bit, pages] of regions) for (let p = 0; p < pages; p++) {
    const page = rec.subarray(from + p * 256, Math.min(to, from + (p + 1) * 256));
    if (!(bit === 0 && p === pages - 1) && page.every((v) => !v)) continue;
    map[(bit + p) >> 5] |= 1 << ((bit + p) & 31);
    out.set(page, at); at += page.length;
  }
  put32(out, 0, PJ_MAGIC); put32(out, 4, REC_BYTES);
  map.forEach((m, i) => put32(out, 8 + i * 4, m >>> 0));
  return out.slice(0, at);
}
export function pjExpand(stored) {               // a stored project object -> its record
  if (stored.length === REC_BYTES && u32(stored, 4) === REC_BYTES) return Uint8Array.from(stored);
  const magic = stored.length > 32 ? u32(stored, 0) : 0, rec = new Uint8Array(REC_BYTES);
  if ((magic !== PJ_MAGIC && magic !== PZ_MAGIC) || u32(stored, 4) !== REC_BYTES)
    throw new Error("This project is in an older format. Load it on the device and save it again first.");
  let at = 32;
  if (magic === PZ_MAGIC) {
    rec.set(stored.subarray(8, 12), 0); put32(rec, 4, REC_BYTES); rec.set(stored.subarray(12, 16), REC_BYTES - 4);
    blocks.forEach(([from, to], b) => {
      const field = u32(stored, 16 + 4 * b), raw = field >= PZ_RAW, n = field % PZ_RAW;
      if (n > stored.length - at || (raw && n !== to - from)) throw new Error("Damaged project record");
      rec.set(raw ? stored.subarray(at, at + n) : lzUnpack(stored.subarray(at, at + n), to - from), from); at += n;
    });
    if (at !== stored.length) throw new Error("Damaged project record");
    return rec;
  }
  for (const [from, to, bit, pages] of regions) for (let p = 0; p < pages; p++) {
    const here = u32(stored, 8 + ((bit + p) >> 5) * 4) >>> ((bit + p) & 31) & 1, n = Math.min(to, from + (p + 1) * 256) - (from + p * 256);
    if (!here) continue;
    if (at + n > stored.length) throw new Error("Damaged project record");
    rec.set(stored.subarray(at, at + n), from + p * 256); at += n;
  }
  if (at !== stored.length || !(u32(stored, 8 + ((T_PAGES - 1) >> 5) * 4) >>> ((T_PAGES - 1) & 31) & 1)) throw new Error("Damaged project record");
  return rec;
}
// A record's parts as this page knows them, its sums checked. kits: the KIT values (9..16) it names, ascending
export function recInfo(rec) {
  const magic = rec.length === REC_BYTES ? u32(rec, 0) : 0, np = rec[FUN_AT + 66], L = recLayout(np);
  if ((magic & 0xFFFFFF) !== 0x4B4246 || magic >>> 24 < 0x4B || magic >>> 24 > 0x52 || u32(rec, 4) !== REC_BYTES ||   /* FBKK..FBKR */
      u32(rec, REC_BYTES - 4) !== fnv(rec, 0, REC_BYTES - 4))
    throw new Error("Not a Melodee project record");
  if (u32(rec, FUN_AT) !== FUN_MAGIC || u32(rec, FUN_AT + 4) !== FUN_BYTES || np < 8 || L.motion + 4 + MOTION_MAX * 4 > L.name ||
      u32(rec, L.funSum) !== fnv(rec, FUN_AT, L.funSum))
    throw new Error("This project is in an older format. Load it on the device and save it again first.");
  const refs = [];                               // [byte offset, 1: a parameter byte (value + 64) | 2: an int16]
  // (FBKK and FBKP / FBKQ are what firmware with user kits writes; the forms between: the FUN record's places only)
  const map = recMap(rec), kitEng = kitEngines(map), patterns = map.patterns;
  const wide = magic === FBKP || magic === FBKQ || magic === FBKR, sounds = [], patternSound = new Uint8Array(NTRK * NPAT);
  if (wide && rec[L.song + 2]) {
    const count = rec[L.sounds], size = rec[L.sounds + 2] | rec[L.sounds + 3] << 8;
    if (!count || count > 64 || rec[L.sounds + 1] !== np || size !== L.soundBytes || L.sounds + L.soundHead + count * size + (patterns ? NTRK * NPAT : 0) > map.end)
      throw new Error("Damaged project record");
    for (let j = 0; j < count; j++) sounds.push(L.sounds + L.soundHead + j * size);
    if (patterns) patternSound.set(rec.subarray(L.sounds + L.soundHead + count * size, L.sounds + L.soundHead + count * size + NTRK * NPAT));
  }
  const drum = [];
  for (let k = 0; k < NPART; k++) {              // a drum track's KIT
    const t = L.tracks + k * L.track;
    drum.push(kitEng.has(rec[t + np]));
    if (drum[k] && isKit(rec[t + L.e0] - 64)) refs.push([t + L.e0, 1]);
  }
  for (const s of sounds)                        // a song's or a pattern's own sound: a drum sound's KIT
    if (kitEng.has(rec[s + np]) && isKit(rec[s + L.e0] - 64)) refs.push([s + L.e0, 1]);
  const count = rec[L.motion];
  if (count > MOTION_MAX) throw new Error("Damaged project record");
  for (let i = 0; i < count; i++) {              // automation of E0 where the pattern plays drums (its own sound's
    const e = L.motion + 4 + i * 4, k = rec[e] >> 6, v = (rec[e + 2] | rec[e + 3] << 8) << 16 >> 16;   // engine, else the track's)
    if (rec[e + 1] !== L.e0 || !isKit(v)) continue;
    const id = patterns ? patternSound[k * NPAT + (rec[L.tags + i] & 15)] : 0;
    if (id > sounds.length) throw new Error("Damaged project record");
    if (id ? kitEng.has(rec[sounds[id - 1] + np]) : drum[k]) refs.push([e + 2, 2]);
  }
  const value = ([at, kind]) => (kind === 1 ? rec[at] - 64 : rec[at]);
  let name = "";
  for (let i = 0; i < NAME_LEN && rec[L.name + i]; i++) name += String.fromCharCode(rec[L.name + i]);
  if (!/^[ -~]*$/.test(name)) name = "";
  return { magic, np, name, refs, kits: [...new Set(refs.map(value))].sort((a, b) => a - b), layout: L, map: map.entries };
}
// rec with every KIT it names changed by map (a Map, old -> new: all at once), its sums made again
export function recRemap(rec, map) {
  const out = Uint8Array.from(rec), { refs, layout: L } = recInfo(rec);
  for (const [at, kind] of refs) {
    const old = kind === 1 ? rec[at] - 64 : rec[at], to = map.has(old) ? map.get(old) : old;
    if (!isKit(to)) throw new Error("Invalid kit number");
    out[at] = kind === 1 ? to + 64 : to;
  }
  put32(out, L.funSum, fnv(out, FUN_AT, L.funSum));
  put32(out, REC_BYTES - 4, fnv(out, 0, REC_BYTES - 4));
  return out;
}
export function kitCheck(bytes) {                // what any reader can tell of a kit record (the device checks all of it)
  if (bytes.length < KIT_HEAD || bytes.length > KIT_MAX || bytes.length % 8 || u32(bytes, 0) !== KIT_MAGIC || (bytes[4] | bytes[5] << 8) !== KIT_HEAD)
    throw new Error("Invalid drum kit in the project file");
}

// ------------------------------------------------------------------------------------------- the file ---
const MEL_HEAD = 32, SEC_HEAD = 16, T_PROJECT = 1, T_KIT = 2;
export const MEL_CONST = { REC_BYTES, KIT_MAX, KIT_FIRST, KIT_COUNT, KIT_HEAD, ENGI_DRUM, FUN_BYTES, TAIL_AT, FUN_MAGIC, FBKP, FBKQ, FBKR, KIT_MAGIC, PJ_MAGIC, PZ_MAGIC, KIT_OWNER_UID };   // (test_mel.mjs: the firmware's)
export const MEL_MAX = MEL_HEAD + (1 + KIT_COUNT) * SEC_HEAD + REC_BYTES + KIT_COUNT * KIT_MAX;
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
// { project: a record, kits: [{ kit: 9..16, bytes: a KIT1 record or none }] } -> the file
export function melPack({ project, kits = [] }) {
  const info = recInfo(project), list = [...kits].sort((a, b) => a.kit - b.kit);
  if (!same(list.map((k) => k.kit), info.kits)) throw new Error("The file's kits must be the ones the project names");
  const sections = [{ type: T_PROJECT, arg: 0, bytes: project, kit: false }];
  for (const k of list) { if (k.bytes.length) kitCheck(k.bytes); sections.push({ type: T_KIT, arg: k.kit, bytes: k.bytes, kit: true }); }
  let total = MEL_HEAD, decoded = 0;
  for (const s of sections) {
    const coded = s.bytes.length ? cmEncode(s.bytes, s.kit) : s.bytes;
    s.coding = coded.length < s.bytes.length ? (s.kit ? 2 : 1) : 0;
    s.stored = s.coding ? coded : s.bytes;
    total += SEC_HEAD + s.stored.length; decoded += s.bytes.length;
  }
  const out = new Uint8Array(total);
  out.set([0x4D, 0x45, 0x4C, 0, 2, 0, MEL_HEAD, 0]);
  put32(out, 12, total); put32(out, 16, decoded); out[20] = sections.length;
  let at = MEL_HEAD;
  for (const s of sections) {
    out[at] = s.type; out[at + 1] = s.coding; out[at + 2] = s.arg;
    put32(out, at + 4, s.stored.length); put32(out, at + 8, s.bytes.length); put32(out, at + 12, bkCrc(s.bytes));
    out.set(s.stored, at + SEC_HEAD); at += SEC_HEAD + s.stored.length;
  }
  put32(out, 24, bkCrc(out.subarray(MEL_HEAD))); put32(out, 28, bkCrc(out.subarray(0, 28)));
  const back = melUnpack(out);                   // what was written reads back as what was given
  if (!same(back.project, project) || back.kits.some((k, i) => !same(k.bytes, list[i].bytes))) throw new Error("Project file check failed");
  return out;
}
// the file -> { project, kits, name }: every length, sum and record checked; anything else throws
export function melUnpack(file) {
  const bad = (why) => { throw new Error(`Not a Melodee project file (${why})`); };
  if (!(file instanceof Uint8Array)) file = new Uint8Array(file);
  if (file.length < MEL_HEAD + SEC_HEAD || u32(file, 0) !== 0x004C454D) bad("no MEL header");
  if ((file[4] | file[5] << 8) !== 2) throw new Error("This project file needs a newer editor");
  if ((file[6] | file[7] << 8) !== MEL_HEAD || u32(file, 8) || file[22] || file[23]) bad("header");
  if (u32(file, 28) !== bkCrc(file.subarray(0, 28))) bad("header checksum");
  if (u32(file, 12) !== file.length || file.length > MEL_MAX) bad("length");
  if (u32(file, 24) !== bkCrc(file.subarray(MEL_HEAD))) bad("checksum");
  const count = file[20] | file[21] << 8;
  if (!count || count > 1 + KIT_COUNT) bad("sections");
  let at = MEL_HEAD, decoded = 0, project = null;
  const kits = [];
  for (let i = 0; i < count; i++) {
    if (at + SEC_HEAD > file.length) bad("section");
    const type = file[at], coding = file[at + 1], arg = file[at + 2] | file[at + 3] << 8;
    const stored = u32(file, at + 4), n = u32(file, at + 8), crc = u32(file, at + 12);
    const kit = type === T_KIT;
    if (type !== (i ? T_KIT : T_PROJECT) || coding > (kit ? 2 : 1) || (kit && coding === 1) || stored > file.length - at - SEC_HEAD ||
        (kit ? !isKit(arg) || n > KIT_MAX || (kits.length && arg <= kits[kits.length - 1].kit) : arg || n !== REC_BYTES) ||
        (coding ? stored >= n : stored !== n)) bad("section");
    const src = file.subarray(at + SEC_HEAD, at + SEC_HEAD + stored);
    let bytes;
    try { bytes = coding ? cmDecode(src, n, kit) : Uint8Array.from(src); } catch { bad("data"); }
    if (bkCrc(bytes) !== crc) bad("data checksum");
    if (kit) { if (n) kitCheck(bytes); kits.push({ kit: arg, bytes }); } else project = bytes;
    at += SEC_HEAD + stored; decoded += n;
  }
  if (at !== file.length || decoded !== u32(file, 16)) bad("length");
  const info = recInfo(project);
  if (!same(kits.map((k) => k.kit), info.kits)) bad("kits");
  return { project, kits, name: info.name };
}

// ------------------------------------------------------------------------------------------ the device ---
const KIT_ID = 92, projectIds = [2, 3, 4, 5, ...Array.from({ length: 28 }, (_, i) => 64 + i)];
export const melProjectId = (slot) => projectIds[slot];          // project 1..32 as slot 0..31 -> its object
const ask = (request, r, o = {}) => request(r, { timeout: 4000, retries: 0, ...o });
async function getObject(request, o, onProgress = () => {}) {
  const bytes = new Uint8Array(o.size);
  for (let off = 0; off < o.size; off += 256) {
    const n = Math.min(256, o.size - off);
    const a = await request([BACKUP_CMD.GET, [o.id, ...bkU32(off), n & 127, n >>> 7]], { timeout: 1000, retries: 1 });
    bkCheck(a[1]);
    if (a[0] !== o.id || bkR32(a, 2) !== off || (a[7] | a[8] << 7) !== n) throw new Error("Unexpected reply");
    bytes.set(bkUnpack(a.slice(9), n), off); onProgress(n);
  }
  if (bkCrc(bytes) !== o.crc) throw new Error("The device changed meanwhile. Try again while stopped.");
  return bytes;
}
async function putObject(request, id, bytes, onProgress = () => {}) {
  const put = async (args) => {
    const a = await ask(request, [BACKUP_CMD.PUT, args]);
    try { bkCheck(a[2]); } catch (e) { e.rc = a[2]; throw e; }
  };
  await put([0, id, ...bkU32(bytes.length), ...bkU32(bytes.length ? bkCrc(bytes) : 0)]);
  try {
    for (let off = 0; off < bytes.length; off += 256) {
      const chunk = bytes.subarray(off, off + 256);
      await put([1, id, ...bkU32(off), ...bkPack(chunk)]); onProgress(chunk.length);
    }
    await put([2, id]);
  } catch (e) { await ask(request, [BACKUP_CMD.PUT, [3, id]]).catch(() => {}); throw e; }
}
const list = async (request) => bkManifest(await ask(request, [BACKUP_CMD.LIST, []], { timeout: 3000 }));
// The device's project in object id (0: the music as it is now) and its kits -> { file, name }
export async function melExport(request, id, onProgress = () => {}) {
  const objects = await list(request), o = objects.find((x) => x.id === id);
  if (!o || !o.size) throw new Error("That project is empty");
  let done = 0, total = o.size;
  const step = (n) => onProgress(done += n, total);
  const project = pjExpand(await getObject(request, o, step)), info = recInfo(project), kits = [];
  const sources = info.kits.map((kit) => objects.find((x) => x.id === KIT_ID + kit - KIT_FIRST));
  total += sources.reduce((n, k) => n + (k ? k.size : 0), 0);
  for (const [i, kit] of info.kits.entries())
    kits.push({ kit, bytes: sources[i] && sources[i].size ? await getObject(request, sources[i], step) : new Uint8Array(0) });
  return { file: melPack({ project, kits }), name: info.name, kits: kits.filter((k) => k.bytes.length).map((k) => k.kit) };
}
// A project file onto the device: its kits into slots that hold the same bytes already or are free, the project,
// naming the kits where they went, into the first free project. Nothing on the device is replaced.
// -> { slot (0..31), name, kits: [{ from, to, reused }], empty: [KIT values the file's project names without a kit] }
export async function melImport(request, file, onProgress = () => {}) {
  const mel = melUnpack(file), objects = await list(request);
  if (!objects.some((x) => x.id === 64)) throw new Error("This firmware cannot take a project file. Update the firmware first.");
  const target = projectIds.map((id) => objects.find((x) => x.id === id)).findIndex((o) => o && !o.size);
  if (target < 0) throw new Error("No free project. Delete a project on the device, then open the file again.");
  const slots = Array.from({ length: KIT_COUNT }, (_, i) => objects.find((x) => x.id === KIT_ID + i));
  const map = new Map(), kits = [], taken = new Set(), writes = [];
  const real = mel.kits.filter((k) => k.bytes.length), empty = mel.kits.filter((k) => !k.bytes.length).map((k) => k.kit);
  if (real.length && !slots[0]) throw new Error("This firmware has no user drum kits. Update the firmware first.");
  const first = (k) => real.find((x) => same(x.bytes, k.bytes));   // (a kit the file holds twice goes in once)
  for (const k of real) if (first(k) === k) {    // the same kit already there (every byte): that slot
    const crc = bkCrc(k.bytes);
    for (let i = 0; i < KIT_COUNT && !map.has(k.kit); i++)
      if (!taken.has(i) && slots[i].size === k.bytes.length && slots[i].crc === crc && same(await getObject(request, slots[i]), k.bytes)) {
        map.set(k.kit, KIT_FIRST + i); taken.add(i); kits.push({ from: k.kit, to: KIT_FIRST + i, reused: true });
      }
  }
  for (const k of real) if (first(k) === k && !map.has(k.kit)) {   // else a free one
    const i = slots.findIndex((s, j) => !s.size && !taken.has(j));
    if (i < 0) {
      const need = real.filter((x) => first(x) === x && !map.has(x.kit)).length + writes.length, free = slots.filter((s) => !s.size).length;
      throw new Error(`No free drum kit: the project brings ${need} new kit${need > 1 ? "s" : ""}, the device has room for ${free}. Delete a kit on the device (Record kit > Delete kit), then open the file again.`);
    }
    map.set(k.kit, KIT_FIRST + i); taken.add(i); kits.push({ from: k.kit, to: KIT_FIRST + i, reused: false }); writes.push([i, k.bytes]);
  }
  for (const k of real) if (first(k) !== k) { map.set(k.kit, map.get(first(k).kit)); kits.push({ from: k.kit, to: map.get(k.kit), reused: true }); }
  for (const kit of empty) {                     // a kit the project names that was empty: an empty slot again, if one is left
    const i = slots[0] ? slots.findIndex((s, j) => !s.size && !taken.has(j)) : -1;
    if (i >= 0) { map.set(kit, KIT_FIRST + i); taken.add(i); }
  }
  const record = recRemap(mel.project, map), project = pjPacked(record), kitBytes = writes.reduce((n, w) => n + w[1].length, 0);
  let done = 0, total = kitBytes + project.length;
  const step = (n) => onProgress(done += n, total), written = [];
  try {
    for (const [i, bytes] of writes) { await putObject(request, KIT_ID + i, bytes, step); written.push(i); }
    try { await putObject(request, projectIds[target], project, step); }
    catch (e) {                                  // firmware before the packed form reads the compact one
      const compact = pjCompact(record);
      if (e.rc !== 2) throw e;
      done = kitBytes; total = kitBytes + compact.length;
      await putObject(request, projectIds[target], compact, step);
    }
  } catch (e) {                                  // the kits this file brought go again: nothing is left half there
    for (const i of written) await putObject(request, KIT_ID + i, new Uint8Array(0)).catch(() => {});
    throw e;
  }
  return { slot: target, name: mel.name, kits, empty };
}
