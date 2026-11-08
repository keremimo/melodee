// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Node checks of the web pages' JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - editor.html: the protocol section (between PROTO-BEGIN/END) against its mock device (v1 commands,
//   the user preset bank / librarian, library files, live pushes, older-firmware fallback, the v3 tracks
//   and the mixer, the v5 drum grid in steps and user presets), its tab layout and ja/en strings,
//   and the user-sample pipeline byte for byte against tools/sampleio.py
// - fm1pkg.js: productOf and logicalImage on build/melodee.fwsc (skipped without a build)
// - fm1ota.js: a full install and an unplug during the write against a simulated FM-1

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import vm from "node:vm";
import {mockInstallSession,strip} from "./installer_test_helpers.mjs";
import { blockCount, logicalImage, productOf, validateStockPackage } from "./fm1pkg.js";
import { HS_FULL, MELODEE_LOADER, STOCK_LOADER, Updater, melodeeLoader, pack7, unpack7 } from "./fm1ota.js";
import { progressModel } from "./install/progress.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(64)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const py = (code, ...args) => execFileSync("python3", ["-c", code, ...args], { maxBuffer: 1 << 26 });
const HERE = new URL(".", import.meta.url).pathname;

/* ------------------------------------------------------------ editor protocol --- */
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + `
;({ frame, unframe, parse, req, Link, MODS, parseWav, resample, normalize, takeSample, autoTrim, zoomView, rootFromName, buildSlot, makeMockDevice, CMD, SMP,
   UP, bank, capturePatch, auditionPatch, startWatch, libraryFile, readLibraryFile, paramKeys, patternFromSteps, stepsFromPattern, upName,
   mixer, parseNotes, parseHits, hitsText, gridFromSteps, LANE_NOTE, LANE_OF, isDevicePort, readDevicePreferences, devicePresetRows, engineOrder, ENGINE_ORDER, aliasOf, fmtValue, F,
   readSysexLibrary, scaleRows, SCALE_INFO, SYNTH, synthSchema, sendSysex, bankDump })`,
{ setTimeout, clearTimeout, setInterval, clearInterval, console });
/* the synths' schemas, byte for byte as the firmware's SCHEMA sends them (tests/descdump.c -> build/host/desc.json) */
const DESC_JSON = join(HERE, "../build/host/desc.json");
const SCHEMAS = existsSync(DESC_JSON) ? JSON.parse(readFileSync(DESC_JSON, "utf8")).SCHEMAS || null : null;
/* the firmware's synths by handle (desc.json's ENG: the schemas' keys) */
const FW_NAMES = SCHEMAS ? (JSON.parse(readFileSync(DESC_JSON, "utf8")).ENG || []).map((e) => e.name) : [];
const KIT_NAMES = ["808", "909", "SID KIT", "CR78", "FM KIT", "SAMPLER"];   /* (2.0: DRUM's kits, synths of their own) */
/* Melodee 2.0: engine numbers are a session's handles (0 none, 1 the MIDI Track, the shipped synths, then modules), never
   stored: the checks look them up by name, in INFO where they have one (H), else in the mock's own table (HN) */
const engineHandles = (info) => (name) => info.engines.indexOf(name);
const MOCK_NAMES = (() => { const m = E.makeMockDevice({ auto: false }); m.stop(); return m.tables.ENG.map((e) => e.name); })();
const HN = (name) => MOCK_NAMES.indexOf(name);

ok(["FM-1", "FM-1 MIDI 1", "Melodee", "Felucca"].every(name => E.isDevicePort({name})) &&
   !E.isDevicePort({name:"Other MIDI"}) && !E.isDevicePort(null),
   "editor: discover and reconnect FM-1 and older firmware MIDI ports");

{
  const mock=E.makeMockDevice({shared:true}),input=[...mock.access.inputs.values()][0],output=[...mock.access.outputs.values()][0];
  const link=new E.Link(d=>output.send(d),{timeout:500});input.onmidimessage=e=>link.receive(e.data);const rq=(r,o)=>link.request(r,o),info=E.parse[E.CMD.INFO](await rq(E.req.info()));
  const H=engineHandles(info),P5=H("PROPHET"),CZ=H("CZ-1"),DR=H("808");
  ok(info.shared?.total===384,"shared: one 384-slot capability replaces the engine pools");
  let all=await E.bank.list(rq,info);ok(all.total===384&&all.slots.at(-1).slot===383,"shared: pagination reaches slot U384 without wrapping");
  const raw=Array.from({length:138},(_,i)=>(i*37)&255);
  await E.bank.put(rq,383,{engine:P5,name:"TWENTY CHAR PROGRAM",native:raw},info);
  let got=await E.bank.get(rq,info,383);ok(got.used&&got.engine===P5&&eq(got.native,raw)&&got.name==="TWENTY CHAR PROGRAM","shared: highest slot preserves a synth's whole patch and its name");
  const cz=Array.from({length:144},(_,i)=>(255-i)&255);await E.bank.put(rq,383,{engine:CZ,name:"CZ IN SAME SLOT",native:cz},info);
  got=await E.bank.get(rq,info,383);ok(got.engine===CZ&&eq(got.native,cz),"shared: CZ replaces Prophet at the same absolute slot");
  await E.bank.load(rq,383,info,2);const status=E.parse[E.CMD.TRACK](await rq(E.req.track()));ok(mock.state.tracks[2].user===384,"shared: load preserves the full slot identity on any track");
  await E.bank.erase(rq,383,info);ok(!(await E.bank.get(rq,info,383)).used,"shared: erasing U384 clears the shared slot");
  const opaque=cz.slice();opaque[128]=0;opaque[143]=255;
  await E.bank.put(rq,381,{engine:CZ,name:"OPAQUE",native:opaque},info);const exact=await E.bank.get(rq,info,381);
  ok(eq(exact.native,opaque),"shared: incoming native name/control bytes remain byte-exact");
  const drum=Array(54).fill(0);await E.bank.put(rq,382,{engine:DR,name:"DRUM PATCH",drum,p:Array(info.pcount).fill(0)},info);
  const kit=await E.bank.get(rq,info,382);ok(eq(kit.drum,drum),"shared: drum patches survive bank export and import");
  let writes=0;try{await E.bank.put(async()=>{writes++;},383,{engine:P5,native:[1,2,256]},info);}catch{}ok(!writes,"shared: malformed native voices fail before sending a write");
  const named={state:{caps:8,filter:0},slots:await E.bank.list(rq,info),favorites:[]};const names=info.engines.map(()=>[]);names[CZ]=["CZ FACTORY"];
  const grouped=E.devicePresetRows(info,names,named),factoryAt=grouped.findIndex(r=>!r.user&&r.engine===CZ),czAt=grouped.findIndex(r=>r.user&&r.actualEngine===CZ);
  ok(factoryAt>=0&&czAt===factoryAt+1,"shared: tone picker groups user slots under their actual engine");
  const factoryNames=mock.tables.ENG.map(engine=>engine.presets.map(preset=>preset.name));
  ok(info.midi === 1 && info.engines[info.midi] === "MIDI Track", "shared: eight-byte INFO name restores MIDI Track's UI identity (its handle from INFO's 0x49 tag)");
  const wireInfo = {...info, engines: info.engines.map((name) => name.slice(0, 8))};
  const prefs=await E.readDevicePreferences(rq,wireInfo,factoryNames);
  ok(prefs.slots.total===384 && prefs.favorites[info.namespace.general].length===384 && prefs.favorites[P5].length===factoryNames[P5].length,
     "shared: 1.5 startup reads the complete factory and shared-slot favorites");
  const midi=E.devicePresetRows(info,factoryNames,prefs).find(row=>row.engine===info.midi);
  ok(midi && midi.canFavorite===false && !prefs.favorites[info.midi],
     "shared: MIDI Track remains loadable without unsupported favorites");
  ok(E.devicePresetRows(info,factoryNames,prefs).filter(row=>row.engine!==info.midi).every(row=>row.canFavorite),
     "shared: factory sounds and saved slots retain their star controls");
  link.close();
}

{
  const d = {min: 0, max: E.SCALE_INFO.length - 1, names: E.SCALE_INFO.map(r => r.name)};
  ok(E.scaleRows(d).length === 60, "scale picker: every scale but the entries that became tunings");
  ok(E.scaleRows(d, "All scales", "", [], d.names.indexOf("WERCK3")).length === 61, "scale picker: a retired entry listed while selected");
  ok(E.scaleRows({min: 0, max: 69, names: d.names.slice(0, 70).map(n => n === "BAYAT-D" ? "BAYATI" : n)}).length === 70, "scale picker: older firmware keeps every entry its own");
  ok(E.scaleRows(d, "Equal divisions").length === 24, "scale picker: family filtering");
  ok(E.scaleRows(d, "All scales", "pentatonic")[0]?.name === "PEN", "scale picker: readable names searchable");
  ok(E.scaleRows(d, "All scales", "53EDO")[0]?.count === 53, "scale picker: short names and note counts searchable");
  ok(E.scaleRows(d, "Favorites", "", ["BP-JI", "24EDO"]).map(r => r.name).join() === "24EDO,BP-JI", "scale picker: favorites retain catalogue IDs");
  ok(E.scaleRows(d, "Favorites").length === 0 && E.scaleRows(d, "All scales", "missing scale").length === 0, "scale picker: empty favorites and search");
  const old = E.scaleRows({min: 0, max: 1, names: ["MAJ", "UNKNOWN"]});
  ok(old[0].title === "Major" && old[1].title === "UNKNOWN" && old[1].id === 1 && old[1].family === "Other", "scale picker: old firmware and unknown scale fallback");
}

{
  const id = 112 + 134, value = (3 << 8) | 23;
  const [cmd, a] = E.req.motion(0, 3, {step:63,param:id,value});
  const reply = E.parse[E.CMD.MOTION]([0,0,1,1,64,a[2],a[3],a[4],a[5]]);
  ok(cmd === E.CMD.MOTION && a.every(x => x < 128) && reply.events[0].step === 63 &&
    reply.events[0].param === id && reply.events[0].value === value, "native motion: 8-bit parameter and tagged value survive MIDI framing");
  const legacy = E.parse[E.CMD.MOTION]([0,0,1,1,64,12,36,...E.req.motion(0,3,{step:12,param:36,value:80})[1].slice(4)]);
  ok(legacy.events[0].step === 12 && legacy.events[0].param === 36 && legacy.events[0].value === 80,
    "native motion: existing motion replies remain compatible");
}

/* a Prophet program as Melodee 1.x's banks keep it (its INIT PROPHET) */
const P5_INIT = "GBgAAQAAAABAQAAAAQB/AAB/AAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAPDx/fyMjAQAFAAAAAAAAAAAAAABJTklUIFBST1BIRVQgICAgICAgIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIUyAwAA";
async function prophetBackupTests(){
  const B=await import("./fm1backup.js"),raw=new Uint8Array(3600),v=new DataView(raw.buffer);v.setUint32(0,0x31553550,true);v.setUint32(4,1<<23,true);v.setUint32(8,1<<23,true);raw.set(Buffer.from(P5_INIT,"base64"),12+23*138);
  const file={format:"felucca-backup",version:1,objects:Array.from({length:28},(_,id)=>({id,size:0,crc:0,data:""}))};
  for(const id of [0,1]){const bytes=new Uint8Array(id===0?27752:604);file.objects[id]={id,size:bytes.length,crc:B.bkCrc(bytes),data:Buffer.from(bytes).toString("base64")};}
  const set=(bytes)=>file.objects[27]={id:27,size:bytes.length,crc:B.bkCrc(bytes),data:Buffer.from(bytes).toString("base64")};set(raw);
  ok(eq(B.readBackup(file).objects[27].bytes,raw),"Prophet backup: 28-object archive keeps P128 and its star byte for byte");
  let writes=0,rejected=0;for(const field of [0,4,12+23*138+134]){const b=raw.slice();b[field]^=255;set(b);try{await B.restoreBackup(async()=>{writes++;},file);}catch{rejected++;}}
  ok(rejected===3&&!writes,"Prophet backup: malformed bank, masks and program fail before transport");
  set(raw);const manifest=[1,0,23];for(let id=0;id<23;id++)manifest.push(id,...B.bkU32(id===0?27752:0),...B.bkU32(0));let calls=0;
  try{await B.restoreBackup(async([cmd])=>{calls++;if(cmd!==65)throw Error("write reached");return manifest;},file);}catch{}
  ok(calls===1,"Prophet backup: older firmware refuses native banks before the first write");
}
await prophetBackupTests();

{
  if (!SCHEMAS) console.log("Library SysEx (no build/host/desc.json)".padEnd(64) + " skip");
  else {
    E.SYNTH.cache.clear();
    for (const [e, b] of Object.entries(SCHEMAS)) E.SYNTH.cache.set(+e, { key: 0, schema: E.SYNTH.parse(b) });
    const engines = MOCK_NAMES;
    const ctx = { keys: Array.from({ length: 92 }, (_, i) => `P${i}`), pe0: 84, engines };
    const file = readFileSync(join(HERE, "../assets/cz1-factory/cz1-factory.syx")), frames = E.SYNTH.frames(file);
    const cz = E.readSysexLibrary(file, "factory.syx", ctx);
    ok(cz.length === frames.length && cz.length >= 64 && cz.every((p) => p.engine === HN("CZ-1") && p.engineName === "CZ-1"),
       "Library SysEx: a real Casio factory bank imports offline (the schemas' patterns)");
    ok(cz.every((p, i) => { const j = E.readLibraryFile(E.libraryFile("patch", [p], ctx), ctx).patches[0]; return j.engine === HN("CZ-1") && eq(j.sysex, frames[i]); }),
       "Library SysEx: every frame survives JSON export/reimport byte for byte");
    const p5 = E.readSysexLibrary(readFileSync(join(HERE, "../assets/prophet5-factory/prophet5-v1.03.syx")), "prophet.syx", ctx);
    ok(p5.length === 200 && p5.every((p) => p.engine === HN("PROPHET")), "Library SysEx: the real Prophet factory library: 200 PROPHET programs");
    const dx = [0xF0, 0x43, 0x00, 0x09, 0x20, 0x00, ...Array(4096).fill(0), 0, 0xF7];
    ok(E.readSysexLibrary(dx, "bank.syx", ctx)[0].engine === HN("FM6"), "Library SysEx: a Yamaha 32-voice bank belongs to FM6");
    const unknown = E.readSysexLibrary([0xF0, 0x7D, 0, 0xF7], "x.syx", ctx)[0];
    let cutOff = 0; try { E.readSysexLibrary([0xF0, 0x43, 0, 9], "cut.syx", ctx); } catch { cutOff++; }
    ok(unknown.engineName === "" && cutOff === 1, "Library SysEx: an unknown synth's frame kept unassigned, a cut file refused");
    E.SYNTH.cache.clear();
  }
}

async function editorMock() {
  const m = E.makeMockDevice();
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const link = new E.Link((d) => out.send(d), { timeout: 300 });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = async (r, o) => link.request(r, o);
  const info = E.parse[E.CMD.INFO](await rq(E.req.info())), H = engineHandles(info);
  ok(info.nengines === 30 && info.engines.slice(0, 12).join() === "-,MIDI Track,PROPHET,FM6,CZ-1,SID,808,909,SID KIT,CR78,FM KIT,SAMPLER" && info.engines.slice(12).every((n) => n === "-") &&
     info.midi === 1 && info.namespace.general === 0x40 && info.namespace.fm === 0x41 && info.namespace.cz === 0x42 && info.namespace.p5 === 0x43 &&
     info.pcount === 124 && info.pe0 === 116,
    "editor: INFO (2.0 handles: 0 none, 1 MIDI Track, the shipped synths; the 0x49 tag's MIDI handle and user kinds)");
  let descs = 0;
  for (let i = 0; i < info.pcount; i++) if (E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))).label) descs++;
  ok(descs === info.pcount-7, "editor: DESC for every parameter");
  {
    for(const id of [33,34,35,36,45,46,48]){
      const d=E.parse[E.CMD.DESC](await rq(E.req.desc(0,id)));
      const r=E.parse[E.CMD.SET](await rq(E.req.set(0,id,127)));
      ok(!d.label && r.value===0,"editor: removed effect parameter is inert "+id);
    }
  }
  {
    /* the modulation matrix (core.h P_M1SRC..P_M4AMT = 49..60, just before P_E0 61): SRC / DST / AMT x 4, the
       DST names (21: more than 16) over DESC, every slot OFF after a factory preset (ui.c apply_preset_to) */
    const md = [];
    for (let i = 49; i < 61; i++) md.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(md.map((d) => d.label).join() === "SRC1,DST1,AMT1,SRC2,DST2,AMT2,SRC3,DST3,AMT3,SRC4,DST4,AMT4"
      && md[0].names.join() === "OFF,LFO,ENV,VEL,KEY,RAND,MODW,AT,EXPR,S&H,SLEW" && md[1].names.length === 21
      && md[1].names[11] === "VIB" && md[1].names[19] === "E8" && md[1].names[20] === "DEPTH" && md[2].min === -64 && md[2].max === 63 && md.every((d) => d.def === 0),
      "editor: matrix parameters 49..60 over DESC (21 DST names)");
    await rq(E.req.set(0, 52, 1));
    await rq(E.req.set(0, 53, 12));
    await rq(E.req.set(0, 54, -30));
    const on = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(H("SID"), 2));
    const off = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(on.p[52] === 1 && on.p[53] === 12 && on.p[54] === -30 && off.p.slice(49, 61).every((v) => v === 0),
      "editor: a factory preset turns every matrix slot off");
  }
  {
    /* the chord keys (core.h P_CHRD, P_VOIC = 81, 82, just before P_E0 83): the track's, a factory preset keeps them */
    const c0 = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 81)));
    const c1 = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 82)));
    ok(c0.label === "CHRD" && c0.names.join() === "OFF,DIA3,DIA7,MAJ,MIN,DOM7,MAJ7,MIN7,SUS4,POW,AUTO,AUTO2" && c0.def === 0 &&
       c1.label === "VOIC" && c1.names.join() === "CLOSE,OPEN,INV1,INV2,+OCT" && c1.def === 0,
      "editor: the chord keys 81, 82 over DESC");
    for (const mode of [10, 11]) {
      await rq(E.req.set(0, 81, mode));
      await rq(E.req.preset(H("SID"), 3));
      ok(E.parse[E.CMD.DUMP](await rq(E.req.dump()), info).p[81] === mode,
        `editor: appended ${mode === 10 ? "AUTO" : "AUTO2"} chord mode can be selected and survives a sound load`);
    }
    const strum = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 114)));
    const stroke = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 115)));
    ok(strum.label === "STRUM" && strum.names.join() === "OFF,1/8,1/16,1/32,1/64,1/128,16T,32T" &&
       stroke.label === "STROKE" && stroke.names.join() === "DOWN,UP,ALT" && !strum.def && !stroke.def,
       "editor: strum divisions and directions over DESC");
    await rq(E.req.set(0, 114, 4));
    await rq(E.req.set(0, 115, 2));
    await rq(E.req.set(0, 81, 2));
    await rq(E.req.set(0, 82, 4));
    await rq(E.req.preset(H("SID"), 3));
    const kept = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(kept.p[81] === 2 && kept.p[82] === 4 && kept.preset === 3, "editor: a factory preset keeps CHRD and VOIC (the track's)");
    ok(kept.p[114] === 4 && kept.p[115] === 2, "editor: sound loads preserve strum and stroke");
    await rq(E.req.set(0, 81, 0));
    await rq(E.req.set(0, 82, 0));
  }
  ok(E.CMD.SONG === 33 && E.CMD.UI_STATE === 34 && E.CMD.FAV_SET === 38, "editor: preferences preserve the existing SONG command");
  const baseInfo = [116,101,115,116,0, 0, 69, 27, 64, 61, 4];
  const untagged = E.parse[E.CMD.INFO]([...baseInfo, 15]);
  ok(!untagged.chainRows && !untagged.uiCaps, "editor: an untagged PR capability cannot masquerade as SONG or preferences");
  const oldSong = E.parse[E.CMD.INFO]([...baseInfo, 16]);
  ok(oldSong.chainRows === 16 && !oldSong.uiCaps, "editor: existing SONG firmware remains compatible without preference probes");
  const tagged = E.parse[E.CMD.INFO]([...baseInfo, 16, 0x55, 1, 11]);
  ok(tagged.chainRows === 16 && tagged.uiCaps === 11, "editor: tagged preference extension advertises its supported controls");
  const pending = E.parse[E.CMD.FAV_SET]([4, 0, 0, 64, 1]);
  ok(pending.rc === 4 && pending.on && pending.engine === 0, "editor: queued favorite response is applied while waiting for STOP");
  const names = [];
  for (let e = 0; e < info.nengines; e++) names.push(E.parse[E.CMD.NAMES](await rq(E.req.names(e))).names);
  const cut = E.parse[E.CMD.NAMES]([H("CZ-1"), 4, 65, 0, 66, 0, 67]);   /* 0.11: CZ-1's names overran the 600-byte reply */
  ok(cut.names.join() === "A,B,PRESET 3,PRESET 4" && !cut.titles.length, "editor: a NAMES reply cut short keeps its whole names");
  let prefs = await E.readDevicePreferences(rq, info, names);
  ok(info.uiCaps === 121 && prefs.palettes.length === 8 && prefs.palettes[0] === "MONO" && prefs.palettes.includes("HI-CON"),
     "editor: preference capabilities (palette, favorites) and palette names");
  ok(prefs.state.clickLevel===12,"click level reports a percentage (capability bit 6), 12 % (the old MID at MASTER 0.5) by default");
  for(const [id,key,value] of [[4,"click",2],[5,"clickLevel",73],[6,"countin",2],[7,"preview",1],[8,"chordAdd",1]]) {
    const changed=E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(id,value)));
    ok(changed.rc===0 && changed[key]===value,`recording preference ${key} round trip`);
    const invalid=E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(id,id===5?101:id<=6?3:2)));
    ok(invalid.rc===1 && invalid[key]===value,`recording preference ${key} rejects invalid values`);
  }
  ok(!(E.parse[E.CMD.UI_STATE]([25,0,127,127,0,0,0,0,0,0,0,0,0]).caps&16),"recording preferences require a complete state suffix");
  const pal = E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(0, 2)));
  ok(pal.rc === 0 && pal.palette === 2, "editor: display preference 0 (palette) round trip");
  ok(E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(0, 8))).rc === 1, "editor: out-of-range palette refused");
  ok(E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(1, 1))).rc === 2, "editor: the retired font weight answers not supported");
  ok(E.parse[E.CMD.FAV_SET](await rq(E.req.favSet(info.namespace.general, 31, true))).rc === 1, "editor: empty user slot cannot be favorited");
  await rq(E.req.favSet(H("PROPHET"), 0, true));
  await rq(E.req.uiSet(3, 1));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(E.devicePresetRows(info, names, prefs).length === 1 && prefs.favorites[H("PROPHET")][0], "editor: favorites filter follows device state");
  m.state.favorites[H("PROPHET")][0] = false; m.state.favorites[H("SID")][0] = true;
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(!prefs.favorites[H("PROPHET")][0] && prefs.favorites[H("SID")][0], "editor: panel-side favorite changes refresh");
  await rq(E.req.upStore(31, "FAVORITE"));
  await rq(E.req.favSet(info.namespace.general, 31, true));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(E.devicePresetRows(info, names, prefs).some((r) => r.user && r.preset === 31), "editor: saved user slot appears as favorite");
  await rq(E.req.upStore(31, "RENAMED"));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(prefs.favorites[info.namespace.general][31] && prefs.slots.slots[31].name === "RENAMED", "editor: overwrite retains star and refreshes name");
  await rq(E.req.upErase(31));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(!prefs.favorites[info.namespace.general][31] && !E.devicePresetRows(info, names, prefs).some((r) => r.user), "editor: erased slot disappears and loses star");
  {   /* the lists in the device's order: FM6 second, MIDI Track last, "-" never; the handles stay */
    const shown = E.engineOrder(info.engines).map((i) => info.engines[i]);
    ok(shown.join() === "PROPHET,FM6,CZ-1,SID,808,909,SID KIT,CR78,FM KIT,SAMPLER,MIDI Track" &&
       E.engineOrder(info.engines)[1] === H("FM6") && E.engineOrder(info.engines).at(-1) === info.midi,
       "editor: engines listed FM6 second, MIDI Track last (indices kept)");
    ok(E.engineOrder(["X", "-", "808", "FM6"]).join() === "3,2,0", "editor: an unknown engine follows the known ones");
    m.state.favorites[H("808")][0] = m.state.favorites[H("FM6")][0] = true;
    await rq(E.req.uiSet(3, 0));
    prefs = await E.readDevicePreferences(rq, info, names, prefs);
    const rows = E.devicePresetRows(info, names, prefs).filter((r) => !r.user), eng = [...new Set(rows.map((r) => r.engine))];
    ok(eng[0] === H("PROPHET") && eng[1] === H("FM6") && eng[eng.length - 1] === info.midi, "editor: device presets in the device's engine order");
  }
  const none = await E.readDevicePreferences(() => { throw new Error("unexpected request"); }, { uiCaps: 0 }, []);
  ok(none === null, "editor: old firmware receives no unsupported preference requests");
  await rq(E.req.uiSet(3, 0));
  const scale = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 26)));
  const scaleNames = JSON.parse(readFileSync(new URL("../assets/scales/catalog.json", import.meta.url), "utf8")).map(s => s.name);
  ok(scale.label === "SCL" && scale.max === scaleNames.length - 1 && eq(scale.names, scaleNames), "editor: full microtonal scale catalogue exposed");
  const tuning = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 47)));
  const tuningNames = JSON.parse(readFileSync(new URL("../assets/scales/tunings.json", import.meta.url), "utf8")).map(s => s.name);
  ok(tuning.label === "TUNING" && tuning.max === tuningNames.length - 1 && eq(tuning.names, tuningNames), "editor: the tunings exposed");
  const scaleSet = E.parse[E.CMD.SET](await rq(E.req.set(0, scale.id, scaleNames.length - 1)));
  ok(scaleSet.value === scaleNames.length - 1, "editor: new scale selection is not clamped to the old range");
  await rq(E.req.set(0, scale.id, scaleNames.indexOf("53EDO")));
  const degree = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 83)));
  const degreeSet = E.parse[E.CMD.SET](await rq(E.req.set(0, 83, 53)));
  ok(degree.max === 53 && degreeSet.value === 53, "editor: MPC degree range follows the selected microtonal scale");
  await rq(E.req.set(0, scale.id, 1));
  const smallDegree = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 83)));
  const smallValue = E.parse[E.CMD.GET](await rq(E.req.get(0, 83)));
  ok(smallDegree.max === 7 && smallValue.value === 7, "editor: changing to a smaller scale clamps MPC degree");
  const dump = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
  ok(dump.p.length === info.pcount && dump.g.length === info.gcount, "editor: DUMP");
  const set = E.parse[E.CMD.SET](await rq(E.req.set(0, 3, 500)));
  ok(set.value === 127, "editor: SET clamps to the range");
  {
    /* ROUT keeps id 14; the device workload setting is appended at 41. */
    const rd = E.parse[E.CMD.DESC](await rq(E.req.desc(1, 14)));
    const r1 = E.parse[E.CMD.SET](await rq(E.req.set(1, 14, 5)));
    const r0 = E.parse[E.CMD.SET](await rq(E.req.set(1, 14, 0)));
    ok(rd.label === "ROUT" && rd.def === 0 && eq(rd.names, ["CH1-4", "SEL", "CH5-8", "CH9-12", "CH13-16"]) && r1.value === 4 && r0.value === 0 && info.gcount === 45,
      "editor: MIDI IN routing (ROUT four-channel ranges / SEL, global id 14)");
  }
  {
    const d=E.parse[E.CMD.DESC](await rq(E.req.desc(1,41)));
    await rq(E.req.set(1,41,24));await rq(E.req.project(1,0));
    await rq(E.req.set(1,41,32));await rq(E.req.project(0,0));
    const r=E.parse[E.CMD.GET](await rq(E.req.get(1,41)));
    ok(d.label==="BUDGET" && d.min===8 && d.max===32 && r.value===32,"editor: device workload budget survives project loads");
    await rq(E.req.set(1,41,16));
  }
  {
    for(const id of [4,5,6,7,8,9,10,11,24,25,26]){
      const d=E.parse[E.CMD.DESC](await rq(E.req.desc(1,id)));
      const r=E.parse[E.CMD.SET](await rq(E.req.set(1,id,127)));
      ok(!d.label && r.value===0,"editor: removed global effect parameter is inert "+id);
    }
  }
  const st = E.parse[E.CMD.STEP_SET](await rq(E.req.stepSet(5, { n: 2, notes: [60, 64], time: 0, flags: 1, vel: 100 })));
  ok(st.n === 2 && st.notes[1] === 64 && st.vel === 100, "editor: STEP_SET");
  const pj = E.parse[E.CMD.PROJECT](await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 }));
  ok(pj.used === 1, "editor: PROJECT save");
  {
    const desc = E.parse[E.CMD.DESC](await rq(E.req.desc(1, 21)));
    const set = E.parse[E.CMD.SET](await rq(E.req.set(1, 21, 432)));
    await rq(E.req.project(0, 2));
    const got = E.parse[E.CMD.GET](await rq(E.req.get(1, 21)));
    ok(desc.label === "A4" && desc.unit === "Hz" && desc.min === 400 && desc.max === 480 &&
       set.value === 432 && got.value === 432, "editor: A4=432 Hz is independent of project loads");
  }
  /* sample upload as smpUpload() does it */
  const s = Int16Array.from({ length: 3000 }, (_, i) => Math.round(8000 * Math.sin(i / 7)));
  const { hdr, data } = E.buildSlot("test", [{ s, root: 60 }]);
  let rc = E.parse[E.CMD.SMP_BEGIN](await rq(E.req.smpBegin(1), { timeout: 1000, retries: 0 })).rc;
  for (let off = 0; off < data.length && !rc; off += 256) {
    rc = E.parse[E.CMD.SMP_WRITE](await rq(E.req.smpWrite(1, E.SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000 })).rc;
  }
  rc = rc || E.parse[E.CMD.SMP_END](await rq(E.req.smpEnd(1, hdr), { timeout: 2000, retries: 0 })).rc;
  const si = E.parse[E.CMD.SMP_INFO](await rq(E.req.smpInfo()));
  ok(rc === 0 && si.slots[1].zones === 1 && si.slots[1].name === "TEST", "editor: sample upload (CRC checked by the mock)");
  /* a device that never answers */
  const dead = new E.Link(() => {}, { timeout: 30 });
  const err = await dead.request(E.req.info(), { retries: 1 }).then(() => null, (e) => e.message);
  ok(/^timeout/.test(err || ""), "editor: no reply -> timeout after the retries");
  link.close();
  m.stop();
}

async function editorSamplePresets() {
 const {rq,done}=attachMock();const info=E.parse[E.CMD.INFO](await rq(E.req.info()));
 ok(!info.engines.some(n=>["SAMPLE","GRAIN","SLICE"].includes(n)) && E.engineOrder(info.engines).every(i=>info.engines[i]!=="-"),
    "editor: SAMPLE, GRAIN and SLICE have no handle; no empty handle listed");done();
}

async function editorMidiTrack() {
  const {m,rq,done} = attachMock();
  const C=E.CMD, info=E.parse[C.INFO](await rq(E.req.info()));
  await rq(E.req.track(2)); await rq(E.req.set(1,20,info.midi));
  let dump=E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(dump.engine===info.midi && dump.p[info.pe0]===3, "MIDI Track: editor defaults output channel to track number");
  const names=E.parse[C.NAMES](await rq(E.req.names(info.midi)));
  ok(names.names.length===1 && names.names[0]==="MIDI Track", "MIDI Track: one clearly named factory preset");
  await rq(E.req.set(0,info.pe0,16)); await rq(E.req.set(0,info.pe0+1,1));
  const level=dump.p[0],pan=dump.p[39];
  await rq(E.req.set(0,0,0)); await rq(E.req.trackParam(2,39,30));
  await rq(E.req.trackMix(2,0,true));
  await rq(E.req.set(0,96,8)); await rq(E.req.trackParam(2,108,127));
  dump=E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(dump.p[0]===level && dump.p[39]===pan && dump.p[40]===1 && !dump.p[96] && !dump.p[108],
    "MIDI Track: editor disables audio controls and FX while retaining mute");
  await rq(E.req.project(1,0)); await rq(E.req.set(0,info.pe0,1)); await rq(E.req.project(0,0));
  dump=E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(dump.engine===info.midi && dump.p[info.pe0]===16 && dump.p[info.pe0+1]===1, "MIDI Track: editor project round-trip retains routing");
  done();
}

/* the mock's tables == the firmware's (build/host/desc.json from tests/descdump.c, written by run_tests.sh):
   parameter descriptors, engines (titles, EDIT, presets), power-on sounds */
function mockTables() {
  {
    const m0 = E.makeMockDevice({ auto: false }), dr = m0.tables.ENG.find((e) => e.name === "808");
    m0.stop();
    ok(!m0.tables.ENG.some((e) => ["PHASE", "VOICE", "TRIO", "WHEEL", "PHYS", "NOISE"].includes(e.name)) &&
       m0.tables.ENG.filter((e) => e.name === "-").every((e) => !e.presets.length),
       "editor: PHASE, VOICE, TRIO, WHEEL, PHYS and NOISE retired (no handle); a free handle has no presets");
    const kits = ["808", "909", "SID KIT", "CR78", "FM KIT", "SAMPLER"].map((n) => m0.tables.ENG.find((e) => e.name === n));
    ok(dr.name === "808" && dr.edit.map((d) => d.label).join() === "KIT,TUNE,TONE,DECY,SNAP,ACC,KICK,DRV" &&
       kits.every((k) => k && k.edit[0].label === "KIT") && kits.map((k) => k.edit[0].min + ".." + k.edit[0].max).join() === "4..4,5..5,6..6,7..7,8..8,9..16" &&
       kits.map((k) => k.presets.map((p) => p.name).join("+")).join() === "808 KIT,909 KIT,SID KIT+SID DIGI,CR78 KIT,FM KIT,KIT 1+KIT 2+KIT 3+KIT 4+KIT 5+KIT 6+KIT 7+KIT 8",
       "editor: the kits, synths of their own (KIT TUNE TONE DECY SNAP ACC KICK DRV): 808, 909, SID KIT, CR78, FM KIT, SAMPLER (KIT 1..8)");
  }
  const dj = join(HERE, "../build/host/desc.json");
  if (!existsSync(dj)) { console.log("editor: mock tables == firmware (no build/host/desc.json)        skip"); return; }
  const fw = JSON.parse(readFileSync(dj, "utf8"));
  const m = E.makeMockDevice({ auto: false }), T = m.tables;
  m.stop();
  const norm = (d) => ({ label: d.label, fmt: d.fmt, min: d.min, max: d.max, def: d.def, names: d.names || null, unit: d.unit || "" });
  const diffs = [];
  const cmp = (what, a, b) => { if (JSON.stringify(a) !== JSON.stringify(b)) diffs.push(`${what}: mock ${JSON.stringify(a)} != firmware ${JSON.stringify(b)}`); };
  for (const k of ["P_COUNT", "G_COUNT", "NSTEP", "P_E0", "G_ENGSEL", "P_RESERVED45", "NTRK"]) cmp(k, T[k], fw[k]);
  if (fw.MAP) cmp("engine map (eng_map.c: each number's module UID)", T.MAP, fw.MAP);
  cmp("TP length", T.TP.length, fw.TP.length);
  fw.TP.forEach((d, i) => cmp(`TP[${i}]`, T.TP[i] && norm(T.TP[i]), d));
  cmp("GP length", T.GP.length, fw.GP.length);
  fw.GP.forEach((d, i) => cmp(`GP[${i}]`, T.GP[i] && norm(T.GP[i]), d));
  cmp("engines", T.ENG.map((e) => e.name), fw.ENG.map((e) => e.name));
  cmp("engine order (ENGINE_ORDER)", E.engineOrder(fw.ENG.map(e=>e.name)).map(i=>fw.ENG[i].name), fw.ORDER);
  fw.ENG.forEach((fe, i) => {
    const me = T.ENG[i];
    if (!me) return;
    cmp(`${fe.name} titles`, me.titles, fe.titles);
    fe.edit.forEach((d, k) => cmp(`${fe.name} edit[${k}]`, norm(me.edit[k]), d));
    cmp(`${fe.name} presets`, me.presets.map((p) => p.name), fe.presets.map((p) => p.name));
    fe.presets.forEach((p, k) => {
      const mp = me.presets[k];
      if (mp) cmp(`${fe.name} ${p.name}`, { e: mp.e, env: mp.env, mono: mp.mono }, { e: p.e, env: p.env, mono: p.mono });
    });
  });
  const m2 = E.makeMockDevice({ auto: false });   /* the mock's power-on sounds: its parts 1..4 */
  cmp("power-on sounds", m2.state.tracks.map((t) => [t.engine, t.preset]), fw.TRK_DEF);
  m2.stop();
  cmp("firmware: G_ENGSEL names == engine names", fw.GP[fw.G_ENGSEL].names, fw.ENG.map((e) => e.name));
  cmp("DRUM grid: the lanes' GM notes", [...E.LANE_NOTE], fw.LANE_NOTE);
  cmp("DRUM grid: the lane of GM 35..81", E.LANE_OF, fw.LANE_OF);
  diffs.slice(0, 20).forEach((d) => console.log("  " + d));
  ok(!diffs.length, `editor: mock tables == firmware (${diffs.length} differences)`);
  /* #31: percent values as the firmware formats them (param_format), SWG 0..100 shows its value */
  const pd = (fw.PCT || []).filter(([sc, i, v, , txt]) => E.fmtValue((sc ? T.GP : T.TP)[i], v)[0] !== txt);   /* [scope, index, value, max, text] */
  pd.slice(0, 5).forEach((x) => console.log("  PCT " + JSON.stringify(x)));
  ok(fw.PCT && fw.PCT.length && !pd.length, `editor: percent values == firmware (${fw.PCT ? fw.PCT.length : 0} values)`);
  const swg = [T.TP, T.GP].flatMap((tb) => tb.filter((d) => d.label === "SWG"));
  ok(swg.length === 3 && swg.every((d) => E.fmtValue(d, 100)[0] === "100" && E.fmtValue(d, 50)[0] === "50") &&
     E.fmtValue({ fmt: swg[0].fmt, min: 0, max: 127 }, 127)[0] === "100" && E.fmtValue({ fmt: swg[0].fmt, min: 0, max: 127 }, 64)[0] === "50",
     "editor: SWG 100 shows 100 % (not 79), 0..127 percents unchanged");
}

/* ------------------------------------- editor protocol v2: librarian + live --- */
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const js = (x) => JSON.stringify(x);
/* a mock + Link pair; counts frames sent per cmd and timeouts reported */
function attachMock(opt, linkOpt = {}) {
  const m = E.makeMockDevice({ auto: false, ...opt });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const sent = {}, ev = { timeouts: 0, unknown: [], pushes: [] };
  const link = new E.Link((d) => { sent[d[4]] = (sent[d[4]] || 0) + 1; out.send(d); }, {
    timeout: 300, onTimeout: () => ev.timeouts++, onUnknown: (f) => ev.unknown.push(f),
    onPush: (f) => ev.pushes.push({ ...f, pending: link.cur ? link.cur.cmd : 0 }), ...linkOpt });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = (r, o) => link.request(r, o);
  return { m, link, rq, sent, ev, done: () => { link.close(); m.stop(); } };
}
const emptyStep = { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 };

async function trackEffects() {
  const {rq,done}=attachMock({});
  try {
    const info=E.parse[E.CMD.INFO](await rq(E.req.info())),H=engineHandles(info);
    await rq(E.req.preset(H("FM6"),0));
    const fx=[10,80,30,71,11,95,55,65,10,105,88,99];
    for(let i=0;i<fx.length;i++)await rq(E.req.set(0,96+i,fx[i]));
    for(let i=0;i<3;i++)await rq(E.req.set(0,111+i,i?-20-i:24));
    const cap=(await E.capturePatch(rq,info,"DRY PATCH")).patch;
    ok(eq(cap.p.slice(111,114),[0,0,0]),"FX: librarian capture excludes TONE");
    ok(eq(cap.p.slice(96,108),[0,0,60,64,0,0,60,64,0,0,60,64]),"FX: librarian capture excludes the track's chain");
    await E.bank.store(rq,30,"DRY PATCH");
    const stored=await E.bank.get(rq,info,30);
    ok(eq(stored.p.slice(96,108),cap.p.slice(96,108)),"FX: device preset store uses dry insert defaults");
    await rq(E.req.preset(H("SID"),1));await E.bank.load(rq,30);await E.auditionPatch(rq,info,cap);
    const dump=E.parse[E.CMD.DUMP](await rq(E.req.dump()),info);
    ok(eq(dump.p.slice(96,108),fx),"FX: factory, user and librarian sound loads preserve all three track slots");
    ok(eq(dump.p.slice(111,114),[24,-21,-22]),"FX: sound loads preserve TONE");
    for (const retired of [1,6,7,8,9,12,13,14,15,16,17,18,19]) {
      for (const id of [96,100,104]) await rq(E.req.set(0,id,retired));
      const retiredDump=E.parse[E.CMD.DUMP](await rq(E.req.dump()),info);
      ok([96,100,104].every(id=>retiredDump.p[id]===0),`FX: retired type ${retired} maps to OFF in all track slots`);
    }
    ok(eq(E.fmtValue({fmt:E.F.TONE},0),["Full",""]) && eq(E.fmtValue({fmt:E.F.TONE},24),["Thin 313","Hz"]) && eq(E.fmtValue({fmt:E.F.TONE},-39),["Dark 516","Hz"]) &&
       eq(E.fmtValue({fmt:E.F.TONE},63),["Thin 1280","Hz"]) && eq(E.fmtValue({fmt:E.F.TONE},1),["Thin 150","Hz"]) && eq(E.fmtValue({fmt:E.F.TONE},-63),["Dark 120","Hz"]),"FX: TONE formats Full, Dark / Thin and the cut's frequency");
    for (const scope of [1]) {
      const base=scope===0?96:27,third=scope===0?111:42;
      const typeDesc=E.parse[E.CMD.DESC](await rq(E.req.desc(scope,base)));
      ok(typeDesc.max===19 && (scope===0 || (!typeDesc.names[10] && !typeDesc.names[11])),"FX: new types retain MIDI-only slots in the shared rack enum");
      ok(eq([12,13,14,15].map(v=>E.fmtValue(typeDesc,v)[0]),["DRIVE · SOFT","DRIVE · HARD","DRIVE · FOLD","DRIVE · FUZZ"]),"FX: every stored DRIVE shape has a distinct editor choice");
      for (const [type,a,b,fmt] of [[8,"SIZE","DAMP",E.F.PCT],[12,"DRIVE","TONE",E.F.PCT],[16,"BITS","ALIAS",E.F.BITS],[17,"RATE","DEPTH",E.F.LFOHZ],[18,"RATE","DEPTH",E.F.LFOHZ],[19,"RATE","DEPTH",E.F.LFOHZ]]) {
        await rq(E.req.set(scope,base,type));
        const da=E.parse[E.CMD.DESC](await rq(E.req.desc(scope,base+2))),db=E.parse[E.CMD.DESC](await rq(E.req.desc(scope,base+3)));
        ok(da.label===a && db.label===b && da.fmt===fmt,`FX: scope ${scope} type ${type} exposes its actual controls`);
        if(type>=17){const fb=E.parse[E.CMD.DESC](await rq(E.req.desc(scope,third)));ok(fb.label==="RESO" && fb.fmt===E.F.BIPCT,"FX: swept pedals expose signed resonance");}
      }
      await rq(E.req.set(scope,base,0));
    }
    const common={step:0,param:info.pe0+1,value:63},native={step:0,param:info.pe0+1,value:(3<<8)|80};
    await rq(E.req.motion(0,3,common));let m=E.parse[E.CMD.MOTION](await rq(E.req.motion(0,3,native)));
    ok(m.count===2,"motion: native and common events sharing an engine ID coexist");
    m=E.parse[E.CMD.MOTION](await rq(E.req.motion(0,4,{...common,typed:true})));
    ok(m.count===1&&m.events[0].value===native.value,"motion: typed common deletion preserves the native event");
    await rq(E.req.motion(0,3,common));m=E.parse[E.CMD.MOTION](await rq(E.req.motion(0,4,{...native,typed:true})));
    ok(m.count===1&&m.events[0].value===common.value,"motion: typed native deletion preserves the common event");
  } finally {done();}
}

async function editorLibrarian() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info())), H = engineHandles(info);
  const pdesc = [];
  for (let i = 0; i < info.pcount; i++) pdesc.push(E.parse[C.DESC](await rq(E.req.desc(0, i))));
  const keys = E.paramKeys(pdesc, info.pe0, info.pcount);
  ok(keys[6] === "PIT" && keys[13] === "PIT#2" && keys[info.pe0] === "E0" && new Set(keys).size === keys.length, "librarian: parameter keys unique (label#n, E0..E7)");

  const b = await E.bank.list(rq);
  ok(b.total === 64 && b.slots.length === 64 && b.slots[1].used && b.slots[1].name === "GLASS BELL" && !b.slots[3].used
    && b.slots[63].slot === 63, "librarian: UP_LIST, 64 slots in 4 frames");

  await rq(E.req.preset(H("SID"), 6));           /* SID SAW BASS on the ACID steps (the power-on Prophet: its own slots) */
  const cap = (await E.capturePatch(rq, info, "acid test")).patch;
  ok(cap.engine === H("SID") && cap.p.length === info.pcount && cap.pattern && cap.pattern[0][0] === 45 && cap.pattern[0][1] === 1,
    "librarian: capture = DUMP + first 16 steps");
  let rc = await E.bank.put(rq, 10, cap);
  const g = await E.bank.get(rq, info, 10);
  ok(rc === 0 && g.used && g.name === "acid test" && g.engine === H("SID") && eq(g.p, cap.p) && js(g.pattern) === js(cap.pattern),
    "librarian: UP_PUT -> UP_GET round trip");
  const bad = E.parse[C.UP_PUT](await rq(E.req.upPut(64, cap), { timeout: 2500, retries: 0 }));
  ok(bad.rc === 1, "librarian: UP_PUT to a slot past the bank -> rc 1");
  ok(E.upName("") === "PATCH" && E.upName("abcdefghijklmnop") === "abcdefghijkl" && E.upName("Bäss") === "Bss", "librarian: device names (ASCII, 1..12)");

  await rq(E.req.set(0, 1, 33));
  rc = await E.bank.store(rq, 11, "STORED");
  const g2 = await E.bank.get(rq, info, 11);
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(rc === 0 && g2.name === "STORED" && g2.engine === d1.engine && eq(g2.p, d1.p) && g2.p[1] === 33, "librarian: UP_STORE keeps the current sound");

  /* another engine, an empty sequencer, then UP_LOAD brings the sound back, not the stored pattern */
  await rq(E.req.preset(H("FM6"), 0));
  for (let i = 0; i < info.nstep; i++) await rq(E.req.stepSet(i, emptyStep));
  await rq(E.req.set(0, 17, 3));                 /* the track's own: ARP MODE */
  rc = await E.bank.load(rq, 10);
  const d2 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  ok(rc === 0 && d2.engine === H("SID") && eq(d2.p.filter((_, i) => i !== 17), cap.p.filter((_, i) => i !== 17)) && d2.p[17] === 3
    && st.every((x) => !x.n) && cap.pattern, "librarian: UP_LOAD applies the sound only (steps and the track's ARP stay)");
  await rq(E.req.set(0, 17, 0));
  for (let i = 0; i < 16; i++) await rq(E.req.stepSet(i, E.stepsFromPattern(cap.pattern)[i]));   /* (for the captures below) */

  rc = await E.bank.erase(rq, 11);
  const b2 = await E.bank.list(rq);
  const rcEmpty = await E.bank.load(rq, 11);
  ok(rc === 0 && !b2.slots[11].used && b2.slots[10].used && rcEmpty === 1, "librarian: UP_ERASE, UP_LOAD of an empty slot -> rc 1");

  /* audition: a patch that holds a pattern; the sound only, the sequence (with notes) stays */
  const bass = await E.bank.get(rq, info, 2);   /* SID BREADBOX, with the ACID steps */
  await rq(E.req.stepSet(20, { n: 1, notes: [50, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await rq(E.req.set(0, 29, 24));               /* the track's own: LEN 24, ARP MODE 2 */
  await rq(E.req.set(0, 17, 2));
  await rq(E.req.set(0, 81, 1));                /* CHRD DIA3: the track's too */
  const seqBefore = [];
  for (let i = 0; i < 24; i++) seqBefore.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  const flashBefore = js(m.state.bank);
  await E.auditionPatch(rq, info, bass, { gEng: 20 });
  const d3 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st3 = [];
  for (let i = 0; i < 24; i++) st3.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  const own = new Set([0, 39, 40, ...Array.from({ length: 16 }, (_, i) => 17 + i), 45, 46, 47, 48, 81, 82]);
  ok(d3.engine === bass.engine && eq(d3.p.filter((_, i) => !own.has(i)), bass.p.filter((_, i) => !own.has(i)))
    && d3.p[29] === 24 && d3.p[81] === 1 && d3.p[17] === 2 && js(st3) === js(seqBefore) && st3[20].notes[0] === 50 && js(m.state.bank) === flashBefore,
    "librarian: audition = G_ENGSEL + SETs of the sound; steps, LEN and ARP stay; no flash write");
  /* the next audition: the same rule */
  const bell = await E.bank.get(rq, info, 1);
  await E.auditionPatch(rq, info, bell, { gEng: 20 });
  const s20 = E.parse[C.STEP_GET](await rq(E.req.stepGet(20)));
  const d4 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(d4.engine === bell.engine && bell.engine === H("FM6") && s20.notes[0] === 50 && d4.p[29] === 24 && eq(d4.p.filter((_, i) => !own.has(i)), bell.p.filter((_, i) => !own.has(i))),
    "librarian: a second audition keeps the sequence too");
  await rq(E.req.set(0, 29, 16));
  await rq(E.req.set(0, 17, 0));
  await rq(E.req.set(0, 81, 0));

  /* ties as the firmware keeps them: no note; a rest has no flags */
  const tied = [[60, 1], [61, 4], [0, 4], [0, 3], [64, 2]];
  const norm = E.patternFromSteps(E.stepsFromPattern(tied));
  rc = await E.bank.put(rq, 12, { ...cap, name: "TIES", pattern: tied });
  const g3 = await E.bank.get(rq, info, 12);
  ok(rc === 0 && js(norm.slice(0, 5)) === js([[60, 1], [0, 4], [0, 4], [0, 0], [64, 2]]) && js(g3.pattern) === js(norm),
    "librarian: pattern ties / rests normalised like the firmware");
  rc = await E.bank.store(rq, 13, "");
  ok(rc === 0 && (await E.bank.get(rq, info, 13)).name === `${info.engines[d4.engine]} 14`, "librarian: UP_STORE with no name -> automatic name");

  /* library files */
  const ctx = { keys, engines: info.engines, firmware: info.version, pe0: info.pe0 };
  const pts = [cap, { ...bass, engineName: info.engines[bass.engine], tags: ["bass", "device"] }];
  const file = JSON.parse(JSON.stringify(E.libraryFile("library", pts, ctx)));
  ok(file.format === "felucca-library" && file.version === 1 && file.pCount === info.pcount && file.paramLabels.length === info.pcount && file.paramLabels[81] === "CHRD" && file.paramLabels[82] === "VOIC" && file.paramLabels[83] === "DEG" && file.paramLabels.slice(92, 96).join() === "SYNC,TRIG,POL,SPRD" && file.engines.length === info.nengines,
    "library file: versioned, with P_COUNT, labels and engines");
  const back = E.readLibraryFile(file, ctx);
  const oldKeys = keys.slice(); oldKeys[8] = "FX";
  const oldTiming = E.readLibraryFile({ ...file, paramLabels: oldKeys }, ctx).patches[0].p;
  ok(keys[8] === "TQNT" && keys[27] === "QNT" && oldTiming[8] === null && oldTiming[27] === cap.p[27],
    "library file: timing QNT has a separate key and preserves legacy scale QNT");
  {
    const legacyKeys=[...keys.slice(0,96),...keys.slice(info.pe0)];
    const legacyValues=[...cap.p.slice(0,96),...cap.p.slice(info.pe0)];
    for (const [i,name] of [[8,"FX"],[33,"MIX"],[34,"RATE"],[35,"CHAR"]]) {legacyKeys[i]=name;legacyValues[i]=127;}
    const mapped=E.readLibraryFile({...file,pCount:104,pE0:96,paramLabels:legacyKeys,
      patches:[{...file.patches[0],params:legacyValues}]},ctx).patches[0].p;
    ok(keys.slice(96,100).join()==="FX.TYPE,FX.MIX,FX.RATE,FX.CHAR" && mapped.slice(96,100).every(v=>v===null),
      "library file: retired effect labels never activate new FX controls");
  }
  ok(back.patches.length === 2 && !back.skipped && eq(back.patches[0].p, cap.p) && eq(back.patches[1].p, bass.p)
    && js(back.patches[1].pattern) === js(bass.pattern) && back.patches[1].tags.join() === "bass,device" && back.patches[1].engineName === "SID",
    "library file: write -> read round trip");
  /* a future firmware: one more parameter at id 5, engines in another order and one of them gone */
  const keys2 = [...keys.slice(0, 5), "NEW", ...keys.slice(5)];
  const eng2 = ["ANALOG", "SAMPLE", "SID"];
  const fut = E.readLibraryFile(file, { keys: keys2, engines: eng2 });
  const p0 = fut.patches[0].p;
  ok(fut.patches.length === 2 && p0.length === info.pcount+1 && p0[5] === null && p0[6] === cap.p[5] && p0[info.pcount] === cap.p[info.pcount-1]
    && fut.patches[0].engine === 2 && fut.patches[1].engine === 2, "library file: other ids / engine order mapped by label and name");
  const lost = E.readLibraryFile({ ...file, patches: [{ ...file.patches[0], engineName: "WAVETABLE" }] }, ctx);
  ok(lost.patches.length === 0 && lost.skipped === 1, "library file: a patch for an unknown engine is skipped");
  const bankFile = E.libraryFile("bank", [{ ...g, engineName: "SID", slot: 10 }], ctx);
  ok(bankFile.kind === "bank" && bankFile.patches[0].slot === 10 && E.readLibraryFile(bankFile, ctx).patches[0].slot === 10, "library file: bank export keeps slot numbers");
  {   /* files from the 69-parameter firmware (P_E0 61): the engine's 8 land on E0..E7 (E0..E7), FM op ENV, the
         chord keys, the MPC degree, the lane levels, LFO 2 and SPRD stay unset */
    const p69 = [...cap.p.slice(0, 61), ...cap.p.slice(info.pe0, info.pcount)];
    const sp = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: cap.engine, engineName: info.engines[cap.engine], p: p69 }, ctx).patches[0].p;
    const keys69 = [...keys.slice(0, 61), ...keys.slice(info.pe0)];
    const lp = E.readLibraryFile({ ...file, pCount: 69, paramLabels: keys69, patches: [{ ...file.patches[0], params: p69 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p69 }] }, ctx).patches[0].p;
    const good = (q) => q.length === info.pcount && eq(q.slice(0, 61), cap.p.slice(0, 61)) && eq(q.slice(info.pe0), cap.p.slice(info.pe0)) && q.slice(61, info.pe0).every((v) => v === null);
    ok(good(sp) && good(lp) && good(nk), "library file: 69-parameter files (patch, labelled, unlabelled) map the engine's 8 to E0..E7");
  }
  {   /* files from the 89-parameter firmware (P_E0 81, before the chord keys): the engine's 8 land on E0..E7, the FM
         op ENV in place, CHRD VOIC DEG and everything after them unset (left as the track has them) */
    const p89 = [...cap.p.slice(0, 81), ...cap.p.slice(info.pe0, info.pcount)];
    const keys89 = [...keys.slice(0, 81), ...keys.slice(info.pe0)];
    const sp = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: cap.engine, engineName: info.engines[cap.engine], p: p89 }, ctx).patches[0].p;
    const lp = E.readLibraryFile({ ...file, pCount: 89, pE0: 81, paramLabels: keys89, patches: [{ ...file.patches[0], params: p89 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p89 }] }, ctx).patches[0].p;
    const good = (q) => q.length === info.pcount && eq(q.slice(0, 81), cap.p.slice(0, 81)) && q.slice(81, info.pe0).every((v) => v === null) && eq(q.slice(info.pe0), cap.p.slice(info.pe0));
    ok(keys89[81] === "E0" && good(sp) && good(lp) && good(nk),
      "library file: 89-parameter files (patch, labelled, unlabelled) map the engine's 8 to E0..E7, the chord keys unset");
  }
  {   /* files from Felucca 1.0 (91 parameters, P_E0 83, before the MPC degree): E0..E7 on E0..E7, DEG and everything after it unset */
    const p91 = [...cap.p.slice(0, 83), ...cap.p.slice(info.pe0, info.pcount)];
    const keys91 = [...keys.slice(0, 83), ...keys.slice(info.pe0)];
    const lp = E.readLibraryFile({ ...file, pCount: 91, pE0: 83, paramLabels: keys91, patches: [{ ...file.patches[0], params: p91 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p91 }] }, ctx).patches[0].p;
    const good = (q) => q.length === info.pcount && eq(q.slice(0, 83), cap.p.slice(0, 83)) && q.slice(83, info.pe0).every((v) => v === null) && eq(q.slice(info.pe0), cap.p.slice(info.pe0));
    ok(keys91[83] === "E0" && good(lp) && good(nk), "library file: Felucca 1.0 files (91 parameters) map the engine's 8 to E0..E7, DEG unset");
  }
  /* (a 1.x file: its engine number 3 was SID's then; the name says which synth) */
  const old = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 3, preset: 6, engineName: "SID", presetName: "SAW BASS", p: d2.p, steps: E.stepsFromPattern(cap.pattern) }, ctx);
  ok(old.patches.length === 1 && old.patches[0].engine === H("SID") && eq(old.patches[0].p, d2.p) && js(old.patches[0].pattern) === js(cap.pattern), "library file: reads the old \"Save to file\" format (the synth by its name)");
  let threw = false;
  try { E.readLibraryFile({ format: "something" }, ctx); } catch (e) { threw = true; }
  ok(threw, "library file: unknown format -> error");
  done();
}

/* v5: the DRUM grid. Steps carry lane hits and accents (3 bytes after vel); a user preset of a DRUM track holds a
   16-step grid (UP_GET / UP_PUT kind 1); the steps page writes them as lane names */
async function editorGrid() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const h = E.parseHits("kick >snare HATCL cym");
  ok(h && h.hit === (1 | 2 | 8 | 128) && h.acc === 2 && E.hitsText(h.hit, h.acc) === "KICK >SNARE HATCL BELL" &&
     E.parseHits("KICK FOO") === null && E.parseHits(" ").hit === 0, "grid: hits as text (lanes, > accent, CYM / CONGA aliases)");
  let s = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 1, notes: [41, 0, 0, 0], time: 0, flags: 0, vel: 100, hit: 0x81, acc: 0x80 })));
  ok(s.hit === 0x81 && s.acc === 0x80 && s.notes[0] === 41, "grid: STEP_SET / STEP_GET carry the hits (lane 8 too)");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 1, notes: [42, 0, 0, 0], time: 0, flags: 0, vel: 100 })));
  ok(s.hit === 0x81 && s.notes[0] === 42, "grid: a STEP_SET without hits (an older editor) keeps them");
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 4, { n: 0, notes: [0, 0, 0, 0], time: 0, flags: 0, vel: 0, hit: 0x12, acc: 0x02 })));
  ok(w.hit === 0x12 && w.acc === 0x02 && E.parse[C.TRACK_STEP]([2, 4, 0, 0, 0, 0, 0, 0, 0, 0]).hit === 0,
    "grid: TRACK_STEP carries the hits (an older firmware's reply: none)");
  await rq(E.req.preset(engineHandles(info)("808"), 0));   /* the selected track the 808 kit, its 16 steps a grid */
  for (let i = 0; i < 16; i++) {
    await rq(E.req.stepSet(i, { n: i === 2 ? 1 : 0, notes: [i === 2 ? 49 : 0, 0, 0, 0], time: i % 4 === 0 || i === 2 ? 0 : 2, flags: 0,
      vel: 0, hit: i % 4 === 0 ? 1 | (i === 0 ? 128 : 0) : 0, acc: i === 0 ? 128 : 0 }));
  }
  const cap = await E.capturePatch(rq, info, "MY BEAT");
  ok(cap.patch.grid && !cap.patch.pattern && cap.patch.grid[0][0] === 0x81 && cap.patch.grid[0][1] === 0x80 &&
     cap.patch.grid[2][0] === 0x80 && cap.patch.grid[1][0] === 0 && cap.patch.grid[4][0] === 1,
     "grid: capture of a DRUM track = its grid (a crash 49 on BELL)");
  const rc = await E.bank.put(rq, 14, cap.patch);
  const g = await E.bank.get(rq, info, 14);
  ok(rc === 0 && g.grid && !g.pattern && JSON.stringify(g.grid) === JSON.stringify(cap.patch.grid), "grid: UP_PUT kind 1 -> UP_GET round trip");
  const put = E.req.upPut(14, cap.patch), saved = JSON.stringify(m.state.bank[14]);
  const base = put[1].length - 17;
  for (const bytes of [put[1].slice(0, -1), [...put[1], 0], [...put[1].slice(0, base), 2, ...Array(16).fill(0)]]) {
    const rejected = E.parse[C.UP_PUT](await rq([C.UP_PUT, bytes]));
    ok(rejected.rc === 1 && JSON.stringify(m.state.bank[14]) === saved,
       "grid: malformed preset extension rejected without changing saved data");
  }
  const ctx = { keys: null, engines: info.engines };
  const back = E.readLibraryFile(JSON.parse(JSON.stringify(E.libraryFile("bank", [{ ...g, slot: 14 }], ctx))), ctx);
  ok(JSON.stringify(back.patches[0].grid) === JSON.stringify(g.grid) && !back.patches[0].pattern, "grid: a library file keeps the grid");
  await rq(E.req.upStore(15, "STORED"));
  const g2 = await E.bank.get(rq, info, 15);
  ok(g2.grid && g2.grid[0][0] === 0x81 && g2.grid[0][1] === 0x80, "grid: UP_STORE of a DRUM track stores its grid");
  const p3 = await E.bank.get(rq, info, 2);
  ok(p3.pattern && p3.grid === null, "grid: a note pattern stays a pattern (kind 0)");
  done();
}

async function editorLive() {
  const C = E.CMD;
  const { m, link, rq, sent, ev, done } = attachMock({ watchMs: 250 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(await E.startWatch(rq), "live: WATCH on");

  /* a push between a request and its reply */
  const pend = rq(E.req.dump());
  const kn = m.sim.knob(9, 4);
  const dump = E.parse[C.DUMP](await pend, info);
  const ch = ev.pushes.find((f) => f.cmd === C.CHANGED);
  const cv = ch && E.parse[C.CHANGED](ch.a);
  ok(dump.p.length === info.pcount && ch && ch.pending === C.DUMP && cv.scope === 0 && cv.id === 9 && cv.value === kn.value && !ev.unknown.length,
    "live: CHANGED while DUMP waits -> push handler, reply still matched");
  const rl = m.sim.reload();
  m.sim.step(3);
  const pend2 = rq(E.req.stepGet(7));
  const s7 = E.parse[C.STEP_GET](await pend2);
  await sleep(10);
  const r = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(s7.index === 7 && r && E.parse[C.RELOAD](r.a).preset === rl.preset && sc && E.parse[C.STEP_CHANGED](sc.a).index === 3,
    "live: RELOAD and STEP_CHANGED routed");
  const nr = ev.pushes.filter((f) => f.cmd === C.RELOAD).length;
  await rq(E.req.preset(engineHandles(info)("FM6"), 2));
  await rq(E.req.stepSet(9, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await sleep(10);
  ok(ev.pushes.filter((f) => f.cmd === C.RELOAD).length === nr + 1 && !ev.pushes.some((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 9),
    "live: RELOAD after an editor PRESET too, nothing after its STEP_SET");

  /* PING keeps the watch on; without requests it ends */
  for (let i = 0; i < 4; i++) { await sleep(120); await rq(E.req.ping()); }
  let n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0 + 1, "live: PING keeps WATCH on");
  await sleep(320);
  n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0, "live: WATCH ends by itself without requests");

  /* a slider drag: 40 values at once -> one SET in flight + one coalesced, the last value wins */
  const before = sent[C.SET] || 0;
  const all = [];
  for (let v = 0; v < 40; v++) all.push(rq(E.req.set(0, 9, v * 3), { key: "0:9" }));
  await Promise.all(all);
  ok((sent[C.SET] || 0) - before === 2 && m.state.p[9] === 117 && link.idle, "live: drag SETs coalesce (2 frames for 40 values, latest kept)");
  ok(ev.timeouts === 0, "live: no timeouts");
  done();

  /* older firmware: no reply to WATCH -> false without a "no reply" message; no bank */
  const o = attachMock({ legacy: true });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const w = await o.rq(E.req.watch(1), { timeout: 60, retries: 0, quiet: true }).then(() => true, () => false);
  const sw = await E.startWatch(o.rq);
  const bl = await E.bank.list((rr, oo) => o.rq(rr, { ...oo, timeout: 60, quiet: true })).then(() => "listed", (e) => e.message);
  ok(!w && !sw && /^timeout/.test(bl) && o.ev.timeouts === 0, "live: older firmware -> WATCH unanswered (fall back to polling), no bank");
  o.done();
}

async function preferenceReplies() {
  const C = E.CMD, unknown = [];
  const link = new E.Link(() => {}, { onUnknown: f => unknown.push(f) });
  for (const [request, stale, reply] of [
    [E.req.uiSet(0, 4), [0, 1, 1], [0, 0, 4]],
    [E.req.uiSet(0, 4), [0, 0, 3], [0, 0, 4]],
    [E.req.favGet(0, 2, 1), [0, 0, 3, 64, 1, 1], [0, 0, 2, 64, 1, 1]],
    [E.req.favSet(0, 2, true), [4, 0, 2, 64, 0], [4, 0, 2, 64, 1]],
  ]) {
    const pending = link.request(request);
    link.receive(E.frame(request[0], stale));
    ok(link.cur !== null, 'preferences: a reply for another setting or favorite cannot finish the request');
    link.receive(E.frame(request[0], reply));
    ok(eq(await pending, reply), 'preferences: the matching reply completes the request');
  }
  ok(unknown.length === 4 && link.idle, 'preferences: stale replies are routed away without leaving requests busy');
  link.close();
}

/* ------------------------------------------------------ editor protocol v3: tracks --- */
async function editorTracks() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info())), H = engineHandles(info);
  const tr = E.parse[C.TRACK](await rq(E.req.track()));
  ok(info.ntrk === 4 && tr.sel === 0 && tr.ntrk === 4 && tr.tracks[0].engine === H("PROPHET") && tr.tracks[1].engine === H("FM6")
    && tr.tracks[3].engine === H("808") && tr.tracks[3].preset === 0, "tracks: INFO NTRK, TRACK lists 4 parts (track 4: the 808 KIT)");
  /* the v1 commands follow the selected track */
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const t1 = E.parse[C.TRACK](await rq(E.req.track(1)));
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  await rq(E.req.set(0, 1, 77));
  const td0 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(0)), info);
  const td1 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(1)), info);
  ok(t1.sel === 1 && d1.engine === H("FM6") && d0.engine === H("PROPHET") && td1.p[1] === 77 && td0.p[1] === d0.p[1] && td0.p[1] !== 77,
    "tracks: TRACK selects; DUMP / SET act on it, TRACK_DUMP reads any track");
  /* steps of a track that is not selected */
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5, { n: 2, notes: [60, 67, 0, 0], time: 0, flags: 1, vel: 99 })));
  const g2 = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const s1 = E.parse[C.STEP_GET](await rq(E.req.stepGet(5)));
  ok(w.track === 2 && g2.n === 2 && g2.notes[1] === 67 && g2.vel === 99 && s1.n === 0, "tracks: TRACK_STEP set / get on another track");
  /* level / mute, track 4 as any other */
  const mx = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0, 90, 1)));
  const mxd = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(3, 64, 0)));
  const mx2 = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0)));
  const tr2 = E.parse[C.TRACK](await rq(E.req.track()));
  ok(mx.level === 90 && mx.mute === 1 && mx2.level === 90 && mxd.level === 64 && m.state.tracks[3].p[0] === 64 && tr2.tracks[3].level === 64
    && tr2.tracks[0].level === 90 && tr2.tracks[0].mute === 1, "tracks: TRACK_MIX level / mute (track 4: its own LEVEL)");
  /* track 4 has a sound: stored and loaded as any other */
  await rq(E.req.track(3));
  const dd = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const us = E.parse[C.UP_STORE](await rq(E.req.upStore(20, "X"), { timeout: 2500, retries: 0 }));
  const ul = E.parse[C.UP_LOAD](await rq(E.req.upLoad(1), { timeout: 2500, retries: 0 }));
  const dl = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(dd.engine === H("808") && us.rc === 0 && ul.rc === 0 && dl.engine === H("FM6"), "tracks: track 4 selected -> DUMP engine 808, UP_STORE / UP_LOAD rc 0");
  /* pushes carry the selected track */
  await rq(E.req.track(0));
  ok(await E.startWatch(rq), "tracks: WATCH on");
  m.sim.track(2);
  m.sim.step(4);
  await sleep(10);
  const rl = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(rl && E.parse[C.RELOAD](rl.a).track === 2 && sc && E.parse[C.STEP_CHANGED](sc.a).track === 2 && E.parse[C.STEP_CHANGED](sc.a).index === 4,
    "tracks: RELOAD / STEP_CHANGED carry the selected track");
  /* projects keep all four tracks */
  await rq(E.req.project(1, 3), { timeout: 4000, retries: 0 });
  await rq(E.req.trackStep(2, 5, { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 }));
  await rq(E.req.track(0));
  await rq(E.req.project(0, 3), { timeout: 4000, retries: 0 });
  const back = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track())).sel;
  ok(back.n === 2 && back.notes[0] === 60 && sel === 2, "tracks: PROJECT save / load keeps every track and the selection");
  /* older firmware: no NTRK in INFO, no RELOAD track byte */
  const o = attachMock({ legacy: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  ok(oi.ntrk === 0 && E.parse[C.RELOAD]([0, 4]).track === 0 && E.parse[C.STEP_CHANGED]([7]).track === 0, "tracks: older firmware parses (no tracks)");
  o.done();
  done();
}

/* ------------------------------------------------ editor v3: the mixer (Tracks tab) --- */
async function editorMixer() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const PAN = 39, MUTE = 40;
  const m0 = await E.mixer.read(rq, info, { pan: PAN });
  ok(m0.ntrk === 4 && m0.tracks.length === 4 && m0.tracks[1].pan === -24 && m0.tracks[2].pan === 20 && m0.tracks[3].engine === engineHandles(info)("808")
    && m0.tracks.every((x) => Number.isInteger(x.level) && (x.mute === 0 || x.mute === 1)), "mixer: read = TRACK + pan of every track (TRACK_DUMP)");
  /* level / mute of tracks that are not selected (clamped) */
  const a = await E.mixer.setMix(rq, 2, 70, 1);
  const b = await E.mixer.setMix(rq, 3, 200, 0);
  const m1 = await E.mixer.read(rq, info, { pan: PAN });
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(a.level === 70 && a.mute === 1 && b.level === 127 && m.state.tracks[3].p[0] === 127 && m1.tracks[3].level === 127
    && m1.tracks[2].level === 70 && m1.tracks[2].mute === 1
    && td2.p[0] === 70 && td2.p[MUTE] === 1 && m1.sel === 0, "mixer: TRACK_MIX level / mute round trip (clamped)");
  /* pan of another track: selected for the SET, the selection put back, no RELOAD pushed */
  ok(await E.startWatch(rq), "mixer: WATCH on");
  const pushes = ev.pushes.length;
  /* (the v3 path: firmware 0.8 has no TRACK_PARAM) */
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40);
  const p0 = await E.mixer.setPan(rq, 0, 0, PAN, 99);
  await sleep(10);
  const m2 = await E.mixer.read(rq, info, { pan: PAN });
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(p2 === -40 && p0 === 63 && m2.sel === 0 && m2.tracks[2].pan === -40 && m2.tracks[0].pan === 63 && d0.p[PAN] === 63 && m2.tracks[1].pan === -24
    && ev.pushes.length === pushes, "mixer: pan of any track via SET (other track selected for a moment, then back; no push)");
  /* the device's TRACKS page: level of the selected track pushes CHANGED; REC arm shows in TRACK */
  m.sim.level(33);
  m.sim.arm(1);
  await sleep(10);
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  const m3 = await E.mixer.read(rq, info, { pan: PAN });
  ok(ch && ch.scope === 0 && ch.id === 0 && ch.value === 33 && m3.tracks[0].level === 33 && m3.tracks[1].armed === 1 && m3.tracks[0].armed === 0,
    "mixer: device-side level (CHANGED push) and REC arm read back");
  await rq(E.req.track(3));
  m.sim.level(90);
  await sleep(10);
  const chd = E.parse[C.CHANGED](ev.pushes.filter((f) => f.cmd === C.CHANGED).pop().a);
  ok(chd.scope === 0 && chd.id === 0 && chd.value === 90, "mixer: track 4's level on the device pushes its LEVEL (P_LEVEL)");
  /* track 4's demo beat: the DRUM kit's lane hits and accents */
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, i))));
  ok(st.every((s) => s.n === 0) && st[0].hit === 1 && st[0].acc === 1 && st[1].hit === 8
    && st[4].hit === 2 && st[4].acc === 2 && st[14].hit === 16,
    "mixer: the mock's track 4 holds the BEAT grid (KICK CHH ... SNARE ... OHH)");
  ok(JSON.stringify(E.parseNotes("C4 E4 G4")) === "[60,64,67]" && JSON.stringify(E.parseNotes("36 38")) === "[36,38]" && E.parseNotes("KICK") === null,
    "mixer: notes parse as numbers or note names (no GM drum names)");
  done();
}

/* ------------------------------------- editor v4: TRACK_PARAM and TRACK_CHANGED --- */
async function editorTrackParam() {
  const C = E.CMD, PAN = 39, MUTE = 40;
  const { m, rq, sent, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const w1 = E.parse[C.WATCH](await rq(E.req.watch(1)));
  m.sim.param(2, PAN, 11);
  await sleep(10);
  ok(w1.on === 1 && !ev.pushes.some((f) => f.cmd === C.TRACK_CHANGED), "v4: WATCH 1 answers 1 as before (no TRACK_CHANGED pushes)");
  ok(await E.startWatch(rq) === 3, "v4: WATCH 3 -> 3 (TRACK_PARAM / TRACK_CHANGED known)");
  const tracks0 = sent[C.TRACK] || 0, pushes = ev.pushes.length;
  const g = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, PAN)));
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40, true);
  const p1 = await E.mixer.setPan(rq, 1, 0, PAN, 99, true);
  const alg = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, info.pe0, 50)));   /* track 2 is FM6 PAD: ALG 0..32 */
  const lv = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(3, 0, -5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track()));
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  await sleep(10);
  ok(g.track === 1 && g.id === PAN && g.value === -24 && p2 === -40 && p1 === 63 && alg.value === 32 && lv.value === 0 && td2.p[PAN] === -40
    && sel.sel === 0 && (sent[C.TRACK] || 0) === tracks0 + 1 && ev.pushes.length === pushes,
    "v4: TRACK_PARAM get / set on other tracks (clamped as SET, selection kept, no push)");
  const bad = await rq(E.req.trackParam(4, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(bad === "none", "v4: TRACK_PARAM of track 5: no reply");
  await rq(E.req.set(0, 8, 3));
  const sharedQnt = await Promise.all([0, 1, 2, 3].map(async (track) =>
    E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(track, 8))).value));
  ok(sharedQnt.every((q) => q === 3), "recording QNT: SET shares one project grid across tracks");
  await rq(E.req.trackParam(2, 8, 6));
  const sharedTrackQnt = await Promise.all([0, 1, 2, 3].map(async (track) =>
    E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(track, 8))).value));
  ok(sharedTrackQnt.every((q) => q === 6), "recording QNT: TRACK_PARAM updates the project grid from another track");
  /* device-side changes: CHANGED for the selected track, TRACK_CHANGED for the others */
  m.sim.param(2, PAN, 30);
  m.sim.param(3, MUTE, 1);
  m.sim.param(0, PAN, -7);
  await sleep(10);
  const tc = ev.pushes.filter((f) => f.cmd === C.TRACK_CHANGED).map((f) => E.parse[C.TRACK_CHANGED](f.a));
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  ok(tc.length === 2 && tc[0].track === 2 && tc[0].id === PAN && tc[0].value === 30 && tc[1].track === 3 && tc[1].id === MUTE && tc[1].value === 1
    && ch && ch.scope === 0 && ch.id === PAN && ch.value === -7 && !ev.unknown.length, "v4: TRACK_CHANGED pushes for the other tracks, CHANGED for the selected one");
  done();
  /* firmware 0.8 (v3): WATCH 3 answers 1, TRACK_PARAM unanswered: the editor keeps the select / restore path */
  const o = attachMock({ v3: true, watchMs: 1000 });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const on = await E.startWatch(o.rq);
  const tp = await o.rq(E.req.trackParam(1, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  const pv = await E.mixer.setPan(o.rq, 2, 0, PAN, 5, false);
  ok(on === 1 && tp === "none" && pv === 5 && o.ev.timeouts === 0 && !o.ev.unknown.length, "v4: v3 firmware -> WATCH 1, no TRACK_PARAM (pan by select / restore)");
  o.done();
}

/* -------------------------------------------------------------- the synths --- */
/* A synth's pages, patches and SysEx through its schema (85..88, editor_synth.c), against the mock with the
   firmware's schemas: no synth's own code in the editor */
async function synthTests() {
  if (!SCHEMAS) { console.log("synths (no build/host/desc.json)".padEnd(64) + " skip"); return; }
  const FM = HN("FM6"), CZ = HN("CZ-1"), P5 = HN("PROPHET"), SID = HN("SID");   /* (the schemas: by handle) */
  const KITS = KIT_NAMES.map((n) => FW_NAMES.indexOf(n));                         /* (the kit synths: the firmware's handles) */
  const schemas = Object.fromEntries(Object.entries(SCHEMAS).map(([e, b]) => [e, Uint8Array.from(b)]));
  const parsed = new Map(Object.entries(schemas).map(([e, b]) => [+e, E.SYNTH.parse(b)]));
  const bad = [];
  for (const [e, s] of parsed) {
    if (s.def.length !== s.head.patch) bad.push(`${e}: default`);
    for (const p of s.params.values()) {
      if (p.kind !== 0 || p.off >= s.def.length) continue;
      const b = s.def.slice();
      E.SYNTH.put(p, b, null, E.SYNTH.get(p, b, null));
      if (!eq(b, s.def)) bad.push(`${e}: ${p.id}`);
    }
  }
  ok(parsed.size === 4 + KITS.length && KITS.every((e) => parsed.has(e)) && !bad.length, `synths: schemas ${[...parsed.keys()].join(" ")} parse, every field reads and writes back${bad.length ? " (" + bad.slice(0, 3) + ")" : ""}`);
  {   /* a kit synth's pads and stored packing, from its schema alone (each kit's: 1.x's KIT wire, 54 bytes) */
    const kit = [...parsed].filter(([, s]) => s.pads).map(([e]) => e), d = parsed.get(KITS[0]);
    const zero = (() => { const r = new Array(E.SYNTH.packedLen(d)).fill(0); let pos = 0;   /* (every offset 0: TUNE 24, the others 64) */
      const put = (v, w) => { for (let i = 0; i < w; i++, pos++) if ((v >> i) & 1) r[pos >> 3] |= 1 << (pos & 7); };
      for (let g = 0; g < 16; g++) { put(24, 6); put(64, 7); put(64, 7); put(0, 7); } return r; })();
    const far = zero.slice(); far[0] = (far[0] & ~63) | 49;  /* (TUNE +25: past its range) */
    const each = KITS.every((e) => { const k = parsed.get(e);
      return k && k.pads.lanes.join() === "KICK,SNARE,CLAP,HATCL,HATOP,TOM,RIM,BELL" && E.SYNTH.packedLen(k) === 54 &&
             E.SYNTH.packedOk(k, zero) && E.SYNTH.unpack(k, zero).every((v) => v === 0) && !E.SYNTH.packedOk(k, far); });
    ok(d && kit.length === KITS.length && KITS.every((e) => kit.includes(e)) && each && !E.SYNTH.packedOk(parsed.get(SID), zero),
       "synths: each kit synth's pads and stored packing read from its schema (808 .. SAMPLER: 54 bytes, its checks; SID: none)");
  }
  const shown = [FM, CZ, P5].map((e) => [...E.SYNTH.sections(parsed.get(e))].length);
  ok(shown.every((n) => n > 1), `synths: FM6, CZ-1 and PROPHET pages from their schemas (${shown} sections)`);

  E.SYNTH.cache.clear();
  const { m, rq, done } = attachMock({ shared: true, schemas });
  const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
  ok(info.synths, "synths: INFO's 0x59 tag (the synths' commands)");
  let n = 0;
  const rqc = (r, o) => { n++; return rq(r, o); };
  const fm = await E.synthSchema(rqc, FM), pages = n, again = await E.synthSchema(rqc, FM);
  ok(!!fm && fm === again && pages === 1 + Math.ceil(schemas[FM].length / 840) && n === pages + 1 && fm.title === parsed.get(FM).title,
     `synths: a schema's key, then its bytes in ${pages - 1} pages; again: the key only (the cache)`);
  E.SYNTH.cache.set(CZ, { key: (schemas[CZ][48] | schemas[CZ][49] << 8 | schemas[CZ][50] << 16 | schemas[CZ][51] << 24) >>> 0, schema: parsed.get(CZ) });
  n = 0;
  ok(await E.synthSchema(rqc, CZ) === parsed.get(CZ) && n === 1, "synths: a schema seeded by its key (Studio's bundle): one request");
  ok(await E.synthSchema(rq, 0) === null && await E.synthSchema(rq, info.midi) === null, "synths: no synth (handle 0) and the MIDI Track have no schema");

  const part = 1, p5s = await E.synthSchema(rq, P5), czs = await E.synthSchema(rq, CZ);   /* (the CZ-1's values are its module's: CALL) */
  const p = [...p5s.params.values()].find((q) => q.kind === 0 && q.max > q.min && !(q.flags & 4) && E.SYNTH.get(q, p5s.def, null) !== q.max);
  const put = E.parse[E.CMD.PATCH](await rq(E.req.patch(1, 0, P5, part, p5s.def)));
  const set = E.parse[E.CMD.PARAM](await rq(E.req.paramSet(part, p.id, p.max)));
  const get = E.parse[E.CMD.PARAM](await rq(E.req.paramGet(part, [p.id], true)));
  const got = E.parse[E.CMD.PATCH](await rq(E.req.patch(0, 0, 127, part)));
  ok(!put.rc && m.state.tracks[part].engine === P5 && set.value === p.max && get.engine === P5 && get.values[0] === p.max &&
     got.engine === P5 && E.SYNTH.get(p, got.bytes, null) === p.max, `synths: PATCH puts a part's patch, PARAM sets and reads ${p.label || p.name} by id`);
  await rq(E.req.track(part));
  const cap = (await E.capturePatch(rq, info, "CAP")).patch;
  ok(cap.engine === P5 && eq(cap.native, E.SYNTH.sound(p5s, got.bytes)), "synths: library capture keeps the part's sound");
  const voice = E.SYNTH.sound(fm, fm.def);
  voice[0] = 99;
  await E.auditionPatch(rq, info, { engine: FM, native: voice, p: [] }, { track: part });
  const back = E.parse[E.CMD.PATCH](await rq(E.req.patch(0, 0, FM, part)));
  ok(voice.length === fm.head.sound && voice.length < fm.head.patch && back.bytes.length === fm.head.patch && eq(back.bytes.slice(0, voice.length), voice),
     "synths: audition puts a slot's sound onto the part's whole patch");
  await E.bank.put(rq, 383, { engine: FM, name: "WHOLE", native: back.bytes }, info);
  ok(eq((await E.bank.get(rq, info, 383)).native, voice), "synths: a user slot keeps a whole patch's sound");
  const dump = E.parse[E.CMD.PATCH](await rq(E.req.patch(3, 0, 127, part)));
  ok(!dump.rc && dump.frame[0] === 0xF0 && dump.frame.at(-1) === 0xF7, "synths: PATCH DUMP: the part's patch as its synth's SysEx");
  ok(E.req.patchBank(FM, 1008, [383, 5])[1].join() === [4, 1, FM, 1008 & 127, 1008 >> 7, 383 & 127, 383 >> 7, 5, 0].join() &&
     E.parse[E.CMD.PATCH]([4, 1, FM, 0, 0, 0, 4102 & 127, 4102 >> 7, 3, 0, 0x43, 0, 9]).bytes.join() === "67,0,9" &&
     (await E.bankDump(rq, FM, [383])) === null, "synths: PATCH BANK: its request, a piece's reply, null where the synth has no bank dump");
  const frame = [0xF0, ...fm.sysex[0], 0, 0x7F, 0xF7];
  const sx = await E.sendSysex(rq, frame, 384), unk = await E.sendSysex(rq, [0xF0, 0x7D, 1, 0xF7], 384);
  const into = await E.sendSysex(rq, [...frame, ...frame], 300);
  ok(sx[0].engine === FM && unk[0].engine === -1 && into.length === 2 && m.state.bank[300]?.engine === FM && m.state.bank[301]?.engine === FM,
     "synths: SysEx files go to the synth their patterns name, kept from the slot asked for");
  const ctx = { keys: Array.from({ length: info.pcount }, (_, i) => `P${i}`), pe0: info.pe0, engines: info.engines };
  const lib = E.readLibraryFile(E.libraryFile("patch", [cap, { ...cap, native: null, sysex: frame }], ctx), ctx).patches;
  ok(eq(lib[0].native, cap.native) && !lib[1].native && eq(lib[1].sysex, frame), "synths: library files keep a synth's sound and its SysEx");
  /* (a 1.x file: its engine number 15 was the CZ-1's then; the name says which synth) */
  const old = E.readLibraryFile({ format: "felucca-patch", engine: 15, engineName: "CZ-1", p: Array(info.pcount).fill(0), cz: czs.def }, ctx).patches[0];
  ok(old.engine === CZ && eq(old.native, czs.def), "synths: Melodee 1.x sound files (p5 / cz / fm6) read as the synth's bytes");
  E.SYNTH.cache.clear();
  done();
}

/* ------------------------------------------------- editor tabs and strings --- */
async function editorSong() {
  const m = E.makeMockDevice({ auto: false });
  const link = new E.Link((d) => [...m.access.outputs.values()][0].send(d), { timeout: 100 });
  [...m.access.inputs.values()][0].onmidimessage = (e) => link.receive(e.data);
  const rq = (r) => link.request(r);
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.chainRows === 16, "SONG: INFO capability appended");
  const ask = async (op = 0, rows = []) => E.parse[C.SONG](await rq(E.req.song(op, rows)));
  const rows = [{ slot: 0, repeat: 4 }, { slot: 1, repeat: 2 }, { slot: 0, repeat: 1 }];
  let r = await ask(1, rows);
  ok(r.rc === 0 && JSON.stringify(r.rows) === JSON.stringify(rows), "SONG: rows and repeats round trip");
  r = await ask(2);
  ok(r.rc === 4 && !r.playing, "SONG: missing source rejected before PLAY");
  await rq(E.req.project(1, 1));
  r = await ask(2);
  ok(!r.rc && r.playing && r.row === 0 && r.remaining === 4, "SONG: PLAY starts from row 1");
  r = await ask(1, [{ slot: 0, repeat: 1 }]);
  ok(r.rc === 2 && r.count === 3, "SONG: editing during PLAY refused, rows kept");
  await ask(3);
  r = E.parse[C.SONG](await rq([C.SONG, [1, 1, 0, 0]]));
  ok(r.rc === 1 && r.count === 3, "SONG: zero repeats rejected without changing rows");
  r = E.parse[C.SONG](await rq([C.SONG, [1, 2, 0, 1]]));
  ok(r.rc === 1 && r.count === 3, "SONG: short row list rejected");
  await rq(E.req.project(1, 2));
  await ask(1, []);
  await rq(E.req.project(0, 2));
  r = await ask();
  ok(JSON.stringify(r.rows) === JSON.stringify(rows) && !r.playing, "SONG: project save/load recalls the chain");
  await ask(1, []);
  r = await ask(2);
  ok(r.rc === 1 && !r.playing, "SONG: empty chain does not PLAY");
  link.close(); m.stop();
  const old = E.makeMockDevice({ auto: false, v5: true });
  const l = new E.Link((d) => [...old.access.outputs.values()][0].send(d));
  [...old.access.inputs.values()][0].onmidimessage = (e) => l.receive(e.data);
  ok(E.parse[C.INFO](await l.request(E.req.info())).chainRows === 0, "SONG: older firmware has no capability");
  l.close(); old.stop();
}

/* synth modules (cmd 84): a .mmod file's head, an upload in 256-byte pieces, the list, a taken number, delete */
async function editorModules() {
  const m = E.makeMockDevice({ auto: false });
  const link = new E.Link((d) => [...m.access.outputs.values()][0].send(d), { timeout: 200 });
  [...m.access.inputs.values()][0].onmidimessage = (e) => link.receive(e.data);
  const rq = (r, o) => link.request(r, o);
  /* (uid: an IDNT chunk naming it, mod_abi3.h: Melodee 2.0 knows a synth by its ID alone) */
  const file = (engine, name, n, abi = 3, uid = 0n) => {
    const b = new Uint8Array(60 + n + (uid ? 104 : 0)), v = new DataView(b.buffer);
    v.setUint32(0, 0x31444D4D, true); v.setUint16(4, abi, true); b[6] = engine; v.setUint32(8, b.length, true);
    v.setUint32(12, n, true); [...name].forEach((c, i) => { b[32 + i] = c.charCodeAt(0); }); v.setUint16(48, 2, true); b[50] = 5;
    v.setUint32(52, 0, true); v.setUint16(56, 1, true);
    for (let i = 60; i < 60 + n; i++) b[i] = (i * 37) & 255;
    if (uid) { b[7] |= 1; v.setUint32(60 + n, 0x544E4449, true); v.setUint32(64 + n, 96, true); v.setBigUint64(68 + n, uid, true); b[76 + n] = 2; }
    return b;
  };
  let bad = 0; try { E.MODS.head(new Uint8Array(60)); } catch (e) { bad++; }
  try { E.MODS.head(file(3, "SID", 500, 2)); } catch (e) { bad++; }     /* (a native ABI 2 file: an older firmware's) */
  const f = file(6, "TRIO", 992, 3, 0x7106n), h = E.MODS.head(f);
  ok(bad === 2 && h.engine === 6 && h.name === "TRIO" && h.version === 2 && h.npresets === 5 && h.total === 1156 && h.key === 0 && h.host === 1,
     "modules: an ABI 3 .mmod head read, a foreign file and an older firmware's refused");
  /* (tools/mod_build.py: the C head's layout; its engine byte means nothing since 2.0, the device gives a handle) */
  const built = join(HERE, "..", "build", "modules", "trio.mmod");
  if (existsSync(built)) {
    const t = E.MODS.head(new Uint8Array(readFileSync(built)));
    ok(t.name === "TRIO" && t.version === 1 && t.npresets === 5 && t.key === 0 && t.host === 4,
       "modules: the built TRIO file's head reads as the firmware's (an SDK module: no key, host table 4)");
  }
  const sid = join(HERE, "..", "build", "modules", "sid.mmod");     /* an engine-source module (modules/sdk/mod_engine.h) */
  if (existsSync(sid)) {
    const t = E.MODS.head(new Uint8Array(readFileSync(sid)));
    ok(t.name === "SID" && t.key === 0 && t.host === 4 && t.npresets === 32, "modules: the built SID file's head reads as the firmware's (no key, host table 4)");
  }
  /* the kit synths (2.0: DRUM's kits, each a module; their pads and SAMPLER's kits: the host table's since host 4) */
  const kits = [["808", "808", 1], ["909", "909", 1], ["sidkit", "SID KIT", 2], ["cr78", "CR78", 1], ["fmkit", "FM KIT", 1], ["sampler", "SAMPLER", 8]];
  const kitFiles = kits.map(([stem]) => join(HERE, "..", "build", "modules", stem + ".mmod"));
  if (kitFiles.every((f) => existsSync(f))) {
    const heads = kitFiles.map((f) => E.MODS.head(new Uint8Array(readFileSync(f))));
    ok(heads.every((t, i) => t.name === kits[i][1] && t.key === 0 && t.host === 4 && t.npresets === kits[i][2]) &&
       !existsSync(join(HERE, "..", "build", "modules", "drum.mmod")),
       "modules: the built kit synths' files (808, 909, SID KIT, CR78, FM KIT, SAMPLER) read as the firmware's (no key: their pads in their schemas, SAMPLER's kits the host's data records); no DRUM file");
  }
  let list = await E.MODS.list(rq);
  ok(!list.rc && !list.skipped && list.slots.length === 8 && list.slots.every((x) => !x.len), "modules: eight empty slots listed");
  const r = await E.MODS.upload(rq, f, null);
  list = await E.MODS.list(rq);
  ok(!r.rc && r.slot === 0 && r.engine === 12 && list.slots[0].len === 1156 && list.slots[0].name === "TRIO" && list.slots[0].state === 1,
     "modules: upload in pieces, committed, listed with its name and size, the first free handle (12: past the manifest's)");
  const again = await E.MODS.upload(rq, file(22, "J106", 500, 3, 0x7106106n), null);
  const clash = await E.MODS.upload(rq, file(22, "J106 B", 400, 3, 0x7106106n), 2);
  ok(!again.rc && again.slot === 1 && again.engine === 13 && clash.rc === 6, "modules: the next empty slot; a synth (its ID) another slot has refused");
  const replaced = await E.MODS.upload(rq, file(6, "TRIO", 800, 3, 0x7106n), null);
  ok(!replaced.rc && replaced.slot === 0, "modules: the same synth (its ID) again replaces its slot");
  ok(!(await E.MODS.remove(rq, 1)) && !(await E.MODS.list(rq)).slots[1].len, "modules: delete empties the slot");
  link.close(); m.stop();
}

/* Run the page's actual session helpers and connect-load path with delayed replies. */
async function editorSessions() {
  const state = html.slice(html.indexOf("let busy = 0;"), html.indexOf("let dragging", html.indexOf("let busy = 0;")));
  const requests = html.slice(html.indexOf("function assertSession(d)"), html.indexOf("/* the whole connect sequence */"));
  const loading = html.slice(html.indexOf("async function load(d)"), html.indexOf("async function getDescOrNull"));
  const library = html.slice(html.indexOf("async function libOp(fn)"), html.indexOf("const checkRc", html.indexOf("async function libOp(fn)")));
  let shown = 0, adopted, adoptStarted;
  const nodes = new Map();
  const noop = () => {};
  const S = vm.runInNewContext(`let dev = null, lastDump = 0, libBusy = false; ${state} ${requests} ${loading} ${library}
    ;({ beginBusy, resetBusy, sessionRequest, load, libOp,
       setDevice: d => { dev = d; }, count: () => busy })`, {
    Date, Error,
    $: id => { if (!nodes.has(id)) nodes.set(id, {}); return nodes.get(id); },
    CMD: { INFO: 1, PROJECT: 2, SMP_INFO: 3, DUMP: 4 },
    parse: { 1: r => r, 2: () => ({ used: false }), 3: () => ({}), 4: r => r },
    req: { info: () => "info", project: () => "project", smpInfo: () => "sample", dump: () => "dump" },
    paramKeys: noop, libAdopt: () => { adoptStarted?.(); return adopted || Promise.resolve(); },
    buildUI: () => { shown++; }, renderPanels: noop, loadSteps: noop,
    startWatch: async () => false, renderLive: noop, renderLib: noop,
    renderBank: noop, renderSysinfo: noop, sayK: noop, report: noop, syncPreferences: noop,
    MODS: { list: async () => null }, renderModules: noop,
  });
  const oldRelease = S.beginBusy();
  S.resetBusy();
  const newRelease = S.beginBusy();
  oldRelease();
  ok(S.count() === 1, "editor: old completion cannot release new connection's busy count");
  newRelease(); newRelease(); oldRelease();
  ok(S.count() === 0, "editor: operation release is idempotent and never goes negative");

  let resolve;
  const d = { link: { request: () => new Promise(r => { resolve = r; }) } };
  S.setDevice(d);
  const stale = S.sessionRequest(d, "dump").then(() => null, e => e);
  S.setDevice({}); resolve({});
  ok((await stale)?.message === "closed", "editor: late reply from an unplugged device is rejected");
  let sent = 0;
  d.link.request = async () => { sent++; };
  const wrong = await S.sessionRequest(d, "write").then(() => null, e => e);
  ok(wrong?.message === "closed" && sent === 0, "editor: captured request cannot write through a replacement connection");

  const old = { link: { request: () => new Promise(r => { resolve = r; }) } };
  S.setDevice(old);
  const loadingOld = S.load(old).then(() => null, e => e);
  S.resetBusy(); S.setDevice({});
  const currentRelease = S.beginBusy();
  resolve({});
  ok((await loadingOld)?.message === "closed" && !shown && S.count() === 1,
     "editor: interrupted initial load cannot paint or unlock a new session");
  currentRelease();

  adopted = new Promise(r => { resolve = r; });
  const reachedAdoption = new Promise(r => { adoptStarted = r; });
  const next = { pdesc: [], gdesc: [], names: [], slotUsed: [], link: { request: async kind =>
    kind === "info" ? { pcount: 0, gcount: 0, nengines: 0 } : {} } };
  S.setDevice(next);
  const loadingNext = S.load(next).then(() => null, e => e);
  await reachedAdoption;
  S.resetBusy(); S.setDevice({}); resolve();
  ok((await loadingNext)?.message === "closed" && !shown && S.count() === 0,
     "editor: unplug during library adoption aborts the old load");
  adopted = null;
  S.setDevice(next);
  await S.load(next);
  ok(shown === 1 && S.count() === 0, "editor: a current connection still completes its initial load");

  const savedDevice = { link: { request: async () => { sent++; } } };
  sent = 0;
  S.setDevice(savedDevice);
  const storageWait = new Promise(r => { resolve = r; });
  const copying = S.libOp(async (device, request) => { await storageWait; await request("save"); });
  S.resetBusy(); S.setDevice({}); resolve();
  await copying;
  ok(!sent && S.count() === 0, "editor: library storage delay cannot send a save to a new device");
}

function editorTabs() {
  const tabs = [...html.matchAll(/<button role="tab" data-tab="(\w+)"/g)].map((x) => x[1]);
  const panels = [...html.matchAll(/<section class="panel" id="p-(\w+)" data-tab="(\w+)"/g)].map((x) => [x[1], x[2]]);
  const TABS = JSON.parse((/const TABS = (\[[^\]]*\]);/.exec(html) || [])[1] || "[]");
  ok(tabs.length === 6 && js(tabs) === js(TABS) && js(panels.map((x) => x[1])) === js(TABS) && panels.every(([a, b]) => a === b),
    `editor: ${tabs.length} tabs, one panel each (${tabs.join(" ")})`);
  ok(/localStorage\.setItem\(TAB_KEY/.test(html) && /try \{ localStorage/.test(html) && /history\.replaceState\([^)]*"#" \+ name\)/.test(html)
    && /addEventListener\("hashchange"/.test(html), "editor: last tab in localStorage (try/catch) and in the URL hash");
  /* every string key in both languages */
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const ja = new Set(Object.keys(TEXT.ja)), en = new Set(Object.keys(TEXT.en));
  const used = new Set([...html.matchAll(/data-t="(\w+)"|\bt\("(\w+)"\)|sayK\("(\w+)"|hint = "(\w+)"/g)].map((x) => x[1] || x[2] || x[3] || x[4]));
  for (const k of ["needDevice", "smpNone", "bankConnect", "bankNone", "selectedTrack", "selectTrack", "notesHelp", "live", "polling"]) used.add(k);
  const miss = [...used].filter((k) => !ja.has(k) || !en.has(k));
  const odd = [...ja].filter((k) => !en.has(k)).concat([...en].filter((k) => !ja.has(k)));
  ok(!miss.length && !odd.length, `editor: every string in ja and en (${used.size} used${miss.length ? ", missing " + miss : ""}${odd.length ? ", one language only " + odd : ""})`);
  ok(!/\b(P5|CZ|FM6|FM4)\.[A-Za-z_]/.test(html) && !/id="(p5panel|czpanel|czbankpanel|p-fm6)"/.test(html),
    "editor: no synth's own page code (a synth's pages are its schema's)");
  /* the page script parses (the browser's view of it) */
  const script = html.slice(html.indexOf("<script>") + 8, html.lastIndexOf("</script>"));
  let err = null;
  try { new vm.Script(script); } catch (e) { err = e.message; }
  ok(!err, "editor: page script compiles" + (err ? ` (${err})` : ""));
  ok(!/#[0-9a-f]{3,6}\b/i.test(html.slice(html.indexOf("[hidden]") - 6000, html.indexOf("[hidden]")).replace(/:root[^}]*\}/g, "")),
    "editor: no colours beyond the black / white tokens in the new styles");
}

/* ------------------------------------------------- editor icons (Fukiai) --- */
function editorIcons() {
  const blk = html.slice(html.indexOf("const GLYPH = {"), html.indexOf("};", html.indexOf("const GLYPH = {")));
  const names = new Set([...blk.matchAll(/(\w+): 0x[0-9A-F]{4}/g)].map((m) => m[1]));
  const used = new Set([...html.matchAll(/data-ic="(\w+)"|ic: "(\w+)"|: "((?:waveform|function|symbol|control|port|ui|note)_\w+)"/g)].map((m) => m[1] || m[2] || m[3]));
  const missing = [...used].filter((n) => !names.has(n));
  ok(names.size > 0 && !missing.length, `editor: every icon name is in GLYPH (${used.size} used${missing.length ? ", missing " + missing : ""})`);
  const ttf = existsSync(join(HERE, "fukiai.ttf")) && readFileSync(join(HERE, "fukiai.ttf"));
  ok(ttf && ttf.readUInt32BE(0) === 0x00010000 && existsSync(join(HERE, "FUKIAI-LICENSE.txt")) && html.includes('href="FUKIAI-LICENSE.txt"'),
    "editor: fukiai.ttf and FUKIAI-LICENSE.txt next to editor.html");
  ok(/html:not\(\.fk\) \.ic \{ display: none; \}/.test(html) && html.includes('classList.add("fk")'), "editor: icons hidden until the font has loaded");
}

/* ------------------------------------------- user samples: JS == sampleio.py --- */
function wav(sr, ch, bits, float, frames, f) {
  const bps = bits / 8, data = Buffer.alloc(frames * ch * bps);
  for (let i = 0; i < frames; i++) for (let c = 0; c < ch; c++) {
    const v = f(i, c), o = (i * ch + c) * bps;
    if (float) data.writeFloatLE(v, o);
    else if (bits === 8) data[o] = Math.max(0, Math.min(255, Math.round(v * 127 + 128)));
    else if (bits === 16) data.writeInt16LE(Math.round(v * 32000), o);
    else if (bits === 24) data.writeIntLE(Math.round(v * 8000000), o, 3);
  }
  const fmt = Buffer.alloc(16);
  fmt.writeUInt16LE(float ? 3 : 1, 0); fmt.writeUInt16LE(ch, 2); fmt.writeUInt32LE(sr, 4);
  fmt.writeUInt32LE(sr * ch * bps, 8); fmt.writeUInt16LE(ch * bps, 12); fmt.writeUInt16LE(bits, 14);
  const chunk = (id, b) => Buffer.concat([Buffer.from(id), Buffer.from(Uint32Array.of(b.length).buffer), b]);
  const body = Buffer.concat([Buffer.from("WAVE"), chunk("fmt ", fmt), chunk("data", data)]);
  return Buffer.concat([Buffer.from("RIFF"), Buffer.from(Uint32Array.of(body.length).buffer), body]);
}

function samplesMatch() {
  const dir = mkdtempSync(join(tmpdir(), "melodee-web-"));
  const files = [
    ["tone_A4.wav", wav(44100, 1, 16, false, 9000, (i) => Math.sin(i * 0.0627) * Math.exp(-i / 4000))],
    ["pad C3.wav", wav(48000, 2, 24, false, 7000, (i, c) => Math.sin(i * (c ? 0.031 : 0.0313)) * 0.7)],
    ["Bb2 float.wav", wav(22050, 1, 32, true, 5000, (i) => ((i % 97) / 48 - 1) * 0.5)],
    ["BD1 lofi.wav", wav(96000, 1, 8, false, 12000, (i) => Math.sin(i * 0.01) * Math.exp(-i / 3000))],
  ].map(([n, b]) => { const p = join(dir, n); writeFileSync(p, b); return p; });
  const zones = files.map((p) => {
    const w = E.parseWav(readFileSync(p));
    const s = E.normalize(E.resample(w.x, w.sr, E.SMP.RATE));
    return { s, root: E.rootFromName(p.split("/").pop().replace(/\.[^.]*$/, "")) };
  });
  const js = E.buildSlot("Mix ä 12345", zones);
  execFileSync("python3", [join(HERE, "../tools/fm1_sample_upload.py"), "build", "Mix ä 12345", join(dir, "slot"), ...files]);
  const pyHdr = readFileSync(join(dir, "slot.hdr")), pyData = readFileSync(join(dir, "slot.bin"));
  ok(eq(js.hdr, pyHdr) && eq(js.data, pyData), `samples: editor == sampleio.py (${files.length} WAV formats, ${js.data.length} B)`);
  /* recording / trimming: takeSample (a cut of the raw input, faded at the cuts, normalised), autoTrim */
  const R = E.SMP.RATE, raw = new Float64Array(R);       /* 1 s: silence, a tone from 0.25 to 0.6 s, silence */
  for (let i = Math.round(R * 0.25); i < Math.round(R * 0.6); i++) raw[i] = Math.sin(i * 0.2) * 0.3;
  ok(eq(E.takeSample(raw, 0, raw.length), E.normalize(raw)) && eq(E.takeSample(raw, -9, 1e9), E.normalize(raw)),
    "samples: the whole input == normalize (files load as before); the ends are clamped");
  const [a, b] = E.autoTrim(raw), on = Math.round(R * 0.25), off = Math.round(R * 0.6);
  ok(a <= on && on - a <= Math.round(R * 0.005) + 2 && b >= off - 2 && b - off <= Math.round(R * 0.02) + 1,
    `samples: autoTrim finds the sound (${a}..${b} for ${on}..${off}: 5 ms before, 20 ms after)`);
  ok(E.autoTrim(new Float64Array(500)).join() === "0,500", "samples: autoTrim of silence keeps all");
  const cut = E.takeSample(raw, on + 1000, on + 3000);
  let pk = 0;
  for (const v of cut) pk = Math.max(pk, Math.abs(v));
  ok(cut.length === 2000 && cut[0] === 0 && Math.abs(cut[1999]) < 1000 && pk === 30000 &&
     Math.abs(cut[22]) < Math.abs(cut[22 + 63]) + 30000 * 0.6,
    "samples: a cut: its length, faded to 0 at both cut ends, peak normalised");

  /* the trimming view: zoom at the pointer, at least 256 samples, inside the input; pan; out to all */
  {
    const n = 22050;
    let v = E.zoomView(n, 0, n, 0.5, 0.5, 0);
    const z1 = v[1] - v[0] === 11025 && v[0] === 5513;                     /* x2 around the middle */
    v = E.zoomView(n, 0, n, 0.1, 0.25, 0);
    const under = Math.abs((v[0] + 0.1 * (v[1] - v[0])) - 0.1 * n) <= 1;    /* the pointer's sample stays */
    let w = [0, n];
    for (let i = 0; i < 60; i++) w = E.zoomView(n, w[0], w[1], 0.97, 0.8, 0);
    const minOk = w[1] - w[0] === 256 && w[1] <= n;
    const p = E.zoomView(n, 1000, 3000, 0.5, 1, 0.1), edge = E.zoomView(n, 21000, 22000, 0.5, 1, 1);
    const out = E.zoomView(n, 1000, 3000, 0.5, 100, 0);
    ok(z1 && under && minOk && p[0] === 1200 && p[1] === 3200 && edge[1] === n && out.join() === `0,${n}`,
      "samples: trim view: zoom at the pointer, 256 samples at least, inside the input, pan, out to all");
  }
}

/* ------------------------------------------------------- packages: JS == Python --- */
async function packages() {
  /* SMK-37 Pro: the stock v016 package has 36 interleave blocks (outside the repo, read only; skipped without it) */
  const v016 = join(process.env.SMK_STOCK_DIR || join(HERE, "no-stock-packages"), "SMK-37_Pro_016.fwsc");
  if (existsSync(v016)) {
    const raw = readFileSync(v016);
    const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(36)) + raw[36 * 48:])`, v016);
    ok(blockCount(raw) === 36 && eq(logicalImage(raw), logical) && productOf(raw) === "SMK-37 Pro_016",
      "fm1pkg.js: stock SMK-37_Pro_016.fwsc: 36 blocks, identity SMK-37 Pro_016");
    const st = await validateStockPackage(new Uint8Array(raw));
    ok(st.product === "SMK-37 Pro_016" && eq(st.image, logical) && st.head.from === 1024 && st.head.to === 1024 + 0x4000,
      "fm1pkg.js: the official SMK-37 Pro V016 is accepted for the return (CHECK HEAD range: flash.bin's first 16 KiB)");
  } else console.log("packages: SMK-37 Pro stock package skipped (SMK_STOCK_DIR not set)");
  const pkg = join(HERE, "../build/melodee.fwsc");
  if (!existsSync(pkg)) {
    console.log("packages: skipped (run ./build.sh first)");
    return;
  }
  const raw = readFileSync(pkg);
  const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, pkg);
  ok(eq(logicalImage(raw), logical), "fm1pkg.js logicalImage");
  const product = py(`import sys; sys.path.insert(0, "tools")
from fm1_install import product_of
print(product_of(open(sys.argv[1], "rb").read()), end="")`, pkg).toString();
  ok(/^FM-1_9\d{2,4}$/.test(productOf(raw)) && productOf(raw) === product,
    "fm1pkg.js productOf == Python (development and release identities)");
  /* an SMK-37 build: the bar counts its flash.bin whole, the app area tail (0x93000..0x9CFFF, smkx.c) included */
  const smk = join(HERE, "../build/melodee-smk37.fwsc");
  if (existsSync(smk)) {
    const [fl, res] = py(`import struct, sys; sys.path.insert(0, "tools")
import fm1_install as I
img = I.deinterleave(open(sys.argv[1], "rb").read())[0]
n = struct.unpack_from("<H", I._dec(img[:0x40]), 8)[0]
ents = {struct.unpack_from("<H", e)[0]: struct.unpack_from("<I", e, 12)[0] for e in (I._dec(img[0x40 + 0x50 * k:0x90 + 0x50 * k]) for k in range(n))}
print(ents[0], ents.get(101, 0), end="")`, smk).toString().split(" ").map(Number);
    const m = progressModel(logicalImage(readFileSync(smk)));
    ok(fl === 0x9D000 && m.write === 0x420 + fl + (fl - 0x4000) + (res ? res + 0x200 : 0),
      "progress.js: an SMK-37 package's bar counts flash.bin to 0x9D000 (its app area tail too)");
  }
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* a handshake answer as the firmware sends it (00 59 11, 27 bytes, checksum): the text, zeros and, from fm1-base
   firmware, the flash's JEDEC ID in bytes 24..26 */
function identFrame(text, flash = 0) {
  const body = [...new TextEncoder().encode(text), ...new Array(24).fill(0)].slice(0, 24);
  body.push((flash >> 16) & 0xFF, (flash >> 8) & 0xFF, flash & 0xFF);
  return [0xF0, ...pack7([0, 0x59, 0x11, body.length, 0, 0, ...body, ~body.reduce((a, b) => a + b, 0) & 0xFF]), 0xF7];
}

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image. base: firmware built on fm1-base
   (HS_QUERY: name, '_' and three digits; HS_FULL: the whole identity, not from the loader; both with the flash ID;
   flash 0: classic Melodee from 1.7.0, the same two answers without it), otherwise HS_FULL goes unanswered */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity, identity = "FM-1_015", afterWrite = "FM-1_900", loader = MELODEE_LOADER, base = false, flash = 0x856014 } = {}) {
    this.afterWrite = afterWrite; this.loader = loader;     // the loader step 1 starts (the package's own)
    this.base = base; this.flash = flash; this.fullAsked = 0;
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, identity === "FM-1_015" ? "FM-1" : "Melodee");
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    const ota = this.identity.startsWith("ota-");
    if (eq(d, HS)) {
      const i = this.identity.indexOf("_");
      const t = this.base && !ota && i >= 0 && i + 4 < this.identity.length ? this.identity.slice(0, i + 4) : this.identity;
      this.tx(identFrame(t, this.base ? this.flash : 0));
    } else if (eq(d, HS_FULL)) {
      this.fullAsked++;
      if (this.base && !ota) this.tx(identFrame(this.identity, this.flash));
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot(this.loader, this.loader === STOCK_LOADER ? "FM-1" : "Melodee Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.afterWrite, this.afterWrite === "FM-1_015" ? "FM-1" : "Melodee"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

async function updater() {
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, "FM-1_900", (k) => steps.push(k), MELODEE_LOADER);
  ok(got === "FM-1_900" && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: install: running firmware -> loader -> Melodee (${dev.served} reads)`);

  for (const identity of ["FM-1_900", "FM-1_91040", "FM-1_91041"]) {
    const stock = new FakeFM1(image, {identity, afterWrite:"FM-1_015", loader:STOCK_LOADER});
    const restored = await new Updater(stock.access).install(image, "FM-1_015", null, STOCK_LOADER);
    ok(restored === "FM-1_015" && stock.bad === 0,
      `fm1ota.js: ${identity} returns to stock V15 through the official loader`);
  }
  // each package goes only to its own loader: the official one would write a Melodee package's head
  // over the boot data, Melodee's would write only V15's application
  const pairs = [["FM-1_015", STOCK_LOADER, MELODEE_LOADER], ["FM-1_900", MELODEE_LOADER, STOCK_LOADER]];
  const refused = await Promise.all(pairs.map(async ([product, own, other]) => {   // ~5 s each
    const wrong = new FakeFM1(image, {identity:"FM-1_91041", afterWrite:product, loader:other});
    const err = await new Updater(wrong.access).install(image, product, null, own).then(() => null, (x) => x);
    return err && err.code === "wrongloader" && err.detail === other && wrong.served === 4 && wrong.identity === other;
  }));
  for (const [i, [product, own, other]] of pairs.entries()) {
    ok(refused[i], `fm1ota.js: ${product} is not written when step 1 starts ${other}`);
    const waiting = new FakeFM1(image, {afterWrite:product});
    waiting.boot(other, "Melodee Update");
    const resumedWrong = await new Updater(waiting.access).resume(image, null, product, own);
    ok(resumedWrong === false && waiting.served === 0 && waiting.identity === other,
      `fm1ota.js: ${product} never resumes in ${other}`);
  }
  const unnamed = new FakeFM1(image);
  unnamed.boot(MELODEE_LOADER, "Melodee Update");
  ok(await new Updater(unnamed.access).resume(image, null, "FM-1_900") === false && unnamed.served === 0,
    "fm1ota.js: resume without the package's loader identity writes nothing");
  const recovery = new FakeFM1(image, {afterWrite:"FM-1_015"});
  recovery.boot(STOCK_LOADER, "FM-1");
  const resumed = await new Updater(recovery.access).resume(image, null, "FM-1_015", STOCK_LOADER);
  ok(resumed && recovery.identity === "FM-1_015" && recovery.bad === 0,
    "fm1ota.js: an interrupted return to stock resumes in the official loader");

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot(MELODEE_LOADER, "Melodee Update");
  const t0 = Date.now();
  const done = await new Updater(dev2.access).resume(image, null, null, MELODEE_LOADER);
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write -> stops at once");

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, "FM-1_900", null, MELODEE_LOADER).then(() => null, (x) => x);
  ok(e && e.code === "lost" && e.lastRead === 0x40, "fm1ota.js: unplugged in step 1 -> error code 'lost', last read address");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, "FM-1_900", null, MELODEE_LOADER).then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");

  /* firmware built on fm1-base: the flash ID after the identity's zeros, HS_FULL for the whole identity */
  const reply = "f000324558010000234d5a447915070c180000000000000000000000000000000000001404462207f7";
  ok(Buffer.from(identFrame("FM-1_900", 0x856014)).toString("hex") === reply,
    "fm1ota.js test: the simulated fm1-base FM-1 answers its 2026-11-06 reply bytes");
  const onBase = new FakeFM1(image, { identity: "FM-1_900", base: true });
  const gotBase = await new Updater(onBase.access).install(image, "FM-1_900", null, MELODEE_LOADER);
  ok(gotBase === "FM-1_900" && onBase.bad === 0, "fm1ota.js: install over an fm1-base FM-1_900 (flash ID in the reply)");
  const rel = new FakeFM1(image, { identity: "FM-1_900", base: true, afterWrite: "FM-1_9001613" });
  const gotRel = await new Updater(rel.access).install(image, "FM-1_9001613", null, MELODEE_LOADER);
  ok(gotRel === "FM-1_9001613" && rel.fullAsked >= 2, "fm1ota.js: an fm1-base release's whole identity through HS_FULL");
  /* classic Melodee from 1.7.0 (firmware/src/ota.c ota_handshake): FM-1_910 to HS_QUERY, the whole FM-1_91070 to HS_FULL */
  const rel17 = new FakeFM1(image, { identity: "FM-1_91070", base: true, flash: 0, afterWrite: "FM-1_91070" });
  const found17 = await new Updater(rel17.access).find();
  found17.link.close();
  ok(found17.id.text === "FM-1_91070" && rel17.fullAsked === 1, "fm1ota.js: Melodee 1.7.0 found as FM-1_91070 behind its plain FM-1_910");
  const got17 = await new Updater(rel17.access).install(image, "FM-1_91070", null, MELODEE_LOADER);
  ok(got17 === "FM-1_91070" && rel17.bad === 0, "fm1ota.js: 1.7.0 over itself, the check after the restart sees FM-1_91070");
  const asked = [];
  for (const identity of ["FM-1_900", "FM-1_015", "SMK-37 Pro_016"]) {
    const d = new FakeFM1(image, { identity });
    const found = await new Updater(d.access).find();
    found.link.close();
    asked.push(found.id.text === identity && d.fullAsked === (identity === "FM-1_900" ? 1 : 0));
  }
  ok(asked.every(Boolean), "fm1ota.js: classic FM-1_900 keeps its plain answer; stock FM-1_015 / SMK-37 Pro_016 never get HS_FULL");

  /* the SMK-37 models (SMK-37 Pro, SMK-37 Elite, MKE-P37): a package installs only on exactly its model */
  for (const [dev, pkg] of [["SMK-37 Pro_016", "SMK-37 Elite_900"], ["SMK-37 Pro_016", "FM-1_900"], ["FM-1_015", "SMK-37 Pro_900"],
                            ["MKE-P37_016", "SMK-37 Pro_900"]]) {
    const d = new FakeFM1(image, { identity: dev });
    const m = await new Updater(d.access).install(image, pkg, null, melodeeLoader(pkg)).then(() => null, (x) => x);
    ok(m && m.code === "model" && d.served === 0, `fm1ota.js: ${pkg} on ${dev} -> 'model', nothing sent`);
  }
  const elite = new FakeFM1(image, { afterWrite: "SMK-37 Elite_900" });
  elite.boot("ota-SMK-37 Elite_900", "Melodee Update");
  const r1 = await new Updater(elite.access).resume(image, null, "SMK-37 Pro_900", melodeeLoader("SMK-37 Pro_900"));
  ok(r1 === false && elite.served === 0, "fm1ota.js: an SMK-37 Elite in update mode is never given an SMK-37 Pro package");
  const r2 = await new Updater(elite.access).resume(image, null, "SMK-37 Elite_900", melodeeLoader("SMK-37 Elite_900"));
  ok(r2 === true && elite.served > 0 && elite.bad === 0 && elite.identity === "SMK-37 Elite_900",
    "fm1ota.js: ... its own model's package resumes and finishes");
  ok(melodeeLoader("FM-1_906") === MELODEE_LOADER && melodeeLoader("MKE-P37_900") === "ota-MKE-P37_900",
    "fm1ota.js: a package's own loader under its model (the FM-1's as before)");
}

async function patternProtocol() {
  const base = [65,0,14,92,27,64,84,...Array(14).fill(0),4];
  const info = E.parse[E.CMD.INFO]([...base,0,0x55,1,9,0x4d,1,64,1,0x42,1,3,0x50,1,8,16,0x46,1,8,32]);
  const nativeInfo = E.parse[E.CMD.INFO]([...base,0,0x55,1,9,0x4d,2,64,1,0x42,1,3,0x50,1,8,16,0x46,1,8,32]);
  ok(nativeInfo.motionVersion === 2 && nativeInfo.motionMax === 64 && nativeInfo.patterns === 8,
    "native motion: INFO v2 retains the tagged capabilities, v1 firmware still supported");
  ok(info.patterns === 8 && info.chainRows === 16 && info.backupCaps === 3,"patterns: tagged INFO preserves preferences, motion and backup");
  ok(eq(E.req.pattern(2,1,7)[1],[2,1,7]) && E.parse[E.CMD.PATTERN]([2,1,0,7,127,8]).active===7,"patterns: bank selection uses its track and zero-based bank");
  const rows=[{banks:[0,2,4,7],repeat:3},{banks:[7,6,5,4],repeat:1}];
  const request=E.req.bankSong(1,rows);
  const reply=E.parse[E.CMD.BANK_SONG]([1,0,2,0,0,0,...request[1].slice(2)]);
  ok(JSON.stringify(reply.rows)===JSON.stringify(rows),"patterns: SONG carries independent banks for every track");
  let invalid=false; try { E.req.bankSong(1,[{banks:[0,1,2,10],repeat:1}]); } catch { invalid=true; }
  ok(invalid,"patterns: invalid bank-song values rejected before transport");
  const extended = E.parse[E.CMD.INFO]([...base,0,0x55,1,9,0x4d,2,64,1,0x42,1,3,0x50,2,8,64]);
  ok(extended.chainRows === 64 && extended.songRests, "SONG v2 advertises 64 sections and explicit rests");
  const longRows = Array.from({length:64}, (_,i)=>({banks:[i%9,8,0,7],repeat:i%16+1}));
  const longRequest = E.req.bankSong(1,longRows);
  const longReply = E.parse[E.CMD.BANK_SONG]([1,0,64,0,0,0,...longRequest[1].slice(2)]);
  ok(JSON.stringify(longReply.rows) === JSON.stringify(longRows), "SONG v2 round-trips all 64 sections and resting cells");
  ok(eq(E.req.bankSong(2,[],63)[1],[2,63]), "SONG v2 start carries the selected section");
  const blocks = E.parse[E.CMD.INFO]([...base,0,0x55,1,9,0x4d,2,64,1,0x42,1,3,0x50,3,8,64]);
  ok(blocks.songBlocks && blocks.songRests, "SONG v3 advertises block continuations");
  const blockRows = [{banks:[0,1,8,8],repeat:1},{banks:[0,9,8,8],repeat:1}];
  const blockRequest = E.req.bankSong(1,blockRows);
  ok(JSON.stringify(E.parse[E.CMD.BANK_SONG]([1,0,2,0,0,0,...blockRequest[1].slice(2)]).rows)===JSON.stringify(blockRows), "SONG v3 preserves a block across section boundaries");
  let orphan=false; try { E.req.bankSong(1,[{banks:[9,1,8,8],repeat:1}]); } catch { orphan=true; }
  ok(orphan, "SONG v3 rejects a continuation without its source block");
  const loopInfo=E.parse[E.CMD.INFO]([...base,0,0x55,1,9,0x4d,2,64,1,0x42,1,3,0x50,4,8,64]);
  ok(loopInfo.songLoop && loopInfo.songBlocks, "SONG v4 advertises the loop setting");
  ok(eq(E.req.bankSong(4,[],1)[1],[4,1]) && E.parse[E.CMD.BANK_SONG]([4,0,0,0,0,0,1]).loop, "SONG v4 loop request and reply preserve the setting");
  const loopMock=E.makeMockDevice({auto:false,advancedSong:true,songLoop:true});
  const loopLink=new E.Link(d=>[...loopMock.access.outputs.values()][0].send(d),{timeout:100});
  [...loopMock.access.inputs.values()][0].onmidimessage=e=>loopLink.receive(e.data);
  const loopReply=E.parse[E.CMD.BANK_SONG](await loopLink.request(E.req.bankSong(4,[],1)));
  const keepLoop=E.parse[E.CMD.BANK_SONG](await loopLink.request(E.req.bankSong(1,blockRows)));
  ok(loopReply.loop && keepLoop.loop,"SONG v4 retains loop mode when editing the arrangement");
  loopLink.close();loopMock.stop();
  let tooLong=false; try { E.req.bankSong(1,[...longRows,longRows[0]]); } catch { tooLong=true; }
  ok(tooLong, "SONG v2 rejects more than 64 sections");
  const B=await import("./fm1backup.js");
  const ids=[0,1,2,3,4,5,6,7,8], manifest=[1,0,9];
  ids.forEach(id=>manifest.push(id,...B.bkU32(id===0?20224:id===1?604:0),...B.bkU32(0)));
  ok(B.bkManifest(manifest)[0].size===20224,"patterns: nine-object archives accept complete 32-bank projects");
}
async function stockRestore() {
  const installer = readFileSync(join(HERE, "index_pkg.html"), "utf8");
  const script = strip(readFileSync(join(HERE,"install/leave.js"),"utf8")) + `
setupLeaving({getLanguage:()=>"en",isBusy:()=>writing,setBusy:on=>{writing=on;},log:()=>{}});`;
  const HEAD = {from:1024, to:1024 + 0x4000};
  // loader: the identity of the update loader the FM-1 is waiting in (false: running firmware);
  // button: "stock-go" (Return to official V15) or "go" (Install)
  async function run({skip = false, loader = false, recovery = false, supported = false, confirm = true, backupError = false, motionVersion = 2, backupVersion = 1, backupCaps = 1, installError = null, button = "stock-go", stockProduct = "FM-1_015", device = "FM-1"} = {}) {
    const calls = [], writes = [], elements = new Map();
    const element = (id) => {
      if (!elements.has(id)) elements.set(id, {checked:false, disabled:false, textContent:"", value:0, listeners:{}, options:[],
        replaceChildren(...options) { this.options = options; this.value = options.find(o => o.selected)?.value || options[0]?.value || ""; },
        addEventListener(k, fn) { this.listeners[k] = fn; }});
      return elements.get(id);
    };
    const info = [...Buffer.from("MELODEE v1.4.0"),0,0,92,0,64,84,4,
      ...(supported ? [16,0x55,1,0,0x4d,motionVersion,64,1,0x42,backupVersion,backupCaps] : [])];
    const context = vm.createContext({
      writing:false,
      navigator:{language:"en", requestMIDIAccess:async () => ({})},
      document:{documentElement:{}, querySelectorAll:() => [], getElementById:element,
        body:{append() {}}, createElement:() => ({click() { calls.push("save"); }, remove() {}})},
      window:{addEventListener() {}},
      fetch:async (url) => { calls.push("fetch " + url); return {ok:true, arrayBuffer:async () => Uint8Array.of(2).buffer}; },
      STOCK_V15_SIZE:1, STOCK_SMK016_SIZE:705252, MELODEE_LOADER, STOCK_LOADER, melodeeLoader,
      validateStockPackage:async (bytes) => {
        if (bytes.length !== 1 || bytes[0] !== 1) throw Error("Not the selected file");
        return {product:stockProduct, image:bytes, head:HEAD};
      },
      productOf:() => "FM-1_9010", logicalImage:bytes => bytes,
      URL:{createObjectURL:() => "blob:test", revokeObjectURL() {}}, Blob, setTimeout:() => 0,
      confirm:(message) => { calls.push("confirm"); calls.push(message); return confirm; },
      Updater:class {
        async find(filter) {
          const id = loader ? {model:loader.split("_")[0], text:loader} : {model:device, text:device + "_9010"};
          return filter(id) ? {id, link:{input:{},output:{},close() {}}} : null;
        }
        async install(image, product, step, own) {
          calls.push("install"); writes.push([image[0], product, own]);
          if (installError) throw installError;
        }
        async resume(image, step, product, own) {
          calls.push("resume"); writes.push([image[0], product, own]); return true;
        }
      },
      BackupConnection:class {
        constructor() { calls.push("backup-open"); }
        async request() { calls.push("info"); return info; }
        close() { calls.push("backup-close"); }
      },
      captureBackup:async () => { calls.push("backup"); if (backupError) throw new Error("Backup failed"); return {}; },
    });
    mockInstallSession(context);
  vm.runInContext(script, context);
    await new Promise(resolve => setImmediate(resolve));
    element("stock-file").files = [{size:1,arrayBuffer:async () => Uint8Array.of(1).buffer}];
    await element("stock-file").listeners.change();
    element("stock-skip-backup").checked = skip;
    element("stock-recovery").checked = recovery;
    element("stock-skip-backup").listeners.change();
    const label = element("stock-go").textContent;
    await element(button).listeners.click();
    return {calls, writes, label, status:element("status").textContent, unlocked:!element("stock-skip-backup").disabled};
  }
  const official = [1, "FM-1_015", STOCK_LOADER], melodee = [2, "FM-1_9010", MELODEE_LOADER];
  const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  const fail = (code, more = {}) => Object.assign(new Error(code), {code}, more);
  let r = await run();
  ok(!r.calls.includes("install") && r.status.includes("select Skip backup") && r.calls.includes("backup-close") && r.unlocked,
    "stock: unsupported backups block flashing by default, explain opt-out");
  r = await run({skip:true});
  ok(r.calls.includes("install") && !r.calls.includes("info") && !r.calls.includes("backup-open")
    && r.calls.some(c => c.includes("without creating a backup")) && r.label.includes("without backup") && r.unlocked,
    "stock: explicit skip bypasses backup protocol and confirms data loss");
  ok(same(r.writes, [official]) && !r.calls.some(c => c.startsWith("fetch"))
    && !/buildStockRestorePackage|application-only/.test(installer),
    "stock: the unmodified official file goes only to the official loader, no Melodee loader is fetched");
  r = await run({skip:true, confirm:false});
  ok(!r.calls.includes("install") && !r.calls.includes("resume") && r.unlocked,
    "stock: cancelling the no-backup confirmation never flashes");
  r = await run({supported:true});
  ok(r.calls.indexOf("backup") < r.calls.indexOf("save") && r.calls.indexOf("save") < r.calls.indexOf("confirm")
    && r.calls.indexOf("confirm") < r.calls.indexOf("install") && r.label.startsWith("Back up"),
    "stock: motion v2 firmware saves the backup before confirmation and flashing");
  r = await run({supported:true, motionVersion:1});
  ok(r.calls.includes("backup") && r.calls.includes("install"),
    "stock: older motion v1 firmware retains backup and restore support");
  for (const capabilities of [{backupVersion:2}, {backupCaps:0}]) {
    r = await run({supported:true, ...capabilities});
    ok(!r.calls.includes("backup") && !r.calls.includes("install") && r.status.includes("Skip backup"),
      "stock: unsupported backup protocol or missing read capability still blocks flashing");
  }
  r = await run({supported:true, backupError:true});
  ok(!r.calls.includes("install") && !r.calls.includes("confirm") && r.calls.includes("backup")
    && r.status.includes("Backup failed") && r.calls.includes("backup-close") && r.unlocked,
    "stock: a failed backup never silently falls through to flashing");
  r = await run({skip:true, installError:fail("stopped", {lastRead:HEAD.from + 0x200})});
  ok(r.status.includes("CHECK HEAD") && r.status.includes("FAILED -5") && r.status.includes("report") && r.unlocked
    && /RECOVERY\.md#return-to-stock-from-running-melodee"[^>]*data-leave-t="stockHelp"/.test(installer),
    "stock: a stop at CHECK HEAD is refused with the FAILED -5 notes, no other path");
  r = await run({skip:true, installError:fail("stopped", {lastRead:0x40})});
  ok(r.status.startsWith("Stopped.") && !r.status.includes("FAILED -5"), "stock: other step-1 stops keep the generic message");
  r = await run({skip:true, installError:fail("wrongloader", {detail:MELODEE_LOADER})});
  ok(r.status.includes("Melodee's update mode"), "stock: a Melodee loader started by step 1 gets nothing");
  r = await run({loader:STOCK_LOADER});
  ok(!r.calls.includes("resume") && r.status.includes("Skip backup"),
    "stock: interrupted updates still require saved backup or explicit skip");
  r = await run({loader:STOCK_LOADER, skip:true});
  ok(r.calls.includes("resume") && r.calls.some(c => c.includes("without creating a backup")) && same(r.writes, [official]),
    "stock: explicit skip can resume the official loader after data-loss confirmation");
  r = await run({loader:STOCK_LOADER, recovery:true});
  ok(r.calls.includes("resume") && r.calls.some(c => c.includes("Has the complete backup")),
    "stock: saved-backup recovery retains its existing confirmation");
  for (const [other, says] of [[MELODEE_LOADER, "Melodee's update mode"], ["ota-FM-1_123", "ota-FM-1_123"]]) {
    r = await run({loader:other, skip:true, recovery:true});
    ok(!r.writes.length && !r.calls.includes("confirm") && r.status.includes(says) && r.unlocked,
      `stock: never resumes in ${other}`);
  }
  /* the SMK-37 Pro: its official V016 goes to the running SMK-37 Pro, through its own official loader only */
  const smk = {stockProduct:"SMK-37 Pro_016", device:"SMK-37 Pro"}, smkOfficial = [1, "SMK-37 Pro_016", "ota-SMK-37 Pro_016"];
  r = await run({...smk, skip:true});
  ok(same(r.writes, [smkOfficial]) && r.calls.includes("install"),
    "stock (SMK-37 Pro): the official V016 goes to the running SMK-37 Pro, through its official loader only");
  r = await run({...smk, skip:true, loader:"ota-SMK-37 Pro_016"});
  ok(same(r.writes, [smkOfficial]) && r.calls.includes("resume"), "stock (SMK-37 Pro): resumes in its official loader");
  r = await run({...smk, skip:true, installError:fail("stopped", {lastRead:HEAD.from + 0x200})});
  ok(r.status.includes("CHECK HEAD"), "stock (SMK-37 Pro): a stop at CHECK HEAD is refused as on the FM-1");
  for (const other of [STOCK_LOADER, "ota-SMK-37 Elite_016", "ota-SMK-37 Pro_900"]) {
    r = await run({...smk, skip:true, recovery:true, loader:other});
    ok(!r.writes.length, `stock (SMK-37 Pro): never resumes in ${other}`);
  }
  r = await run({skip:true, device:"SMK-37 Pro"});
  ok(!r.writes.length, "stock: the FM-1's V15 never goes to an SMK-37");
  r = await run({stockProduct:"SMK-37 Pro_016", skip:true});
  ok(!r.writes.length, "stock: the SMK-37 Pro's V016 never goes to an FM-1");
}
// Installer channel/race and real dialog tests live in test_installer.mjs and test_install_safety.mjs.
await stockRestore();
await patternProtocol();
await editorMock();
await editorMidiTrack();
await editorSamplePresets();
mockTables();
await editorLibrarian();
await trackEffects();
await editorGrid();
await editorLive();
await preferenceReplies();
await editorTracks();
await editorMixer();
await editorTrackParam();
await editorSong();
await editorSessions();
await editorModules();
await synthTests();
editorTabs();
editorIcons();
samplesMatch();
await packages();
await updater();
console.log(failed ? `WEB TESTS FAILED (${failed})` : "web tests passed");
process.exit(failed ? 1 : 0);
