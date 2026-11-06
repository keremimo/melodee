// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// mel_file.js (the .mel project file) against the firmware's own records (tests/mel_fixture.c emit ->
// build/host/mel): the page's layout table == the firmware's, the compact form both ways, the coder, files refused
// when anything is off, every place a project names a kit renamed as the firmware's decoded state has it, export and
// import against a simulated device (identical kit reused, free slot taken, no room refused, nothing left half
// written). Writes what an import sent (out-*.bin) for mel_fixture verify, and prints the sizes.
import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc } from "./fm1backup.js";
import { MEL_CONST, MEL_MAX, recLayout, cmEncode, cmDecode, lzPack, lzUnpack, pjPacked, pjCompact, pjExpand, recInfo, recRemap,
  melPack, melUnpack, melExport, melImport, melProjectId } from "./mel_file.js";

const DIR = join(dirname(fileURLToPath(import.meta.url)), "../build/host/mel");
if (!existsSync(join(DIR, "layout.json"))) { console.log("mel: no build/host/mel (tests/mel_fixture.c emit)                        skip"); process.exit(0); }
let fails = 0;
const ok = (c, what) => { console.log(`${("mel: " + what).padEnd(100)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const says = (f, re) => { try { f(); return false; } catch (e) { return re.test(e.message); } };
const asays = async (f, re) => { try { await f(); return false; } catch (e) { return re.test(e.message); } };
const read = (name) => new Uint8Array(readFileSync(join(DIR, name)));
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const u32 = (b, at) => (b[at] | b[at + 1] << 8 | b[at + 2] << 16 | b[at + 3] << 24) >>> 0;
const put32 = (b, at, v) => { b[at] = v; b[at + 1] = v >>> 8; b[at + 2] = v >>> 16; b[at + 3] = v >>> 24; };

// ------------------------------------------------------------------------------ the firmware's layout ---
const fw = JSON.parse(readFileSync(join(DIR, "layout.json"), "utf8"));
ok(Object.keys(MEL_CONST).every((k) => MEL_CONST[k] === fw[k]) && melProjectId(31) === 91 && melProjectId(4) === 64 && fw.PROJECTS === 32,
  "the page's record constants are the firmware's");
ok(Object.keys(fw.layout).every((k) => recLayout(fw.layout.np)[k] === fw.layout[k]), "where a record keeps its tracks, sounds and automation: the firmware's");

// ------------------------------------------------------------------------------------------ the coder ---
const projects = ["blank", "fun19", "fun20", "fun21", "sketch", "song", "big", "kits"];
const kitNames = ["909", "808", "cr78"], kit = Object.fromEntries(kitNames.map((n) => [n, read(`kit-${n}.bin`)]));
const rec = Object.fromEntries([...projects, "kits-moved", "kits-p", "kits-p-moved"].map((n) => [n, read(`${n}.rec`)]));
const SECTOR = 4056, sectors = (n) => Math.ceil(n / SECTOR);   // a pool record's payload a sector (storage.c ST_LIN_PAYLOAD_MAX)
ok(projects.every((n) => u32(read(`${n}.hpj`), 0) === fw.PZ_MAGIC && same(pjExpand(read(`${n}.hpj`)), rec[n])), "a project as the firmware stores it (packed) expands to its whole record");
ok(projects.every((n) => same(pjExpand(pjPacked(rec[n])), rec[n]) && pjPacked(rec[n]).length <= read(`${n}.hpj`).length && same(pjExpand(pjCompact(rec[n])), rec[n])),
  "a record packed by the page (never larger than the firmware's), and in the compact form of before, expands to itself");
{
  const lcg = (seed) => () => (seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) >>> 24;
  const r = lcg(11), noise = Uint8Array.from({ length: 5000 }, r);
  let good = true;
  for (let c = 0; c < 300; c++) {
    const n = c < 140 ? c : c * 31, d = Uint8Array.from({ length: n }, (_, i) => (c % 4 === 0 ? 0 : c % 4 === 1 ? noise[i % 5000] : c % 4 === 2 ? i % (c % 130 + 1) : noise[(i / 131) | 0] & 3));
    const e = lzPack(d);
    good &&= same(lzUnpack(e, n), d) && e.length <= n + Math.ceil(n / 128);
  }
  const zeros = lzPack(new Uint8Array(1000));
  ok(good && zeros.length <= 30, "the device's packing, by the page: every length, zeros, noise, runs and repeats come back");
  const bad = (f) => says(f, /Damaged/);
  ok(bad(() => lzUnpack(Uint8Array.of(0x80, 0, 0), 3)) && bad(() => lzUnpack(Uint8Array.of(0, 7, 0x80, 1, 0), 4)) && bad(() => lzUnpack(Uint8Array.of(0, 7, 0x85, 0, 0), 4)) &&
     bad(() => lzUnpack(Uint8Array.of(3, 1, 2), 4)) && bad(() => lzUnpack(Uint8Array.of(0, 7, 0), 1)) && bad(() => lzUnpack(Uint8Array.of(0, 7), 2)) && bad(() => lzUnpack(Uint8Array.of(0, 7, 0x80, 0), 4)) &&
     same(lzUnpack(Uint8Array.of(0, 7, 0x80, 0, 0), 4), [7, 7, 7, 7]),
    "packed bytes that copy from before the block, run past it, end early or late are refused");
  const p = read("song.hpj"), field = (f, w, v) => { const g = Uint8Array.from(f); put32(g, 16 + 4 * w, v); return g; };
  ok(bad(() => pjExpand(field(p, 0, u32(p, 16) - 1))) && bad(() => pjExpand(field(p, 0, u32(p, 16) + 1))) && bad(() => pjExpand(field(p, 0, (u32(p, 16) + 0x80000000) >>> 0))) &&
     bad(() => pjExpand(field(p, 1, 0x7fffffff))) && says(() => recInfo(pjExpand(Uint8Array.from(p, (v, i) => (i === 12 ? v ^ 1 : v)))), /Not a Melodee project record/),
    "a packed project whose head does not match its blocks, or whose sum is off, is refused");
}
ok(same(pjExpand(rec.song), rec.song) && says(() => pjExpand(rec.song.subarray(0, 13212)), /older format/) &&
   says(() => pjExpand(read("song.hpj").subarray(0, 5000)), /Damaged/) && says(() => pjExpand(Uint8Array.of(...read("song.hpj"), 0)), /Damaged/),
  "a raw record is taken as it is; an older form, a short or a long one are refused");
{
  const lcg = (seed) => () => (seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) >>> 24;
  const r = lcg(5), noise = Uint8Array.from({ length: 3000 }, r), text = Uint8Array.from({ length: 2000 }, (_, i) => (i * 7 + (i >> 4)) & 63);
  let good = true;
  for (const d of [new Uint8Array(0), Uint8Array.of(7), noise.subarray(0, 5), noise, text, new Uint8Array(4000), rec.song, rec.big])
    for (const k of [false, true]) { const e = cmEncode(d, k); good &&= same(cmDecode(e, d.length, k), d); }
  for (const n of [615, 616, 617, 623, 624, 625, 1000]) { const d = kit["909"].subarray(0, n), e = cmEncode(d, true); good &&= same(cmDecode(e, n, true), d); }
  for (const n of kitNames) good &&= same(cmDecode(cmEncode(kit[n], true), kit[n].length, true), kit[n]);
  ok(good, "the coder gives back every byte: nothing, one byte, noise, zeros, records, kits cut anywhere");
  // the coder is the file's format: these bytes may never change (a file written today must open in every later page)
  const slices = Uint8Array.from({ length: 2000 }, (_, i) => (i < 616 ? i & 31 : (r() & r()) | (i & 7 ? 0 : 192)));   // (a kit's shape: its head, 4-bit slices)
  const pins = [bkCrc(cmEncode(text)), bkCrc(cmEncode(slices, true)), bkCrc(cmEncode(noise.subarray(0, 600)))];
  ok(same(pins, [0x36C2230, 0x35750584, 0x438E034C]), "the coder writes the same bytes as when the format was set");
  if (process.env.MEL_PIN) console.log("  pins:", pins.map((v) => "0x" + v.toString(16).toUpperCase()).join(", "));
  const e = cmEncode(rec.song);
  ok(says(() => cmDecode(e.subarray(0, e.length - 1), rec.song.length), /Damaged/) && says(() => cmDecode(Uint8Array.of(...e, 0), rec.song.length), /Damaged/) &&
     says(() => cmDecode(new Uint8Array(2), 10), /Damaged/), "coded bytes cut short or with more after them are refused");
}

// ------------------------------------------------------------------------- a project's kits, renamed ---
const info = recInfo(rec.kits), moved = new Map([[10, 12], [13, 9], [16, 15]]);
ok(same(info.kits, [10, 13, 16]) && info.refs.length === 7 && info.name === "KIT SONG" && info.np === fw.layout.np,
  "a project's kits: the drum tracks', a section's and a pattern's own sound's, automation's (7 places, 3 kits)");
ok(recInfo(rec.song).kits.length === 0 && recInfo(rec.blank).kits.length === 0 && recInfo(rec.fun19).kits.length === 0, "the built-in kits (808, 909, ..) are no file's business");
ok(same(recRemap(rec.kits, moved), rec["kits-moved"]), "kits renamed by the page == the firmware's record of the project with them renamed (FBKQ)");
ok(same(recInfo(rec["kits-p"]).kits, [10, 13, 16]) && same(recRemap(rec["kits-p"], moved), rec["kits-p-moved"]), ".. without a pattern's own sound too (FBKP)");
ok(same(recRemap(rec.kits, new Map()), rec.kits) && same(recRemap(recRemap(rec.kits, moved), new Map([[12, 10], [9, 13], [15, 16]])), rec.kits),
  "no change leaves every byte; renamed and back is the record again");
ok(same(recInfo(recRemap(rec.kits, new Map([[10, 13], [13, 10]]))).kits, [10, 13, 16]) && !same(recRemap(rec.kits, new Map([[10, 13], [13, 10]])), rec.kits),
  "two kits swap in one pass");
{
  const bad = Uint8Array.from(rec.kits); bad[5000] ^= 1;
  const old = Uint8Array.from(rec.kits); old[8] ^= 1; put32(old, old.length - 4, 0); // (a FUN magic of before)
  ok(says(() => recInfo(bad), /Not a Melodee project record/) && says(() => recInfo(rec.kits.subarray(0, 27000)), /Not a Melodee/) &&
     says(() => recRemap(rec.kits, new Map([[10, 5]])), /Invalid kit/), "a record whose sum is off, a short one, a rename to a built-in kit: refused");
}

// ------------------------------------------------------------------------------------------- the file ---
const kitsOf = (a, b) => [{ kit: 10, bytes: a }, { kit: 13, bytes: b }, { kit: 16, bytes: new Uint8Array(0) }];
const mel = melPack({ project: rec.kits, kits: kitsOf(kit["909"], kit["808"]) });
{
  const back = melUnpack(mel);
  ok(same(back.project, rec.kits) && back.kits.length === 3 && same(back.kits[0].bytes, kit["909"]) && same(back.kits[1].bytes, kit["808"]) &&
     back.kits[2].kit === 16 && !back.kits[2].bytes.length && back.name === "KIT SONG" && mel.length <= MEL_MAX, "a file holds the project and its kits, an empty kit named as empty");
  ok(same(melUnpack(melPack({ project: rec.song })).project, rec.song) && says(() => melPack({ project: rec.song, kits: kitsOf(kit["909"], kit["808"]) }), /kits must be/) &&
     says(() => melPack({ project: rec.kits, kits: kitsOf(kit["909"], kit["808"]).slice(0, 2) }), /kits must be/), "a file's kits are exactly the ones its project names");
  // a file sealed again after a change inside: its own sums right, so the checks behind them answer
  const seal = (f) => { put32(f, 24, bkCrc(f.subarray(32))); put32(f, 28, bkCrc(f.subarray(0, 28))); return f; };
  const with_ = (fn) => { const f = Uint8Array.from(mel); fn(f); return seal(f); };
  const refused = (f, re = /Not a Melodee project file/) => says(() => melUnpack(f), re);
  let cut = true;
  for (let n = 0; n < mel.length; n += Math.max(1, mel.length >> 6)) cut &&= refused(mel.subarray(0, n));
  ok(cut && refused(new Uint8Array(0)) && refused(Uint8Array.of(...mel, 0)), "a file cut anywhere, empty, or with bytes after it is refused");
  let flips = true;
  for (let i = 0; i < 32 + 16; i++) flips &&= says(() => melUnpack(Uint8Array.from(mel, (v, j) => (j === i ? v ^ 4 : v))), /./);
  for (let i = 48; i < mel.length; i += 997) flips &&= refused(Uint8Array.from(mel, (v, j) => (j === i ? v ^ 4 : v)));
  ok(flips, "one bit changed anywhere is refused");
  const k1 = 32 + 16 + u32(mel, 32 + 4);   // the first kit's section
  ok(refused(with_((f) => { f[4] = 3; }), /newer editor/) && refused(with_((f) => { f[8] = 1; })) && refused(with_((f) => { f[20] = 2; })) &&
     refused(with_((f) => { f[20] = 0; })) && refused(with_((f) => { f[20] = 10; })) && refused(with_((f) => { put32(f, 16, 5); })),
    "a later version, unknown flags, a wrong section count or total: refused");
  ok(refused(with_((f) => { f[32] = 2; })) && refused(with_((f) => { f[33] = 2; })) && refused(with_((f) => { f[34] = 1; })) &&
     refused(with_((f) => { put32(f, 32 + 8, 27903); })) && refused(with_((f) => { put32(f, 32 + 4, 0x7fffffff); })) && refused(with_((f) => { f[32 + 12] ^= 1; })) &&
     refused(with_((f) => { f[k1] = 1; })) && refused(with_((f) => { f[k1 + 1] = 1; })) && refused(with_((f) => { f[k1 + 1] = 3; })) && refused(with_((f) => { f[k1 + 2] = 8; })) &&
     refused(with_((f) => { f[k1 + 2] = 11; })) && refused(with_((f) => { f[k1 + 2] = 14; })) && refused(with_((f) => { put32(f, k1 + 8, 28400); })) &&
     refused(with_((f) => { f[k1 + 16 + 40] ^= 1; })), "a section of the wrong kind, coding, kit, length or sum, coded bytes changed: refused");
  // sections written here, any way wanted
  const file = (sections) => {
    const n = sections.reduce((a, s) => a + 16 + s.bytes.length, 32), f = new Uint8Array(n);
    f.set([0x4D, 0x45, 0x4C, 0, 2, 0, 32, 0]); put32(f, 12, n); put32(f, 16, sections.reduce((a, s) => a + s.bytes.length, 0)); f[20] = sections.length;
    let at = 32;
    for (const s of sections) { f[at] = s.type; f[at + 2] = s.arg || 0; put32(f, at + 4, s.bytes.length); put32(f, at + 8, s.bytes.length); put32(f, at + 12, bkCrc(s.bytes)); f.set(s.bytes, at + 16); at += 16 + s.bytes.length; }
    return seal(f);
  };
  const P = { type: 1, bytes: rec.kits }, K = (n, bytes = kit.cr78) => ({ type: 2, arg: n, bytes }), none = new Uint8Array(0);
  const wrongKit = Uint8Array.from(kit.cr78); wrongKit[0] ^= 1;
  const tornProject = Uint8Array.from(rec.kits); tornProject[9000] ^= 1;
  ok(same(melUnpack(file([P, K(10), K(13), K(16, none)])).kits[1].bytes, kit.cr78) && refused(file([P, K(10), K(13)])) && refused(file([P, K(13), K(10), K(16, none)])) &&
     refused(file([P, K(10), K(10), K(16, none)])) && refused(file([P, K(10), K(13), K(16, none), K(16, none)])) && refused(file([K(10), P])) && refused(file([P, P])) &&
     refused(file([{ type: 1, bytes: rec.song }, K(10)])) && says(() => melUnpack(file([P, K(10, wrongKit), K(13), K(16, none)])), /Invalid drum kit/) &&
     says(() => melUnpack(file([P, K(10, kit.cr78.subarray(0, 700)), K(13), K(16, none)])), /Invalid drum kit/) &&
     says(() => melUnpack(file([{ type: 1, bytes: tornProject }, K(10), K(13), K(16, none)])), /Not a Melodee project record/),
    "kits missing, twice, out of order, not the project's, not kits; a project whose own sum is off: refused");
}

// ---------------------------------------------------------------------------------------- the device ---
const IDS57 = [...Array.from({ length: 29 }, (_, i) => i), ...Array.from({ length: 28 }, (_, i) => 64 + i)], IDS65 = [...IDS57, ...Array.from({ length: 8 }, (_, i) => 92 + i)];
function device(objs, opt = {}) {
  const d = { objs: new Map(objs), log: [], staged: null, gets: 0 };
  const ids = opt.ids || IDS65;
  d.request = async ([cmd, a]) => {
    if (cmd === BACKUP_CMD.LIST) {
      if (opt.playing) return [1, 3, 0];
      const out = [1, 0, ids.length];
      for (const id of ids) { const v = d.objs.get(id) || new Uint8Array(0); out.push(id, ...bkU32(v.length), ...bkU32(v.length ? bkCrc(v) : 0)); }
      return out;
    }
    if (cmd === BACKUP_CMD.GET) {
      const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = d.objs.get(id);
      d.gets++;
      return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))];
    }
    if (cmd === BACKUP_CMD.PUT) {
      const [op, id] = a;
      if (op === 0) { d.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, opt.refuse?.(id, d.staged.size) || 0]; }
      if (op === 1) { d.staged.bytes.push(...bkUnpack(a.slice(7), Math.min(256, d.staged.size - bkR32(a, 2)))); return [op, id, 0]; }
      if (op === 2) {
        const s = Uint8Array.from(d.staged.bytes), old = opt.packed === false && s.length > 4 && u32(s, 0) === fw.PZ_MAGIC;
        const rc = opt.failCommit === id || old ? 2 : !s.length || bkCrc(s) === d.staged.crc ? 0 : 2;
        if (!rc) { if (s.length) d.objs.set(id, s); else d.objs.delete(id); d.log.push(s.length ? id : -id); }
        return [op, id, rc];
      }
      if (op === 3) { d.log.push(`abort ${id}`); return [op, id, 0]; }
    }
    throw new Error(`unexpected ${cmd}`);
  };
  return d;
}
const other = (n) => { const b = Uint8Array.from(kit.cr78); b[8] = 65 + n; return b; };   // (a kit of another name)
const stored = read("kits.hpj"), live = read("song.hpj");
// the device the project comes from: project 7, KIT 2 the 909 take, KIT 5 the 808's, KIT 8 empty
const source = device([[0, live], [66, stored], [93, kit["909"]], [96, kit["808"]]]);
const out = await melExport(source.request, 66);
ok(same(out.file, mel) && out.name === "KIT SONG" && same(out.kits, [10, 13]) && !source.log.length, "export: a saved project and the two kits it plays, the device untouched");
{
  const liveOut = await melExport(source.request, 0);
  ok(same(melUnpack(liveOut.file).project, rec.song) && !liveOut.kits.length, "export: the music as it is now, no kit for a project that plays none");
  ok(await asays(() => melExport(source.request, 67), /empty/) && await asays(() => melExport(device([[66, stored]], { playing: true }).request, 66), /Stop playback/),
    "export: an empty project, a playing device: said so");
}
// the device it goes to: projects 1..6 taken; KIT 1 the same 808 take, KIT 4 and KIT 7 free, the rest other kits
const targetObjs = () => [[2, live], [3, live], [4, live], [5, live], [64, live], [65, live], [92, kit["808"]], [93, other(1)], [94, other(2)], [96, other(3)], [97, other(4)], [99, other(5)]];
{
  const target = device(targetObjs());
  let last = 0, steps = 0, mono = true;
  const r = await melImport(target.request, mel, (done, total) => { mono &&= done >= last && done <= total; last = done; steps++; });
  ok(r.slot === 6 && r.name === "KIT SONG" && same(r.empty, [16]) && r.kits.length === 2 &&
     r.kits.some((k) => k.from === 13 && k.to === 9 && k.reused) && r.kits.some((k) => k.from === 10 && k.to === 12 && !k.reused),
    "import: the kit the device has already (every byte) is used where it is, the other takes the first free slot");
  ok(same(target.log, [95, 66]) && same(target.objs.get(95), kit["909"]) && u32(target.objs.get(66), 0) === fw.PZ_MAGIC && same(pjExpand(target.objs.get(66)), rec["kits-moved"]) &&
     sectors(target.objs.get(66).length) === 1 && target.objs.size === 14 && mono && steps > 100,
    "import: one kit written, then the project (packed, one sector) into the first free one, naming its kits where they are now");
  writeFileSync(join(DIR, "out-kit-12.bin"), target.objs.get(95));
  writeFileSync(join(DIR, "out-project.bin"), target.objs.get(66));
  const before = device(targetObjs(), { packed: false });   // firmware that reads the compact form only
  let over = false;
  const rb = await melImport(before.request, mel, (done, total) => { over ||= done > total; });
  ok(rb.slot === 6 && same(before.log, [95, "abort 66", 66]) && u32(before.objs.get(66), 0) === fw.PJ_MAGIC && same(pjExpand(before.objs.get(66)), rec["kits-moved"]) && !over,
    "import: firmware of before the packed form refuses it and gets the compact one");
  writeFileSync(join(DIR, "out-project-compact.bin"), before.objs.get(66));
  const again = await melImport(target.request, mel);
  ok(again.slot === 7 && again.kits.every((k) => k.reused) && same(target.log, [95, 66, 67]) && same(target.objs.get(67), target.objs.get(66)),
    "import: the same file again writes no kit");
  const round = await melExport(target.request, 66), back = melUnpack(round.file);
  ok(same(back.kits.map((k) => k.kit), [9, 12, 15]) && same(back.kits[0].bytes, kit["808"]) && same(back.kits[1].bytes, kit["909"]) && same(back.project, rec["kits-moved"]),
    "import, then export: the project with its kits under their new numbers");
}
{
  const full = device([...targetObjs(), [95, other(6)], [98, other(7)]]);
  ok(await asays(() => melImport(full.request, mel), /No free drum kit: the project brings 1 new kit, the device has room for 0/) && !full.log.length,
    "import: no free kit slot is said, nothing written");
  const one = device([[92, other(0)], [93, other(1)], [94, other(2)], [96, other(3)], [97, other(4)], [98, other(6)], [99, other(5)]]);
  ok(await asays(() => melImport(one.request, mel), /brings 2 new kits, the device has room for 1/) && !one.log.length, "import: room for one of two kits is said, nothing written");
  const noProject = device([...targetObjs(), ...Array.from({ length: 26 }, (_, i) => [66 + i, live])]);
  ok(await asays(() => melImport(noProject.request, mel), /No free project/) && !noProject.log.length, "import: no free project is said, nothing written");
  const fails = device(targetObjs(), { failCommit: 66 });
  ok(await asays(() => melImport(fails.request, mel), /failed validation/) && same(fails.log, [95, "abort 66", "abort 66", -95]) && !fails.objs.has(95) && !fails.objs.has(66),
    "import: a project the device refuses takes its new kit away again");
  const old = device(targetObjs().filter(([id]) => id < 92), { ids: IDS57, packed: false });
  ok(await asays(() => melImport(old.request, mel), /no user drum kits/) && !old.log.length && (await melImport(old.request, melPack({ project: rec.song }))).slot === 6,
    "import: firmware of before the user kits takes a project without kits only");
  ok(await asays(() => melImport(device([], { ids: IDS57.slice(0, 29) }).request, mel).catch((e) => { throw new Error(e.message); }), /./), "import: firmware of before the project pool is refused");
  const twins = device([[66, stored], [93, kit["909"]], [96, kit["909"]]]), twinFile = (await melExport(twins.request, 66)).file, empty = device([]);
  const r = await melImport(empty.request, twinFile);
  ok(same(empty.log, [92, 2]) && r.kits.length === 2 && r.kits.every((k) => k.to === 9) && same(recInfo(pjExpand(empty.objs.get(2))).kits, [9, 10]),
    "import: a kit the file holds twice goes in once; the empty kit's name moves to an empty slot");
  ok(await asays(() => melImport(empty.request, mel.subarray(0, 9000)), /Not a Melodee project file/) && same(empty.log, [92, 2]), "import: a damaged file touches nothing");
}

// ------------------------------------------------------------------------------------------- the sizes ---
console.log("\nmel: bytes (pool sectors)        stored before     stored now      .mel file");
const pad = (v, n) => String(v).padStart(n);
for (const n of projects) {
  const c = pjCompact(rec[n]).length, d = read(`${n}.hpj`).length, m = melPack({ project: rec[n], kits: recInfo(rec[n]).kits.map((k) => ({ kit: k, bytes: new Uint8Array(0) })) }).length;
  console.log(`mel:   project ${n.padEnd(8)} ${pad(c, 20)} (${sectors(c)}) ${pad(d, 11)} (${sectors(d)}) ${pad(m, 12)}`);
}
for (const n of kitNames) {
  const m = cmEncode(kit[n], true).length + 16;
  console.log(`mel:   kit ${n.padEnd(12)} ${pad(kit[n].length, 20)} (${sectors(kit[n].length)}) ${pad(kit[n].length, 11)} (${sectors(kit[n].length)}) ${pad(m, 12)}`);
}
console.log(`mel:   the kit project and its two kits as one file ${pad(mel.length, 37)}\n`);

if (fails) { console.log(`mel: ${fails} FAILED`); process.exit(1); }
console.log("mel: all checks ok");
