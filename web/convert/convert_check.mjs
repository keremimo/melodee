// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The converter in WebAssembly (convert.mjs: what the page runs) against the same converter built natively
// (legacy_native convert: the one the parity renders check): every 1.x backup converted by both must give the same
// 2.0 objects, byte for byte, and the same report.
//   1. fixtures made by 1.x's own code (legacy_native fixtures + store, kit_fixture): the test projects as projects
//      2.., the two user kits, the store's settings, template and preset banks, together as one backup
//   2. --backup DIR: a real 1.x backup (<id>.bin, as tools/prophet_device.py backup writes it), if given or found
//   3. the same backup as the installer's file (felucca-backup JSON) read back by readBackupV1
//   web/convert/build.sh; node web/convert/convert_check.mjs [--backup DIR]
import { readFileSync, readdirSync, writeFileSync, existsSync, mkdtempSync, mkdirSync, copyFileSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { legacyConverter, readBackupV1, LEGACY_MODULES } from "./convert.mjs";
import { bkCrc } from "../fm1backup.js";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "../..");
const opt = (name, def) => { const i = process.argv.indexOf(`--${name}`); return i > 0 ? process.argv[i + 1] : def; };
const B = join(ROOT, "build/convert");
const NATIVE = join(B, "legacy_native"), MODDIR = join(B, "modules");
const BACKUP = opt("backup", [join(ROOT, "build/backups/fm1-900-1x-2026-11-19"),
  join(ROOT, "../../../build/backups/fm1-900-1x-2026-11-19")].find((d) => existsSync(join(d, "1.bin"))));
let failures = 0;
const check = (what, ok, detail = "") => {
  console.log(`convert: ${what.padEnd(76)} ${ok ? "ok" : "FAIL"}${detail ? `  ${detail}` : ""}`);
  if (!ok) failures++;
  return ok;
};
const run = (args) => {
  const r = spawnSync(args[0], args.slice(1), { encoding: "utf8" });
  if (r.status !== 0) throw new Error(`${args.join(" ")}: rc ${r.status}\n${r.stdout}${r.stderr}`);
  return r.stdout;
};
const objectsIn = (dir) => new Map(readdirSync(dir).flatMap((f) => {
  const m = /^(\d+)\.bin$/.exec(f);
  return m ? [[Number(m[1]), readFileSync(join(dir, f))]] : [];
}));

const tmp = mkdtempSync(join(tmpdir(), "melodee-convert-"));
const cv = await legacyConverter(readFileSync(join(B, "melodee_legacy.wasm")),
  Object.fromEntries(LEGACY_MODULES.map((m) => [m, readFileSync(join(MODDIR, `${m}.wasm.mmod`))])));

/* dir (a 1.x backup's <id>.bin) through both: the same objects, the same report */
async function compare(name, dir, input = objectsIn(dir)) {
  const nat = join(tmp, `native-${name}`);
  mkdirSync(nat);
  run([NATIVE, MODDIR, "convert", dir, nat]);
  const want = new Map(readdirSync(nat).flatMap((f) => {
    const m = /^v2-(\d+)\.bin$/.exec(f);
    return m ? [[Number(m[1]), readFileSync(join(nat, f))]] : [];
  }));
  const t0 = performance.now();
  const got = await cv.convert(input);
  const ms = performance.now() - t0;
  const ids = (m) => [...m.keys()].sort((a, b) => a - b).join(" ");
  check(`${name}: the same objects (${ids(want)})`, ids(want) === ids(got.objects), ids(want) === ids(got.objects) ? "" : ids(got.objects));
  const differ = [...want].filter(([id, b]) => !got.objects.has(id) || Buffer.compare(b, Buffer.from(got.objects.get(id))));
  check(`${name}: byte for byte (${[...want.values()].reduce((n, b) => n + b.length, 0)} B)`, !differ.length,
    differ.map(([id]) => id).join(" "));
  check(`${name}: the same report (${got.report.split("\n").length - 1} lines)`, got.report === readFileSync(join(nat, "report.txt"), "utf8"));
  const clean = !got.refused.length && !got.left.length;
  check(`${name}: nothing refused, nothing left out`, clean, clean ? "" : `refused ${got.refused.join(" ")} left ${got.left.join(" ")}`);
  console.log(`convert: ${name}: ${ms.toFixed(0)} ms in WebAssembly (power-on included)`);
  return got;
}

try {
  const fx = join(tmp, "fx"), st = join(tmp, "st"), all = join(tmp, "all");
  for (const d of [fx, st, all]) mkdirSync(d);
  run([join(B, "kit_fixture"), fx]);
  run([NATIVE, MODDIR, "fixtures", fx]);
  run([NATIVE, MODDIR, "store", st]);
  const recs = readdirSync(fx).filter((f) => /^v1-.*\.rec$/.test(f)).sort();
  const pids = [2, 3, 4, 5, ...Array.from({ length: 28 }, (_, i) => 64 + i)];
  recs.forEach((f, i) => copyFileSync(join(fx, f), join(all, `${pids[i]}.bin`)));
  for (const f of ["92.bin", "93.bin"]) copyFileSync(join(fx, f), join(all, f));
  for (const f of readdirSync(st)) if (/^\d+\.bin$/.test(f)) copyFileSync(join(st, f), join(all, f));
  console.log(`convert: fixtures: ${recs.map((f) => f.slice(3, -4)).join(", ")} as projects; kits 92, 93; ${readdirSync(st).length} store objects`);
  await compare("fixtures", all);

  if (BACKUP) {
    await compare("backup", BACKUP);
    const objects = [...objectsIn(BACKUP)].sort(([a], [b]) => a - b).map(([id, b]) => ({ id, size: b.length, crc: bkCrc(b), data: b.toString("base64") }));
    const file = JSON.stringify({ format: "felucca-backup", version: 1, firmware: "1.x", created: "", objects });
    const back = readBackupV1(file);
    check("backup: its file read back (readBackupV1) holds the same objects",
      back.size === objects.length && objects.every((o) => !Buffer.compare(Buffer.from(back.get(o.id)), Buffer.from(o.data, "base64"))));
    await compare("backup file", BACKUP, back);
    let bad = false;
    try { readBackupV1(JSON.stringify({ ...JSON.parse(file), objects: objects.map((o, i) => i ? o : { ...o, crc: o.crc ^ 1 }) })); } catch { bad = true; }
    check("a changed object's checksum refused", bad);
  } else console.log("convert: no 1.x backup (--backup DIR): skipped");
} finally {
  rmSync(tmp, { recursive: true, force: true });
}
console.log(failures ? `convert: ${failures} FAILED` : "convert: all ok");
process.exit(failures ? 1 : 0);
