// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The converter tests' FM-1s (update_check.mjs, ../test_install_convert.mjs): Melodee 1.x's virtual FM-1 (the
// converter's own build: build/convert/melodee_legacy.wasm, its five synths) and 2.0's (build/core/melodee_core.wasm, its
// ten), each powered on with its synths in the pool and answering the editor's commands as the device does; and a 1.x
// backup made by 1.x's own code (the converter's fixtures: legacy_native fixtures + store, kit_fixture).
import { readFileSync, readdirSync, mkdirSync, copyFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { BACKUP_CMD, bkU32, bkPack, bkCrc } from "../fm1backup.js";
import { installModules } from "../core/mod_link.mjs";
import { LEGACY_MODULES } from "./convert.mjs";

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), "../..");
const B = join(ROOT, "build/convert");
const V2_MODS = ["sid", "808", "909", "sidkit", "cr78", "fmkit", "sampler", "cz", "fm6", "prophet"];

/* a virtual FM-1 powered on with its synths: reply(cmd, args) -> the reply's payload (null: none), request as a
   BackupConnection's */
async function fm1(wasm, files) {
  const x = (await WebAssembly.instantiate(await WebAssembly.compile(wasm), {})).exports;
  x._initialize?.();
  x.core_boot(0); x.core_poll(100);
  await installModules(x, files);
  x.core_poll(100); x.core_boot(1); x.core_poll(100);
  const s = x.core_scratch(), size = x.core_scratch_size();
  const reply = (cmd, args) => {
    const f = Uint8Array.from([0xF0, 0x7D, 0x46, 0x4C, cmd, ...args, 0xF7]);
    new Uint8Array(x.memory.buffer).set(f, s);
    x.core_sysex_in(s, f.length);
    x.core_poll(5);                                   // (the main loop: what a commit leaves it)
    const n = x.core_sysex_out(s, size), b = new Uint8Array(x.memory.buffer).slice(s, s + n);
    for (let i = 0, a = -1; i < b.length; i++) {
      if (b[i] === 0xF0) a = i;
      else if (b[i] === 0xF7 && a >= 0) { if (b[a + 4] === cmd) return Array.from(b.slice(a + 5, i)); a = -1; }
    }
    return null;
  };
  const request = async ([cmd, args]) => {
    const r = reply(cmd, args);
    if (!r) throw new Error(`no reply to ${cmd}`);
    return r;
  };
  return { x, reply, request };
}
export const legacyFM1 = () => fm1(readFileSync(join(B, "melodee_legacy.wasm")), LEGACY_MODULES.map((m) => readFileSync(join(B, "modules", `${m}.wasm.mmod`))));
export const fm1v2 = () => fm1(readFileSync(join(ROOT, "build/core/melodee_core.wasm")), V2_MODS.map((m) => readFileSync(join(ROOT, "build/modules/wasm", `${m}.mmod`))));

/* one object PUT as the editor's restore puts it (begin, chunks, commit): 0, or "op:rc" */
export async function put(request, id, bytes) {
  const rc = async (args) => { const r = (await request([BACKUP_CMD.PUT, args]))[2]; return r && `${args[0]}:${r}`; };
  let r = await rc([0, id, ...bkU32(bytes.length), ...bkU32(bkCrc(bytes))]);
  for (let off = 0; !r && off < bytes.length; off += 256) r = await rc([1, id, ...bkU32(off), ...bkPack(bytes.subarray(off, off + 256))]);
  return r || rc([2, id]);
}
/* objects into an FM-1, live music last: the ids it refused ("id(op:rc)") */
export async function restoreInto(request, objects) {
  const refused = [];
  for (const id of [...objects.keys()].sort((a, b) => (a === 0) - (b === 0) || a - b))
    if (objects.get(id).length) { const r = await put(request, id, objects.get(id)); if (r) refused.push(`${id}(${r})`); }
  return refused;
}
export const objectsIn = (dir) => new Map(readdirSync(dir).flatMap((f) => {
  const m = /^(\d+)\.bin$/.exec(f);
  return m ? [[Number(m[1]), new Uint8Array(readFileSync(join(dir, f)))]] : [];
}));
/* a 1.x backup made by 1.x's code in dir: three projects (raw records: six fill the 1.x pool with the synths in it),
   live music, the two user kits, the store's preset banks; not its settings (a voice budget set: 1.x's own PUT refuses
   the tag its export writes, legacy.c lg_settings) */
export function fixtureBackup(dir) {
  const fx = join(dir, "fx"), st = join(dir, "st"), all = join(dir, "all");
  for (const d of [fx, st, all]) mkdirSync(d, { recursive: true });
  const run = (args) => { const r = spawnSync(args[0], args.slice(1), { encoding: "utf8" }); if (r.status) throw new Error(`${args[0]}: ${r.stdout}`); };
  run([join(B, "kit_fixture"), fx]);
  run([join(B, "legacy_native"), join(B, "modules"), "fixtures", fx]);
  run([join(B, "legacy_native"), join(B, "modules"), "store", st]);
  for (const [id, f] of [[2, "synths"], [3, "kits"], [64, "userkit"], [0, "synths"]]) copyFileSync(join(fx, `v1-${f}.rec`), join(all, `${id}.bin`));
  for (const f of ["92.bin", "93.bin"]) copyFileSync(join(fx, f), join(all, f));
  for (const f of readdirSync(st)) if (/^\d+\.bin$/.test(f) && f !== "1.bin") copyFileSync(join(st, f), join(all, f));
  return objectsIn(all);
}
