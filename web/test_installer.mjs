// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
import assert from 'node:assert/strict';
import {pageDOM,controllerSource,template} from './installer_test_helpers.mjs';
import {describeDevice,planFor,progressModel,remaining} from './install/progress.js';
const page=pageDOM(),w=page.window,d=w.document,$=id=>d.getElementById(id);
const releases=[{version:'1.3.2',product:'new',pkg:{dual:'new-dual'}},{version:'1.2.1',product:'old',pkg:{dual:'old-dual'}}];
const meta={...releases[0],releases,rc:{version:'1.4.0-rc.1',product:'rc',pkg:{dual:'rc-dual'}}};
const pending=[];
Object.assign(w,{
 meta,describeDevice,planFor,progressModel,remaining,logicalImage:b=>b,productOf:b=>['new','old','rc'][b[0]],
 fetch:url=>url.includes('-res.bin')?Promise.resolve({ok:false,status:404}):new Promise(resolve=>pending.push({url,resolve})),
 setupLeaving:()=>({lock(){},applyLang(){}}),hold:async()=>()=>{},busyElsewhere:async()=>false,
});
w.eval(controllerSource+'\nwindow.controller={st,render,log,loadRelease,resRegion};');
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
 assert.deepEqual(urls,['../../firmware/melodee-nightly-res.bin?commit=abc']);
 assert.deepEqual(planFor('this',true),{firmware:false,resume:false,boot:true});
 assert.deepEqual(planFor('loader',true),{firmware:true,resume:true,boot:true});
 assert.equal(planFor('open',true),null);
 console.log('Installer: Saadet layout/plan, Melodee channels, cache separation, fetch races, language and write lock passed');
}finally{page.window.close();}
