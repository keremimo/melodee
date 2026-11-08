// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Melodee 1.x -> 2.0 in the page (the web installer's 2.0 update, Studio's imports) and in Node (the tests): the last
// Melodee 1.x's virtual FM-1 with web/convert/legacy.c in it (build/convert/melodee_legacy.wasm and its five synths'
// module files build/convert/modules/<name>.wasm.mmod: web/convert/build.sh) takes a 1.x backup's objects through its
// own readers and gives Melodee 2.0's objects for the same ids, with a report of what 2.0 could not keep as 1.x had it.
// Each conversion runs on a new power-on of that FM-1 (nothing of an earlier one left), the calls in legacy.c's order
// (its lg_convert_dir makes the same ones natively: web/convert/convert_check.mjs compares them byte for byte).
//   const cv = await legacyConverter(wasmBytes, { sid, drum, cz, fm6, prophet });   // (module files' bytes)
//   const { objects, report, refused, left } = await cv.convert(readBackupV1(file));
//   objects: Map id -> Uint8Array (2.0's; an empty one: nothing to write there), refused: ids 1.x would not read,
//   left: ids the backup had that 2.0 has no place for
import { installModules } from "../core/mod_link.mjs";
import { bkCrc } from "../fm1backup.js";

export const LEGACY_MODULES = ["sid", "drum", "cz", "fm6", "prophet"];   // (linked in this order, as legacy_native's)
const range = (a, n) => Array.from({ length: n }, (_, i) => a + i);
const PROJECTS = [2, 3, 4, 5, ...range(64, 28), 0];
const BANKS = range(6, 23);
const KITS = range(92, 8);
const TAKEN = new Set([1, ...PROJECTS, ...BANKS, ...KITS]);

/** A 1.x backup file (the installer's "felucca-backup" JSON, as text or parsed) -> Map id -> bytes, every object's size
 * and CRC checked. Its contents are left to 1.x's own readers (the converter): a kind 2.0's fm1backup.js would refuse
 * (a KIT1 kit, an old preset bank) is what this is for. */
export function readBackupV1(file) {
  if (typeof file === "string") file = JSON.parse(file);
  if (!file || file.format !== "felucca-backup" || file.version !== 1 || !Array.isArray(file.objects))
    throw new Error("Not a Melodee backup");
  const out = new Map();
  for (const o of file.objects) {
    if (!o || !Number.isInteger(o.id) || o.id < 0 || o.id > 255 || !Number.isInteger(o.size) || o.size < 0 ||
        typeof o.data !== "string" || out.has(o.id)) throw new Error("Invalid backup object");
    const bytes = Uint8Array.from(atob(o.data), (c) => c.charCodeAt(0));
    if (bytes.length !== o.size || bkCrc(bytes) !== o.crc >>> 0) throw new Error(`Backup object ${o.id}: checksum mismatch`);
    out.set(o.id, bytes);
  }
  return out;
}

/** The converter: wasm (melodee_legacy.wasm's bytes or a compiled WebAssembly.Module), modules ({sid, drum, cz, fm6,
 * prophet}: their .wasm.mmod files' bytes). */
export async function legacyConverter(wasm, modules) {
  const module = wasm instanceof WebAssembly.Module ? wasm : await WebAssembly.compile(wasm);
  const files = LEGACY_MODULES.map((m) => {
    if (!modules?.[m]) throw new Error(`the converter needs 1.x's ${m} module`);
    return Uint8Array.from(modules[m]);
  });
  return {
    /** objects: Map (or [[id, bytes], ...]) of a 1.x backup -> { objects, report, refused, left } */
    async convert(objects) {
      const input = objects instanceof Map ? objects : new Map(objects);
      const x = (await WebAssembly.instantiate(module, {})).exports;
      x._initialize?.();
      x.core_boot(0); x.core_poll(100);                 // (power-on as legacy.c lg_boot_modules: the synths in the pool)
      await installModules(x, files);
      x.core_poll(100); x.core_boot(1); x.core_poll(100);
      const u8 = () => new Uint8Array(x.memory.buffer);  // (memory may grow: a fresh view each time)
      const out = new Map(), refused = [];
      const take = (id) => {                            // id's bytes at cv_in: their length, 0 none
        const b = input.get(id);
        if (!b?.length) return 0;
        if (b.length > x.cv_in_size()) { refused.push(id); return 0; }
        u8().set(b, x.cv_in());
        return b.length;
      };
      const give = (id, n) => out.set(id, u8().slice(x.cv_out(), x.cv_out() + n));
      for (const id of PROJECTS) {
        const n = take(id), m = n ? x.cv_project(id, n) : 0;
        if (n && !m) refused.push(id); else if (m) give(id, m);
      }
      let any = false;                                   // (1.x reads its banks together: all in, then each)
      for (const id of BANKS) {
        const n = take(id);
        if (!n) continue;
        if (x.cv_bank_in(id, n)) refused.push(id);
        any = true;
      }
      if (any) {
        x.cv_banks();
        for (const id of BANKS) { const m = x.cv_bank(id); if (m) give(id, m); }
      }
      for (const id of KITS) {
        const n = take(id), m = n ? x.cv_kit(id, n) : 0;
        if (m < 0) refused.push(id); else if (n) give(id, m);
      }
      const n = take(1), m = n ? x.cv_settings(n) : 0;
      if (n && !m) refused.push(1); else if (m) give(1, m);
      const report = new TextDecoder().decode(u8().subarray(x.cv_report(), x.cv_report() + x.cv_report_len()));
      const left = [...input.keys()].filter((id) => input.get(id)?.length && !TAKEN.has(id)).sort((a, b) => a - b);
      return { objects: out, report, refused, left };
    },
  };
}
