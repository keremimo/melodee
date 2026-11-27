// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
import assert from 'node:assert/strict';
import {pageDOM,controllerSource,template} from './installer_test_helpers.mjs';
import {describeDevice,planFor,progressModel,remaining,variantFor,modelOf,MODELS} from './install/progress.js';
const page=pageDOM(),w=page.window,d=w.document,$=id=>d.getElementById(id);
const releases=[{version:'1.3.2',product:'new',pkg:{dual:'new-dual'},models:{'SMK-37 Elite':{product:'SMK-37 Elite_x',pkg:{dual:'elite-dual'}}}},{version:'1.2.1',product:'old',pkg:{dual:'old-dual'}}];
const meta={...releases[0],releases,rc:{version:'1.4.0-rc.1',product:'rc',pkg:{dual:'rc-dual'},models:{'SMK-37 Pro':{product:'SMK-37 Pro_rc',pkg:{dual:'rc-pro'}}}}};
const pending=[];
Object.assign(w,{
 meta,describeDevice,planFor,progressModel,remaining,variantFor,modelOf,MODELS,logicalImage:b=>b,productOf:b=>['new','old','rc','SMK-37 Elite_x','SMK-37 Pro_rc'][b[0]],
 fetch:url=>/-(res|mods|convert)\.bin/.test(url)?Promise.resolve({ok:false,status:404}):new Promise(resolve=>pending.push({url,resolve})),
 setupLeaving:()=>({lock(){},applyLang(){}}),hold:async()=>()=>{},busyElsewhere:async()=>false,
});
w.eval(controllerSource+'\nwindow.controller={st,render,log,loadRelease,resRegion,modBundleFile,useModel,selection,connect};');
const tick=()=>new Promise(r=>setImmediate(r));
async function finish(req,product,marker){req.resolve({ok:true,arrayBuffer:async()=>Uint8Array.of(product,marker).buffer});await tick();}
function change(id){$(id).dispatchEvent(new w.Event('change'));}
try{
 assert.equal($('release').options.length,2);
 assert.equal($('advanced-installs').open,false);
 assert.equal($('advanced-installs').hidden,false);
 assert.equal($('release').value,'1.3.2');
 assert.equal(pending[0].url,'new-dual');await finish(pending.shift(),0,10);
 assert.equal(w.controller.st.image[1],10);
 $('release').value='1.2.1';change('release');assert($('go').disabled);
 assert.equal(pending[0].url,'old-dual');await finish(pending.shift(),1,20);
 assert.equal(w.controller.st.image[1],20);assert.equal($('hver').textContent,'1.2.1');
 $('lang').click();assert.equal($('release').value,'1.2.1');$('lang').click();
 $('build-rc-dual').checked=true;change('build-rc-dual');const rc=pending.shift();
 $('release').value='1.3.2';change('release');await tick();assert.equal(w.controller.st.image[1],10);
 await finish(rc,2,30);assert.equal(w.controller.st.image[1],10);
 w.controller.st.running=true;w.controller.render();
 for(const id of ['release','build-rc-dual','mboot','go','find'])assert($(id).disabled,id);
 change('release');assert.equal(pending.length,0);
 assert.match(template,/id="mboot"/);assert.doesNotMatch(template,/build-(?:rc-|nightly-)?single/);
 // Saadet's actual styles and three-step interaction, including current firmware -> boot-only.
 // A nightly's resource region is fetched with its commit, as its package is (no stale CDN copy).
 const urls=[];w.fetch=url=>{urls.push(url);return Promise.resolve({ok:false,status:404});};
 w.controller.st.meta={version:'n',product:'p',commit:'abc',pkg:{dual:'../../firmware/melodee-nightly.fwsc'}};
 assert.equal(await w.controller.resRegion(),null);
 assert.equal(await w.controller.modBundleFile(),null);   /* (and its synth modules, beside it) */
 assert.deepEqual(urls,['../../firmware/melodee-nightly-res.bin?commit=abc','../../firmware/melodee-nightly-mods.bin?commit=abc']);
 assert.deepEqual(planFor('this',true),{firmware:false,resume:false,boot:true});
 assert.deepEqual(planFor('loader',true),{firmware:true,resume:true,boot:true});
 assert.equal(planFor('open',true),null);
 // the SMK-37 builds: a package goes only to exactly its model (SMK-37 Pro, SMK-37 Elite, MKE-P37); the FM-1's as before
 assert.equal(describeDevice({model:'SMK-37 Pro',text:'SMK-37 Pro_016'},'SMK-37 Pro_91061').kind,'stock');
 assert.equal(describeDevice({model:'SMK-37 Pro',text:'SMK-37 Pro_900'},'SMK-37 Pro_91061').kind,'dev');
 assert.deepEqual(describeDevice({model:'SMK-37 Pro',text:'SMK-37 Pro_91061'},'SMK-37 Pro_91061'),{kind:'this',version:'1.6.1'});
 assert.equal(describeDevice({model:'SMK-37 Pro',text:'SMK-37 Pro_91060'},'SMK-37 Pro_91061').kind,'melodee');
 assert.equal(describeDevice({model:'FM-1',text:'FM-1_91061'},'SMK-37 Pro_91061').kind,'model');
 assert.equal(describeDevice({model:'SMK-37 Elite',text:'SMK-37 Elite_016'},'SMK-37 Pro_91061').kind,'model');
 assert.equal(describeDevice({model:'SMK-37 Pro',text:'SMK-37 Pro_016'},'FM-1_91061').kind,'model');
 assert.equal(describeDevice({model:'FM-1',text:'FM-1_016'},'FM-1_91061').kind,'stock');
 assert.equal(describeDevice({model:'ota-SMK-37 Pro',text:'ota-SMK-37 Pro_900'},'SMK-37 Pro_91061').kind,'loader');
 // the SMK-37 family: a channel's package for the connected model (release.json "models"), the FM-1's otherwise
 const fm={version:'1.7.0',product:'FM-1_91070',pkg:{dual:'fm'},sha256:{dual:'a'},commit:'c',
           models:{'SMK-37 Pro':{product:'SMK-37 Pro_91070',pkg:{dual:'pro'},sha256:{dual:'b'}},'MKE-P37':{product:'SMK-37 Pro_91070',pkg:{dual:'x'}}}};
 assert.equal(variantFor(fm,'FM-1'),fm);assert.equal(variantFor(fm,''),fm);
 assert.deepEqual(variantFor(fm,'SMK-37 Pro'),{version:'1.7.0',product:'SMK-37 Pro_91070',pkg:{dual:'pro'},sha256:{dual:'b'},commit:'c',models:fm.models});
 assert.equal(variantFor(fm,'SMK-37 Elite'),null);          // no package for that model in this version
 assert.equal(variantFor(fm,'MKE-P37'),null);               // an entry under another model's identity is never offered
 assert.equal(variantFor(fm,'constructor'),null);assert.equal(variantFor({product:'FM-1_900',pkg:{dual:'n'}},'SMK-37 Pro'),null);
 assert.equal(modelOf({model:'ota-SMK-37 Pro'}),'SMK-37 Pro');assert.equal(modelOf({model:'FM-1'}),'FM-1');assert.equal(modelOf(null),'');
 assert.deepEqual(MODELS,['FM-1','SMK-37 Pro','SMK-37 Elite','MKE-P37']);
 // the page: an SMK-37 Elite found -> its package fetched and checked as its own; a version without one keeps the FM-1's
 pending.length=0;w.fetch=url=>/-(res|mods|convert)\.bin/.test(url)?Promise.resolve({ok:false,status:404}):new Promise(resolve=>pending.push({url,resolve}));
 w.controller.st.running=false;$('build-dual').checked=true;$('release').value='1.3.2';
 const sw=w.controller.useModel('SMK-37 Elite');await tick();
 assert.equal(pending[0].url,'elite-dual');await finish(pending.shift(),3,40);await sw;
 assert.equal(w.controller.st.meta.product,'SMK-37 Elite_x');assert.equal(w.controller.st.image[1],40);
 assert.equal(w.controller.st.boot,null);                   // m-bootloader is the FM-1's alone: never offered to another model
 assert.equal(w.controller.selection().pkg.dual,'elite-dual');
 $('release').value='1.2.1';change('release');await tick();   // 1.2.1 has no SMK-37 package: the FM-1's, which an Elite is refused
 assert.equal(w.controller.st.meta.product,'old');
 assert.equal(describeDevice({model:'SMK-37 Elite',text:'SMK-37 Elite_016'},'FM-1_91021').kind,'model');
 await w.controller.useModel('SMK-37 Pro');                 // no Pro package in any version here: the FM-1's again
 assert.equal(w.controller.selection().product,'old');
 $('release').value='1.3.2';change('release');await tick();   // back on the FM-1's own package (still cached: no fetch)
 assert.equal(pending.length,0);assert.equal(w.controller.st.meta.product,'new');assert.equal(w.controller.st.image[1],10);
 // Find, with an SMK-37 Elite on stock firmware answering: the page turns to the Elite's package, names the model, offers
 // Install with the beta note, and never m-bootloader; an FM-1 found next gets the FM-1's package back
 let found={model:'SMK-37 Elite',text:'SMK-37 Elite_016'};
 w.SteadyUpdater=class{async find(){return {id:found,link:{close(){}}};}};
 Object.defineProperty(w.navigator,'requestMIDIAccess',{configurable:true,value:async()=>({inputs:new Map(),outputs:new Map()})});
 await w.controller.connect();
 assert.equal(w.controller.st.meta.product,'SMK-37 Elite_x');assert.equal(w.controller.st.image[1],40);
 assert.equal(w.controller.st.dev.kind,'stock');assert.equal($('devname').textContent,'SMK-37 Elite');
 assert.equal($('go').disabled,false);assert.match($('plantext').textContent,/SMK-37 Elite is a beta/);
 assert.equal($('st-options').hidden,true);assert.equal(w.controller.st.boot,null);
 $('release').value='1.2.1';change('release');await tick();   // a version without an Elite package: refused, and said so
 assert.equal(w.controller.st.dev.kind,'model');assert($('go').disabled);
 assert.match($('plantext').textContent,/Melodee 1\.2\.1 has no package for the SMK-37 Elite/);
 found={model:'FM-1',text:'FM-1_016'};
 await w.controller.connect();
 assert.equal(w.controller.st.meta.product,'old');            // (this fixture's FM-1 packages carry made-up identities)
 assert.equal($('devname').textContent,'FM-1');assert.doesNotMatch($('plantext').textContent,/beta/);
 found={model:'Launchkey',text:'Launchkey_1'};                // some other device that answered: not ours, plainly
 await w.controller.connect();
 assert.equal(w.controller.st.dev.kind,'model');assert($('go').disabled);assert.match($('plantext').textContent,/not an FM-1 or an SMK-37/);
 // An SMK-37 Pro found on a stable version without its package: the page says the pre-release has one and opens
 // Advanced installs; choosing the pre-release fetches the Pro's package of it (the model found is kept across builds)
 found={model:'SMK-37 Pro',text:'SMK-37 Pro_016'};$('advanced-installs').open=false;
 await w.controller.connect();
 assert.equal(w.controller.st.dev.kind,'model');assert($('go').disabled);
 assert.match($('plantext').textContent,/The pre-release has one/);assert.equal($('advanced-installs').open,true);
 $('build-rc-dual').checked=true;change('build-rc-dual');await tick();
 assert.equal(pending[0].url,'rc-pro');await finish(pending.shift(),4,50);
 assert.equal(w.controller.st.meta.product,'SMK-37 Pro_rc');assert.equal(w.controller.st.image[1],50);
 assert.equal(w.controller.st.dev.kind,'stock');assert.equal($('go').disabled,false);
 assert.match($('plantext').textContent,/SMK-37 Pro is a beta/);
 // an SMK-37 in M-VAVE's own update mode: refused at Find (M-VAVE's updater finishes that), never resumed from here;
 // in Melodee's loader for its model it resumes. The FM-1 in any loader as before.
 found={model:'ota-SMK-37 Pro',text:'ota-SMK-37 Pro_016'};
 await w.controller.connect();
 assert.equal(w.controller.st.dev.kind,'vendor');assert($('go').disabled);
 assert.match($('plantext').textContent,/SMK-37 Pro is waiting in M-VAVE's update mode \(ota-SMK-37 Pro_016\)/);
 assert.equal(describeDevice({model:'ota-FM-1',text:'ota-FM-1_015'},'FM-1_91061').kind,'loader');
 assert.equal(planFor('vendor',false),null);
 found={model:'ota-SMK-37 Pro',text:'ota-SMK-37 Pro_900'};
 await w.controller.connect();
 assert.equal(w.controller.st.dev.kind,'loader');assert.equal($('go').disabled,false);
 assert.equal(w.controller.st.meta.product,'SMK-37 Pro_rc');
 console.log('Installer: Saadet layout/plan, Melodee channels, cache separation, fetch races, language, write lock and per-model packages passed');
}finally{page.window.close();}
