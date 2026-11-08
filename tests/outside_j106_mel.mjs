// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The .mel round trip of a project that plays an outside synth (tests/outside_j106_test.c, run_tests.sh outside_j106):
// the device's stored project (DIR/j106.hpj: two parts of Saadet's JUNO-106, an FBKR record) exported by the web page
// (web/mel_file.js melExport) from a simulated device, the file read back, imported (melImport) into another simulated
// device's first free project; what the import sent is DIR/out-j106.bin, for outside_j106_test --mel.
//   node tests/outside_j106_mel.mjs DIR
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc } from "../web/fm1backup.js";
import { MEL_CONST, pjExpand, recInfo, melUnpack, melExport, melImport, melProjectId } from "../web/mel_file.js";

const DIR = process.argv[2];
let fails = 0;
const ok = (c, what) => { console.log(`${("mel: " + what).padEnd(100)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const u32 = (b, at) => (b[at] | b[at + 1] << 8 | b[at + 2] << 16 | b[at + 3] << 24) >>> 0;
const IDS = [...Array.from({ length: 29 }, (_, i) => i), ...Array.from({ length: 28 }, (_, i) => 64 + i),
  ...Array.from({ length: 8 }, (_, i) => 92 + i)];
function device(objs) {                          // (web/test_mel.mjs's simulated device: LIST, GET, PUT)
  const d = { objs: new Map(objs), log: [], staged: null };
  d.request = async ([cmd, a]) => {
    if (cmd === BACKUP_CMD.LIST) {
      const out = [1, 0, IDS.length];
      for (const id of IDS) { const v = d.objs.get(id) || new Uint8Array(0); out.push(id, ...bkU32(v.length), ...bkU32(v.length ? bkCrc(v) : 0)); }
      return out;
    }
    if (cmd === BACKUP_CMD.GET) {
      const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = d.objs.get(id);
      return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))];
    }
    if (cmd === BACKUP_CMD.PUT) {
      const [op, id] = a;
      if (op === 0) { d.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, 0]; }
      if (op === 1) { d.staged.bytes.push(...bkUnpack(a.slice(7), Math.min(256, d.staged.size - bkR32(a, 2)))); return [op, id, 0]; }
      if (op === 2) {
        const s = Uint8Array.from(d.staged.bytes), rc = bkCrc(s) === d.staged.crc ? 0 : 2;
        if (!rc) { d.objs.set(id, s); d.log.push(id); }
        return [op, id, rc];
      }
      if (op === 3) return [op, id, 0];
    }
    throw new Error(`unexpected ${cmd}`);
  };
  return d;
}

const stored = new Uint8Array(readFileSync(join(DIR, "j106.hpj"))), rec = pjExpand(stored);
ok(u32(stored, 0) === MEL_CONST.PZ_MAGIC && u32(rec, 0) === MEL_CONST.FBKR, "the device's project of the JUNO-106: packed, an FBKR record");
const source = device([[melProjectId(1), stored]]);
const out = await melExport(source.request, melProjectId(1));
const back = melUnpack(out.file);
ok(out.name === "JUNO SONG" && !out.kits.length && same(back.project, rec) && !back.kits.length && !source.log.length,
  "export: the .mel holds the record byte for byte (its engine map and the JUNO-106's patches), no kits");
ok(recInfo(back.project).name === "JUNO SONG", ".. the page reads its name");
const target = device([]);
const r = await melImport(target.request, out.file);
const sent = target.objs.get(melProjectId(r.slot));
ok(r.slot === 0 && sent && u32(sent, 0) === MEL_CONST.PZ_MAGIC && same(pjExpand(sent), rec) && same(target.log, [melProjectId(0)]),
  "import: into the first free project, packed by the page, the same record");
writeFileSync(join(DIR, "out-j106.bin"), sent);
console.log(fails ? "mel: FAILED" : "mel: the outside synth's project through a .mel file: passed");
process.exit(fails ? 1 : 0);
