// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The installer's 1.x -> 2.0 update (web/install/conversion.js) over the backup protocol, against the firmware's own
// code on both sides (test_cores.mjs): the FM-1 before is Melodee 1.x's virtual FM-1 with a 1.x backup restored into it
// (the fixtures; --backup DIR a real one, else Kerem's if found), the FM-1 after is 2.0's. prepareConversion reads the
// 1.x one and converts with the published converter files (build/melodee-convert.bin); writeConverted writes the 2.0
// one and reads it back. Then the 2.0 FM-1 asked again: nothing of 1.x's left there (an update from it converts nothing).
//   web/convert/build.sh, web/core/build.sh; node web/convert/update_check.mjs [--backup DIR]
import { readFileSync, existsSync, mkdtempSync, rmSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { BACKUP_CMD, bkManifest } from "../fm1backup.js";
import { legacyIds } from "./convert.mjs";
import { prepareConversion, writeConverted } from "../install/conversion.js";
import { ROOT, legacyFM1, fm1v2, restoreInto, objectsIn, fixtureBackup } from "./test_cores.mjs";

const opt = (name, def) => { const i = process.argv.indexOf(`--${name}`); return i > 0 ? process.argv[i + 1] : def; };
const BACKUP = opt("backup", [join(ROOT, "build/backups/fm1-900-1x-2026-11-19"),
  join(ROOT, "../../../build/backups/fm1-900-1x-2026-11-19")].find((d) => existsSync(join(d, "1.bin"))));
let failures = 0;
const check = (what, ok, detail = "") => {
  console.log(`update: ${what.padEnd(76)} ${ok ? "ok" : "FAIL"}${detail ? `  ${detail}` : ""}`);
  if (!ok) failures++;
  return ok;
};
const bundle = readFileSync(join(ROOT, "build/melodee-convert.bin"));

async function update(name, objects) {
  const before = await legacyFM1();
  const refused = await restoreInto(before.request, objects);
  /* (1.x's PUT refuses the settings its own GET gives when a voice budget's tag or the radio keys are in them: the 1.x
     FM-1 here keeps its own; legacy.c lg_settings converts such settings, convert_check.mjs) */
  const own = refused.length === 1 && refused[0].startsWith("1(");
  check(`${name}: the 1.x FM-1 took the backup (${objects.size} objects${own ? ", its own settings kept" : ""})`, !refused.length || own, own ? "" : refused.join(" "));
  let fetched = 0;
  const t0 = performance.now();
  const conv = await prepareConversion(before.request, "FM-1_9010601", async () => { fetched++; return bundle; });
  const ms = performance.now() - t0;
  if (!check(`${name}: 1.x records found, converted (${conv?.old.join(" ")})`, !!conv && fetched === 1)) return;
  const sizes = conv.after.objects.filter((o) => o.size && (o.id === 0 || (o.id >= 2 && o.id <= 5) || (o.id >= 64 && o.id < 92))).map((o) => `${o.id}: ${o.size} B`);
  console.log(`update: ${name}: backup + conversion ${ms.toFixed(0)} ms; ${conv.before.objects.length} objects listed; projects packed ${sizes.join(", ")}`);
  const clean = !conv.refused.length && !conv.left.length;
  check(`${name}: nothing refused, nothing left out`, clean, clean ? "" : `${conv.refused} / ${conv.left}`);
  const after = await fm1v2();
  let written;
  try { written = await writeConverted(after.request, conv.after); }
  catch (e) { check(`${name}: written into the 2.0 FM-1 and read back`, false, e.message); return; }
  check(`${name}: written into the 2.0 FM-1 and read back (${written.join(" ")})`, written.length > 0);
  const again = await prepareConversion(after.request, "FM-1_900", async () => { fetched++; return bundle; });
  check(`${name}: the 2.0 FM-1 holds nothing of 1.x's (its update converts nothing)`, again === null && fetched === 1);
  const list = bkManifest(await after.request([BACKUP_CMD.LIST, []]));
  check(`${name}: its project slots, banks and kits as converted`, conv.after.objects.filter((o) => o.id > 1 && o.size)
    .every((o) => list.find((l) => l.id === o.id)?.crc === o.crc));
}

const tmp = mkdtempSync(join(tmpdir(), "melodee-update-"));
try {
  await update("fixtures", fixtureBackup(tmp));
  if (BACKUP) await update("backup", objectsIn(BACKUP));
  else console.log("update: no 1.x backup (--backup DIR): skipped");
  check("a 2.0 backup's ids: none of 1.x's", legacyIds(new Map([[1, Uint8Array.of(0x36, 0x52, 0x45, 0x50)]])).length === 0);
} finally {
  rmSync(tmp, { recursive: true, force: true });
}
console.log(failures ? `update: ${failures} FAILED` : "update: all ok");
process.exit(failures ? 1 : 0);
