// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// The installer page's update from Melodee 1.x to 2.0, the published page (make_site.py's libraries) against a simulated
// FM-1 (install/fakefm1.js) whose backup commands go to the firmware's own code: Melodee 1.x's virtual FM-1 before the
// write, 2.0's after (web/convert/test_cores.mjs). The build has a converter (<package>-convert.bin):
//   convert   1.x records on it: backed up into the downloads before anything is written, converted, the firmware
//             written, the records written back; the 2.0 FM-1 then holds them and nothing of 1.x's
//   current   already 2.0's records: nothing backed up or written back (that phase hidden)
//   broken    a damaged converter: stopped before the firmware (safe), nothing written
//   refused   the 2.0 FM-1 refuses the write back: the firmware runs, the end screen says so, the converted copy saved
//   web/convert/build.sh, web/core/build.sh, ./build.sh (build/melodee.fwsc); node web/test_install_convert.mjs
import assert from 'node:assert/strict';
import {readFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {createHash, webcrypto} from 'node:crypto';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {JSDOM} from 'jsdom';
import {productOf, logicalImage} from './fm1pkg.js';
import {MELODEE_LOADER} from './fm1ota.js';
import {fakeAccess} from './install/fakefm1.js';
import {BACKUP_CMD, bkManifest} from './fm1backup.js';
import {legacyIds, readBackupV1} from './convert/convert.mjs';
import {controllerSource} from './installer_test_helpers.mjs';
import {legacyFM1, fm1v2, restoreInto, fixtureBackup} from './convert/test_cores.mjs';
import {prepareConversion, writeConverted} from './install/conversion.js';

const nativeSet = globalThis.setTimeout;
const clock = {set:(f,ms) => nativeSet(f,ms >= 1000 ? ms / 20 : ms),clear:globalThis.clearTimeout};
const html = readFileSync(new URL('index_pkg.html',import.meta.url),'utf8');
const libraries = execFileSync('python3',['-c',"import sys;sys.path.insert(0,'web');from make_site import installer_libraries;print(installer_libraries())"],{encoding:'utf8'});
const firmware = new Uint8Array(readFileSync(new URL('../build/melodee.fwsc',import.meta.url)));
const digest = createHash('sha256').update(firmware).digest('hex');
const bundle = new Uint8Array(readFileSync(new URL('../build/melodee-convert.bin',import.meta.url)));
const tmp = mkdtempSync(join(tmpdir(),'melodee-install-convert-'));
const backup = fixtureBackup(tmp);

async function run(scenario) {
  const before = await legacyFM1(), after = await fm1v2();
  assert.deepEqual(await restoreInto(before.request,backup),[]);
  if (scenario === 'current') {                       /* (an FM-1 on 2.0 already: the backup converted into it first) */
    const conv = await prepareConversion(before.request,'FM-1_9010601',async () => bundle);
    await writeConverted(after.request,conv.after);
  }
  let puts = 0;
  const backupCmd = (cmd,args,installed) => {
    if (scenario === 'current') return after.reply(cmd,args);
    if (!installed) return before.reply(cmd,args);
    if (cmd === BACKUP_CMD.PUT && args[0] !== 3) puts++;
    if (scenario === 'refused' && cmd === BACKUP_CMD.PUT && args[0] === 2) return [2,args[1],4];   /* (flash write failed) */
    return after.reply(cmd,args);
  };
  const page = new JSDOM(html.replace(/<script type="module">[\s\S]*?<\/script>/,''),{url:'https://example.test/webapp/installer/',runScripts:'outside-only'});
  const win = page.window, doc = win.document, $ = id => doc.getElementById(id);
  const simulated = fakeAccess({images:{firmware:logicalImage(firmware)},identity:'FM-1_91061',after:productOf(firmware),
                                loader:MELODEE_LOADER,stride:128,timers:clock,backup:backupCmd});
  const pageSet = win.setTimeout.bind(win);win.setTimeout = (f,ms = 0,...a) => pageSet(f,ms >= 1000 ? ms / 20 : ms,...a);
  win.HTMLDialogElement.prototype.showModal = function(){this.setAttribute('open','');};
  win.HTMLDialogElement.prototype.close = function(){this.removeAttribute('open');};
  win.TextEncoder = TextEncoder;win.TextDecoder = TextDecoder;
  Object.defineProperty(win.crypto,'subtle',{value:webcrypto.subtle});
  const downloads = [];                               /* (the page's blobs; a download: one an anchor saved, named) */
  win.URL.createObjectURL = blob => { const u = `blob:${downloads.length}`;downloads.push({url:u,blob});return u; };
  win.URL.revokeObjectURL = () => {};
  win.HTMLAnchorElement.prototype.click = function(){ const d = downloads.find(x => x.url === this.href);if (d) d.name = this.download; };
  const fetched = [];
  win.fetch = async (url,opt = {}) => {
    fetched.push(`${opt.method || 'GET'} ${url}`);
    if (/-(res|mods)\.bin/.test(url)) return {ok:false,status:404};
    if (/-convert\.bin/.test(url)) {
      const b = scenario === 'broken' ? bundle.slice(0,100) : bundle;
      return {ok:true,status:200,arrayBuffer:async () => b.slice().buffer};
    }
    return {ok:true,status:200,arrayBuffer:async () => firmware.slice().buffer};
  };
  win.navigator.requestMIDIAccess = async () => simulated.access;
  const metadata = {version:'2.0.0',product:productOf(firmware),pkg:{dual:'test.fwsc'},sha256:{dual:digest}};
  const script = html.match(/<script type="module">([\s\S]*?)<\/script>/)[1].replace('/*LIB*/',() => libraries)
    .replace('/*META*/{}',JSON.stringify(metadata)).replace('/*INSTALLER*/',() => `(() => {${controllerSource}})();`);
  win.eval(script);await new Promise(r => nativeSet(r,30));
  $('find').click();
  for (const until = Date.now() + 3000;$('device').hidden && Date.now() < until;) await new Promise(r => nativeSet(r,10));
  assert(!$('device').hidden && !$('go').disabled,scenario);
  assert.match($('plantext').textContent,/new form/,'the plan says the records go along');
  $('go').click();await new Promise(r => nativeSet(r,10));
  $('pfok').checked = true;$('pfok').dispatchEvent(new win.Event('change'));
  $('preflight').querySelector('form').dispatchEvent(new win.SubmitEvent('submit',{cancelable:true,submitter:$('pfgo')}));
  for (const until = Date.now() + 30000;$('run').dataset.state === 'running' && Date.now() < until;) await new Promise(r => nativeSet(r,10));
  const state = $('run').dataset.state, log = $('log').textContent, tips = $('endtips').textContent;
  const written = simulated.device.log.includes('written: ' + productOf(firmware));
  const files = await Promise.all(downloads.filter(d => d.name).map(async d => ({name:d.name,file:JSON.parse(await d.blob.text())})));
  const v2 = bkManifest(after.reply(BACKUP_CMD.LIST,[]));
  page.window.close();
  return {state,log,tips,written,files,puts,fetched,v2,after};
}

try {
  {
    const r = await run('convert');
    assert.equal(r.state,'done',r.log);
    assert(r.written,'the firmware was written');
    assert.equal(r.files.length,1,'one download: the 1.x backup');
    assert.match(r.files[0].name,/^Melodee-1\.x-backup-\d{8}-\d{4}\.json$/);
    const saved = readBackupV1(r.files[0].file);
    assert.deepEqual([...saved.keys()].filter(id => backup.has(id) && id !== 0).sort(),[...backup.keys()].filter(id => id !== 0).sort());
    for (const id of [2,3,64,92,93]) assert.deepEqual(Array.from(saved.get(id)),Array.from(backup.get(id)),`backup object ${id} as the FM-1 held it`);
    assert(r.puts > 0);
    assert.match(r.tips,/converted for Melodee 2\.0\.0 and written back/);
    assert.match(r.tips,/Melodee-1\.x-backup-/);
    assert.deepEqual(r.v2.filter(o => o.size && [2,3,64,92,93].includes(o.id)).map(o => o.id),[2,3,64,92,93],'the 2.0 FM-1 holds the projects and kits');
    assert.equal(await prepareConversion(r.after.request,'FM-1_900',async () => bundle),null,'and nothing of 1.x\'s');
    assert(r.fetched.some(f => f.startsWith('HEAD') && f.includes('-convert.bin')) && r.fetched.some(f => f.startsWith('GET') && f.includes('-convert.bin')));
    console.log('Installer 1.x -> 2.0: backed up into the downloads before the write, converted, firmware written, records written back');
  }
  {
    const r = await run('current');
    assert.equal(r.state,'done',r.log);
    assert.equal(r.files.length,0,'nothing downloaded');
    assert.equal(r.puts,0,'nothing written back');
    assert(!r.fetched.some(f => f.startsWith('GET') && f.includes('-convert.bin')),'the converter is never downloaded');
    assert.match(r.log,/nothing of Melodee 1\.x's/);
    console.log('Installer on an FM-1 with 2.0 records: a look at them, nothing backed up or converted');
  }
  {
    const r = await run('broken');
    assert.equal(r.state,'safe',r.log);
    assert(!r.written,'the firmware was not written');
    assert.equal(r.puts,0);
    assert.match(r.tips,/couldn't be converted, so nothing was written/);
    console.log('Installer with a damaged converter: stops before the firmware, nothing written');
  }
  {
    const r = await run('refused');
    assert.equal(r.state,'sounds',r.log);
    assert(r.written);
    assert.equal(r.files.length,2,'the 1.x backup and the converted copy');
    assert.match(r.files[1].name,/^Melodee-2\.0\.0-converted-\d{8}-\d{4}\.json$/);
    assert.deepEqual(legacyIds(readBackupV1(r.files[1].file)),[],'the converted copy holds 2.0 records only');
    assert.match(r.tips,/Flash write failed/);
    console.log('Installer when the write back fails: the firmware runs, the converted copy saved for the editor');
  }
} finally {
  rmSync(tmp,{recursive:true,force:true});
}
