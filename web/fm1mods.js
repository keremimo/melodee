// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The synth modules a build ships (SID, DRUM, CZ-1, FM6, the Prophet: ABI 3, firmware/modules/sdk/mod_abi3.h), written
// after an update through editor command 84 into the device's module slots: pool records, taken first come first served
// like projects and preset banks. The file is build/melodee-mods.bin: the build's .mmod files one after another (each
// head gives its length). A module of host 1..3 could name a firmware's own data key (the head's u32 at 52; since host 4
// every module's is 0, and a nonzero one is another build's): a slot holding a module for the same engine (from an
// older firmware, damaged, or this one) is emptied and written again; one already holding the very file is left.
// Errors carry a code for the installer's words: modfile (not a whole bundle), modnocmd (the firmware has no
// command 84), modbuild (another build's), modbusy (playing), modroom (the storage is full), modslots (no free slot),
// modwrite, modcheck.
import { bkPack } from "./fm1backup.js";

export const MOD_COMMAND = 84;
const MOD_FILE_MAGIC = 0x31444d4d;                       /* "MMD1" */
const mod_r35 = (a, at) => (a[at] | a[at + 1] << 7 | a[at + 2] << 14 | a[at + 3] << 21 | a[at + 4] << 28) >>> 0;
const mod_u35 = (n) => [0, 7, 14, 21, 28].map((sh) => (n >>> sh) & 0x7f);
const modError = (code, msg, rc) => Object.assign(new Error(msg), { code, rc });
/* module_store.c MS_*: a slot whose module plays (or will) */
const MOD_MS = { EMPTY: 0, LOADED: 1, STORED: 2, BAD: 3, NORAM: 4, CLASH: 5, SKIPPED: 6, STALE: 7 };

// the bundle's files: [{ engine, name, abi, key, crc, bytes }]
export function modBundle(bundle) {
  const files = [];
  for (let at = 0; at < (bundle?.length ?? 0);) {
    if (bundle.length - at < 52) throw modError("modfile", "The synth module file is cut short");
    const v = new DataView(bundle.buffer, bundle.byteOffset + at), total = v.getUint32(8, true), abi = v.getUint16(4, true);
    if (v.getUint32(0, true) !== MOD_FILE_MAGIC || total < 56 || at + total > bundle.length)
      throw modError("modfile", "Not a Melodee synth module file");
    const bytes = bundle.subarray(at, at + total);
    files.push({ engine: bytes[6], abi, name: String.fromCharCode(...bytes.subarray(32, 48)).replace(/\0.*$/, ""),
                 crc: v.getUint32(28, true), key: abi >= 2 ? v.getUint32(52, true) : 0, bytes });
    at += total;
  }
  return files;
}

// the device's slots (op 0), then each one's file (op 5): [{ slot, state, engine, len, crc, key }], and its own key
async function modSlots(ask) {
  const a = await ask([0], 3000);
  if (!a || a.length < 3 || a[0]) throw modError("modnocmd", "This firmware has no synth module command");
  const n = a[2], out = [];
  let p = 3, own = null;
  for (let k = 0; k < n; k++) {
    const state = a[p], engine = a[p + 1], len = mod_r35(a, p + 4);
    p += 9;
    while (a[p] !== 0 && p < a.length) p++;               /* (the name: a 0-terminated string) */
    p++;
    out.push({ slot: k, state, engine, len, crc: 0, key: 0 });
  }
  for (const s of out) {
    if (!s.len) continue;
    const b = await ask([5, s.slot], 3000);
    if (!b || b.length !== 19 || b[0]) throw modError("modnocmd", "This firmware cannot report its synth modules");
    s.crc = mod_r35(b, 4); s.key = mod_r35(b, 9); own = mod_r35(b, 14);
  }
  if (own === null) {                                    /* (every slot empty: ask slot 0 for the firmware's key) */
    const b = await ask([5, 0], 3000);
    if (!b || b.length !== 19 || b[0]) throw modError("modnocmd", "This firmware cannot report its synth modules");
    own = mod_r35(b, 14);
  }
  return { list: out, own };
}

// request([cmd, args], {timeout}) -> reply bytes (fm1backup.js BackupConnection). Resolves "current" when every module
// is on the device already, "written" after the missing ones were written and loaded or stored.
export async function installModules(request, bundle, onProgress = () => {}) {
  const files = modBundle(bundle);
  if (!files.length) return "current";
  const ask = (args, timeout) => request([MOD_COMMAND, args], { timeout });
  let { list, own } = await modSlots(ask);
  if (files.some((f) => f.abi >= 2 && f.key && f.key !== own))
    throw modError("modbuild", "These synth modules belong to another firmware build");
  const playing = (s) => [MOD_MS.LOADED, MOD_MS.STORED, MOD_MS.NORAM, MOD_MS.SKIPPED].includes(s.state);
  const todo = files.filter((f) => !list.some((s) => s.engine === f.engine && playing(s) && s.crc === f.crc && s.key === f.key));
  if (!todo.length) return "current";
  const total = todo.reduce((n, f) => n + f.bytes.length, 0);
  let done = 0;
  for (const f of todo) {
    /* its engine's slot (an older or damaged copy: emptied first, so its room is free again), else a free one; a
       damaged file in any slot goes too (no firmware plays it) */
    let s = list.find((x) => x.len && x.engine === f.engine) || list.find((x) => x.len && x.state === MOD_MS.BAD) ||
            list.find((x) => !x.len);
    if (!s) throw modError("modslots", "Every synth module slot is in use");
    if (s.len) {
      const rc = (await ask([4, s.slot], 10000))[0];
      if (rc === 4) throw modError("modbusy", "Stop playback, then retry", rc);
      if (rc) throw modError("modwrite", `Could not empty slot ${s.slot + 1} (${rc})`, rc);
      s.len = 0; s.engine = 0; s.state = MOD_MS.EMPTY;
    }
    let rc = (await ask([1, s.slot, ...mod_u35(f.bytes.length)], 10000))[0];
    for (let off = 0; !rc && off < f.bytes.length; off += 256) {
      rc = (await ask([2, s.slot, off & 0x7f, (off >> 7) & 0x7f, (off >> 14) & 0x7f, ...bkPack(f.bytes.subarray(off, off + 256))], 10000))[0];
      onProgress(done + Math.min(f.bytes.length, off + 256), total);
    }
    if (rc === 2) throw modError("modroom", `No room for ${f.name}`, rc);
    if (rc === 4) throw modError("modbusy", "Stop playback, then retry", rc);
    if (rc) throw modError("modwrite", `${f.name}: write refused (${rc})`, rc);
    const c = await ask([3, s.slot], 30000);
    if (c[0] === 2) throw modError("modroom", `No room for ${f.name}`, c[0]);
    if (c[0] === 5) throw modError("modbuild", `${f.name} is not for this firmware`, c[0]);
    if (c[0] && c[0] !== 7) throw modError("modcheck", `${f.name}: check failed (${c[0]})`, c[0]);   /* (7: stored, no RAM now) */
    s.len = f.bytes.length; s.engine = f.engine; s.state = c[1];
    done += f.bytes.length;
  }
  return "written";
}

// whether the device lacks one of the bundle's modules (op 0 and 5): true / false, null when it cannot tell
export async function modulesMissing(request, bundle) {
  try {
    const files = modBundle(bundle);
    if (!files.length) return false;
    const { list } = await modSlots((args, timeout) => request([MOD_COMMAND, args], { timeout }));
    return files.some((f) => !list.some((s) => s.engine === f.engine && s.crc === f.crc && s.key === f.key &&
                                               [MOD_MS.LOADED, MOD_MS.STORED, MOD_MS.NORAM, MOD_MS.SKIPPED].includes(s.state)));
  } catch (_) { return null; }
}
