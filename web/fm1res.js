// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The resource region (firmware/src/res.c): Rubik, the factory sounds and the radio program, written after an
// update through editor command 82. The file is build/melodee-res.bin: a 32-byte header, then the payload.
// Errors carry a code for the installer's words: resfile (not a whole resource file), resnocmd (the firmware has no
// command 82), resbuild (another build's), resbusy (playing or Bluetooth on), resroom (begin could not move old preset
// banks out, or erase), reswrite, rescheck.
import { bkPack } from "./fm1backup.js";

export const RES_COMMAND = 82;
const r35 = (a, at) => (a[at] | a[at + 1] << 7 | a[at + 2] << 14 | a[at + 3] << 21 | a[at + 4] << 28) >>> 0;
const resError = (code, msg, rc) => Object.assign(new Error(msg), { code, rc });

// the header: this build's payload length and CRC-32
export function resHeader(region) {
  if (!region || region.length < 32) throw resError("resfile", "Not a Melodee resource file");
  const head = new DataView(region.buffer, region.byteOffset, 32);
  if (head.getUint32(0, true) !== 0x3153524d) throw resError("resfile", "Not a Melodee resource file");
  return { length: head.getUint32(4, true), crc: head.getUint32(8, true) };
}

export function resStatus(reply) {
  if (!reply || reply.length !== 12) throw resError("resnocmd", "This firmware has no resource command");
  return { rc: reply[0], installed: !!reply[1], length: r35(reply, 2), crc: r35(reply, 7) };
}

// request([cmd, args], {timeout}) -> reply bytes (fm1backup.js BackupConnection). Resolves "current" when the
// device already has these resources, "written" after a verified write.
export async function installResources(request, region, onProgress = () => {}) {
  const { length, crc } = resHeader(region), payload = region.subarray(32, 32 + length);
  if (payload.length !== length) throw resError("resfile", "The resource file is cut short");
  const ask = async (args, timeout = 3000) => resStatus(await request([RES_COMMAND, args], { timeout }));
  const now = await ask([0]);
  if (now.installed && now.crc === crc) return "current";
  if (now.length !== length || now.crc !== crc) throw resError("resbuild", "These resources belong to another firmware build");
  const begin = await ask([1], 30000);                 /* old preset banks moved out, the region erased */
  if (begin.rc === 4) throw resError("resbusy", "Stop playback and switch Bluetooth off, then retry", 4);
  if (begin.rc === 3) throw resError("resroom", "The FM-1 could not clear the region (3)", 3);
  if (begin.rc) throw resError("reswrite", `Resource write refused (${begin.rc})`, begin.rc);
  for (let off = 0; off < length; off += 256) {
    const r = await ask([2, off & 127, off >> 7 & 127, off >> 14 & 127, ...bkPack(payload.subarray(off, off + 256))]);
    if (r.rc) throw resError("reswrite", `Resource write failed at ${off} (${r.rc})`, r.rc);
    onProgress(Math.min(length, off + 256), length);
  }
  const end = await ask([3], 10000);
  if (end.rc || !end.installed) throw resError("rescheck", `Resource check failed (${end.rc})`, end.rc);
  return "written";
}
