// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The update from Melodee 1.x to 2.0 (installer.js runs these around the firmware write). Firmware 2.0 reads only its
// own records: the installer brings the user's along. Before anything is written, every object the running firmware
// lists is read (the backup, saved to the user's computer) and the 1.x ones converted in the page (web/convert:
// Melodee 1.x's own readers in WebAssembly), so a backup or a conversion that fails leaves the FM-1 as it was. After the
// firmware and its synths: the converted objects written as a restore writes a backup, then listed again.
//   prepareConversion(request, firmware, getBundle, onProgress) -> null (nothing of 1.x's) | { before, after, report,
//     refused, left, old }   before / after: backup files (fm1backup.js), the 1.x one and the 2.0 one to write
//   writeConverted(request, after, onProgress) -> the ids written; throws code "convverify" if one reads back otherwise
import { BACKUP_CMD, bkCheck, bkManifest, bkR32, bkU32, bkUnpack, backupFile, captureBackup, readBackup, restoreBackup } from "../fm1backup.js";
import { convertBundle, legacyConverter, legacyIds, readBackupV1 } from "../convert/convert.mjs";

const convError = (code, msg) => Object.assign(new Error(msg), { code, detail: msg });

/* the ids holding 1.x's records, from each listed object's first bytes (an FM-1 already on 2.0: no more is read) */
async function legacyOnDevice(request) {
  const list = bkManifest(await request([BACKUP_CMD.LIST, []], { timeout: 3000, retries: 0 })), heads = new Map();
  for (const o of list) {
    if (!o.size) continue;
    const n = Math.min(12, o.size), a = await request([BACKUP_CMD.GET, [o.id, ...bkU32(0), n & 127, n >>> 7]], { timeout: 1000, retries: 1 });
    bkCheck(a[1]);
    if (a[0] !== o.id || bkR32(a, 2) !== 0 || (a[7] | a[8] << 7) !== n) throw new Error("Unexpected backup reply");
    heads.set(o.id, bkUnpack(a.slice(9), n));
  }
  return legacyIds(heads);
}

/* request: a BackupConnection's; getBundle: the build's converter files (<package>-convert.bin), fetched only when the
   device holds 1.x's records */
export async function prepareConversion(request, firmware, getBundle, onProgress = () => {}) {
  let before, old;
  try {
    if (!(await legacyOnDevice(request)).length) return null;
    before = await captureBackup(request, firmware, onProgress, readBackupV1);
  } catch (e) { throw convError("convbackup", e.message); }
  const objects = readBackupV1(before);
  old = legacyIds(objects);
  let r;
  try {
    const { wasm, modules } = convertBundle(await getBundle());
    r = await (await legacyConverter(wasm, modules)).convert(new Map(old.map((id) => [id, objects.get(id)])));
  } catch (e) { throw e.code ? e : convError("convfail", e.message); }
  /* every id the backup had: 1.x's converted (or nothing: its place emptied), 2.0's own as they were */
  const empty = new Uint8Array(0);
  const after = backupFile(before.objects.map(({ id }) => ({ id, bytes: old.includes(id) ? r.objects.get(id) ?? empty : objects.get(id) })),
                           `${firmware}, converted`);
  try { readBackup(after); }                          // (what restoreBackup checks: before the firmware is written)
  catch (e) { throw convError("convfail", e.message); }
  return { before, after, report: r.report, refused: r.refused, left: r.left, old };
}

/* the restore's checks (sizes, the firmware's own PUT checks) before its first write; live music last. Read back: an
   object the firmware keeps as written lists the same size and CRC (live music and the settings are taken into RAM and
   listed as the firmware writes them again: present is enough) */
export async function writeConverted(request, after, onProgress = () => {}) {
  await restoreBackup(request, after, onProgress);
  const list = bkManifest(await request([BACKUP_CMD.LIST, []], { timeout: 3000, retries: 0 }));
  const bad = after.objects.filter((o) => {
    const l = list.find((x) => x.id === o.id);
    if (!l) return o.size > 0;
    return o.id <= 1 ? !l.size !== !o.size : l.size !== o.size || l.crc !== o.crc;
  });
  if (bad.length) throw convError("convverify", `Read back otherwise: ${bad.map((o) => o.id).join(", ")}`);
  return after.objects.filter((o) => o.size).map((o) => o.id);
}
