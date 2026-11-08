// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Melodee 1.x's project files (.mel version 2: a 1.x project record and the KIT1 kits it plays) for 2.0: read as 1.x's
// web/mel_file.js wrote them (the same container and coder, a kit coded as 1.x laid it out: ../mel_file.js cmDecode's
// "kit1"), then the record and the kits through the converter (convert.mjs: 1.x's own readers) into what a version 3
// file holds: the project's whole 2.0 record and its kits' KIT2 blobs, under the same KIT values. The editor opens the
// result as it opens a 2.0 file (mel_file.js melImportParts).
//   melUnpackV2(file) -> { project, kits: [{ kit, bytes }] }   (1.x's forms; every length and sum checked)
//   melConvert(converter, file) -> { project, kits, name, report }   (converter: legacyConverter's)
import { cmDecode, recInfo } from "../mel_file.js";
import { bkCrc } from "../fm1backup.js";

const u32 = (b, at) => (b[at] | b[at + 1] << 8 | b[at + 2] << 16 | b[at + 3] << 24) >>> 0;
const MEL_HEAD = 32, SEC_HEAD = 16, T_PROJECT = 1, T_KIT = 2, REC_BYTES = 27904, KIT1_MAX = 28392, KIT_FIRST = 9, KIT_COUNT = 8;
const MEL_V2_MAX = MEL_HEAD + (1 + KIT_COUNT) * SEC_HEAD + REC_BYTES + KIT_COUNT * KIT1_MAX;   // (1.x's MEL_MAX)

export function melUnpackV2(file) {
  const bad = (why) => { throw new Error(`Not a Melodee 1.x project file (${why})`); };
  if (!(file instanceof Uint8Array)) file = new Uint8Array(file);
  if (file.length < MEL_HEAD + SEC_HEAD || u32(file, 0) !== 0x004C454D || (file[4] | file[5] << 8) !== 2) bad("no MEL version 2 header");
  if ((file[6] | file[7] << 8) !== MEL_HEAD || u32(file, 8) || file[22] || file[23]) bad("header");
  if (u32(file, 28) !== bkCrc(file.subarray(0, 28))) bad("header checksum");
  if (u32(file, 12) !== file.length || file.length > MEL_V2_MAX) bad("length");
  if (u32(file, 24) !== bkCrc(file.subarray(MEL_HEAD))) bad("checksum");
  const count = file[20] | file[21] << 8;
  if (!count || count > 1 + KIT_COUNT) bad("sections");
  let at = MEL_HEAD, decoded = 0, project = null;
  const kits = [];
  for (let i = 0; i < count; i++) {
    if (at + SEC_HEAD > file.length) bad("section");
    const type = file[at], coding = file[at + 1], arg = file[at + 2] | file[at + 3] << 8;
    const stored = u32(file, at + 4), n = u32(file, at + 8), crc = u32(file, at + 12), kit = type === T_KIT;
    if (type !== (i ? T_KIT : T_PROJECT) || coding > (kit ? 2 : 1) || (kit && coding === 1) || stored > file.length - at - SEC_HEAD ||
        (kit ? arg < KIT_FIRST || arg >= KIT_FIRST + KIT_COUNT || n > KIT1_MAX || (kits.length && arg <= kits[kits.length - 1].kit) : arg || n !== REC_BYTES) ||
        (coding ? stored >= n : stored !== n)) bad("section");
    const src = file.subarray(at + SEC_HEAD, at + SEC_HEAD + stored);
    let bytes;
    try { bytes = coding ? cmDecode(src, n, kit ? "kit1" : false) : Uint8Array.from(src); } catch { bad("data"); }
    if (bkCrc(bytes) !== crc) bad("data checksum");
    if (kit) kits.push({ kit: arg, bytes }); else project = bytes;
    at += SEC_HEAD + stored; decoded += n;
  }
  if (at !== file.length || decoded !== u32(file, 16)) bad("length");
  return { project, kits };
}

/* the record as project 1 (id 2) and each kit as its own slot's (92 + KIT - 9), as a 1.x backup holds them */
export async function melConvert(converter, file) {
  const v2 = melUnpackV2(file), id = (kit) => 92 + kit - KIT_FIRST;
  const r = await converter.convert(new Map([[2, v2.project], ...v2.kits.filter((k) => k.bytes.length).map((k) => [id(k.kit), k.bytes])]),
                                    { packed: false });
  if (r.refused.length || !r.objects.get(2)) throw new Error(`Melodee 1.x's records here could not be read (${r.refused.join(", ") || "the project"})`);
  const empty = new Uint8Array(0), project = r.objects.get(2);
  const kits = v2.kits.map((k) => ({ kit: k.kit, bytes: k.bytes.length ? r.objects.get(id(k.kit)) ?? empty : empty }));
  return { project, kits, name: recInfo(project).name, report: r.report };
}
