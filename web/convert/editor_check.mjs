// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The editor's Melodee 1.x files on 2.0 (web/editor.html: Restore and Open project file), against the firmware's own code
// (test_cores.mjs):
//   1. a .mel of version 2 written by 1.x's own web/mel_file.js (the frozen tree's) from the user-kit fixture and its two
//      kits: read back byte for byte (mel_legacy.mjs), converted, the record and kits the same bytes as the native
//      converter's (whose renders were checked against 1.x's), opened on the 2.0 FM-1 (melImportParts)
//   2. a 1.x backup file (the fixtures, as the editor saved it from a 1.x FM-1): converted (conversion.js convertBackup)
//      and restored into the 2.0 FM-1, read back
//   3. Backup from a 1.x FM-1 with kits: 2.0's checks refuse its KIT1 kits, readBackupV1 takes them
//   web/convert/build.sh, web/core/build.sh; node web/convert/editor_check.mjs
import { readFileSync, mkdtempSync, rmSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { BACKUP_CMD, bkManifest, backupFile, captureBackup, readBackup } from "../fm1backup.js";
import { melImportParts, recInfo } from "../mel_file.js";
import { convertBundle, legacyConverter, legacyIds, readBackupV1 } from "./convert.mjs";
import { melUnpackV2, melConvert } from "./mel_legacy.mjs";
import { convertBackup, writeConverted } from "../install/conversion.js";
import { ROOT, legacyFM1, fm1v2, restoreInto, fixtureBackup } from "./test_cores.mjs";

const REF = readFileSync(join(ROOT, "web/convert/LEGACY_REF"), "utf8").trim();
const v1mel = await import(join(ROOT, "build/legacy", REF, "src/web/mel_file.js"));   // (1.x's own)
let failures = 0;
const check = (what, ok, detail = "") => {
  console.log(`editor: ${what.padEnd(76)} ${ok ? "ok" : "FAIL"}${detail ? `  ${detail}` : ""}`);
  if (!ok) failures++;
  return ok;
};
const same = (a, b) => !!a && !!b && a.length === b.length && a.every((v, i) => v === b[i]);
const bundle = readFileSync(join(ROOT, "build/melodee-convert.bin"));
const { wasm, modules } = convertBundle(bundle);
const converter = await legacyConverter(wasm, modules);
const tmp = mkdtempSync(join(tmpdir(), "melodee-editor-"));

try {
  const backup = fixtureBackup(tmp), fx = join(tmp, "fx");
  {                                                   /* 1. a 1.x project file */
    const project = new Uint8Array(readFileSync(join(fx, "v1-userkit.rec")));
    const kits = [{ kit: 9, bytes: new Uint8Array(readFileSync(join(fx, "92.bin"))) }, { kit: 10, bytes: new Uint8Array(readFileSync(join(fx, "93.bin"))) }];
    const file = v1mel.melPack({ project, kits });
    check(`.mel v2 by 1.x's mel_file.js (${file.length} B: the user-kit project, KIT 1 and KIT 2)`, (file[4] | file[5] << 8) === 2);
    const back = melUnpackV2(file);
    check("read back as 1.x wrote it: the record and both KIT1 kits, byte for byte",
      same(back.project, project) && back.kits.length === 2 && back.kits.every((k, i) => k.kit === kits[i].kit && same(k.bytes, kits[i].bytes)));
    let refused = false;
    try { melUnpackV2(Uint8Array.from(file, (v, i) => (i === file.length - 9 ? v ^ 1 : v))); } catch { refused = true; }
    check("one bit changed: refused", refused);
    const t0 = performance.now();
    const conv = await melConvert(converter, file);
    console.log(`editor: converted in ${(performance.now() - t0).toFixed(0)} ms (power-on included)`);
    check("the record as the native converter writes it (its renders checked against 1.x's)", same(conv.project, new Uint8Array(readFileSync(join(fx, "v2-userkit.rec")))));
    check("the kits as the native converter writes them (KIT2)", same(conv.kits[0].bytes, new Uint8Array(readFileSync(join(fx, "v2-92.bin")))) &&
      same(conv.kits[1].bytes, new Uint8Array(readFileSync(join(fx, "v2-93.bin")))));
    check("the record names the same kits (KIT 1, KIT 2)", same(recInfo(conv.project).kits, [9, 10]));
    const fm = await fm1v2();
    const r = await melImportParts(fm.request, conv);
    const list = bkManifest(await fm.request([BACKUP_CMD.LIST, []]));
    const slot = [2, 3, 4, 5][r.slot];
    check(`opened on the 2.0 FM-1: project ${r.slot + 1}, ${r.kits.map((k) => `KIT ${k.from - 8} -> ${k.to - 8}`).join(", ")}`,
      !!list.find((o) => o.id === slot)?.size && r.kits.length === 2 && r.kits.every((k) => list.find((o) => o.id === 92 + k.to - 9)?.size));
  }
  {                                                   /* 2. a 1.x backup file */
    const before = await legacyFM1();
    await restoreInto(before.request, backup);
    const file = await captureBackup(before.request, "FM-1_91061", () => {}, readBackupV1);
    const text = JSON.stringify(file);
    const conv = await convertBackup(text, async () => bundle);
    check(`a 1.x backup file converted (${conv.old.join(" ")})`, !!conv && !conv.refused.length && !conv.left.length);
    let ok = true;
    try { readBackup(conv.after); } catch { ok = false; }
    check("the converted file passes 2.0's checks (readBackup)", ok);
    const fm = await fm1v2();
    const written = await writeConverted(fm.request, conv.after);
    check(`restored into the 2.0 FM-1 and read back (${written.join(" ")})`, written.includes(2) && written.includes(92));
    check("a 2.0 backup file: nothing to convert", (await convertBackup(conv.after, async () => bundle)) === null);
  }
  {                                                   /* 3. Backup from a 1.x FM-1 */
    const fm = await legacyFM1();
    await restoreInto(fm.request, backup);
    let refused = false;
    try { await captureBackup(fm.request, "FM-1_91061"); } catch { refused = true; }
    const file = await captureBackup(fm.request, "FM-1_91061", () => {}, readBackupV1);
    check("Backup from a 1.x FM-1 with kits: 2.0's checks refuse its KIT1 kits, 1.x's take them", refused && legacyIds(readBackupV1(file)).includes(92));
  }
} finally {
  rmSync(tmp, { recursive: true, force: true });
}
console.log(failures ? `editor: ${failures} FAILED` : "editor: all ok");
process.exit(failures ? 1 : 0);
