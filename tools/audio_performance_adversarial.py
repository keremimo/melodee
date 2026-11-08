#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Final device corner cases, using existing state artifacts only. No backups."""
import argparse,contextlib,itertools,json,time
from pathlib import Path
import mido
from prophet_device import patch_get,patch_put,param_set
from audio_performance_device import (ReliableDevice,run,usb_streams,restore_settings,restore_runtime,verify,inventory,param_ids,metadata)

def corners(d,cat,variant,notes):
    case=d.case
    if cat[0]==0:
        for p in range(4):
            # Full native wheel and Poly-Mod routes, all oscillator waves,
            # both output/filter revisions and extreme control values.
            values={8:127,9:127,17:case['cut'],18:127,20:case['model'],21:127,22:127,
                    23:1,24:1,25:1,26:64,27:1,28:1,29:1,30:1,31:1,32:127,33:127}
            for field,value in values.items():
                param_set(d,p,field,value)
    elif cat[0]==2:
        for p in range(4):
            e,raw=patch_get(d,p)
            raw[0]=3|(case['noise']<<2)
            word=case['wave']<<13|((case['wave']+1)%8)<<10|512|case['window']<<6|case['modulation']<<3
            for off in (0,57):raw[14+off]=word>>8;raw[15+off]=word&255
            raw[4]=8;raw[6]=raw[7]=0;raw[9]=600&255;raw[10]=600>>8
            raw[12]=127 if case['vibrato'] else 1;raw[13]=0
            if case.get('envelopes'):
                for off in (0,57):
                    for end,base in ((54,55),(37,38),(20,21)):
                        raw[end+off]=(raw[end+off]&0xf0)|7
                        for k in range(8):raw[base+off+2*k]=127;raw[base+off+2*k+1]=(127-k%2)|(128 if k==6 else 0)
            patch_put(d,p,e,raw)
    elif cat[0]==3:
        for p in range(4):d.params(p,dict(P_E0=case['model'],P_E1=case['wave'],P_E2=case['stack'],P_E3=127 if variant&1 else 1,
                                        P_E6=6,P_E5=15,P_E4=case['cut']))
    elif cat[0]>=4:
        # Long metal/noise/sample overlaps and all choke/retrigger lanes.
        notes=[(p,n) for p in range(4) for n in case['notes']]
    if 'mode' in case:
        for p in range(4):d.params(p,dict(P_VOICE=case['mode'],P_SPRD=42))
    return notes

def events(d,cat,notes,step):
    # Bounded controller bursts and repeated releases/retriggers. Never
    # creates additional slots: release tails consume their reservation.
    for p in range(4):
        d.outgoing.send(mido.Message('pitchwheel',channel=p,pitch=(-8192 if step&1 else 8191)))
        d.outgoing.send(mido.Message('control_change',channel=p,control=1,value=127 if step&1 else 0))
    if step%2==0:
        for p,n in notes:
            if cat[p]<4:d.outgoing.send(mido.Message('note_off',channel=p,note=n))
        for p,n in notes:d.outgoing.send(mido.Message('note_on',channel=p,note=n,velocity=127))

def cases():
    for model,note,cut in itertools.product(range(2),(0,24,60,108),(0,127)):
        yield dict(label='Prophet: sync/Poly-Mod/wheel',cat=0,model=model,note=note,cut=cut,variant=model)
    for v in range(96):yield dict(label='FM6: all algorithms/modes/feedback',cat=1,variant=v,note=0 if v&1 else 108)
    for wave,window,modulation,vibrato in itertools.product(range(8),range(8),(0,3,4),(False,True)):
        yield dict(label='CZ: dual waves/windows/ring/noise',cat=2,wave=wave,window=window,noise=(wave+window)%3,
                   note=24 if wave&1 else 96,variant=wave*8+window,envelopes=window==7,modulation=modulation,vibrato=vibrato)
    for model,wave,stack in itertools.product(range(2),range(8),(1,5,6)):
        yield dict(label='SID: chip/combined/noise/sync/ring',cat=3,model=model,wave=wave,stack=stack,
                   cut=127 if wave&1 else 0,note=12 if wave&1 else 108,variant=wave)
    for cat in (4,5):
        for notes in ((36,38),(39,56),(42,46),(45,49),(51,49),(37,75)):
            yield dict(label='808/909: overlap/retrigger/choke',cat=cat,notes=notes,note=48,variant=cat)
    for cat,mode in itertools.product(range(4),(1,2,3)):
        case=dict(label='Mono/legato/unison',cat=cat,mode=mode,note=60,variant=mode)
        if cat==0:case.update(model=0,cut=127)
        if cat==2:case.update(wave=6,window=7,noise=0,modulation=4,vibrato=True,envelopes=True)
        if cat==3:case.update(model=0,wave=7,stack=1,cut=127)
        yield case

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--backup',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--contract',type=Path,default=Path('tools/audio_voice_contract.json'));p.add_argument('--seconds',type=float,default=.6);p.add_argument('--start',type=int,default=0);p.add_argument('--stop',type=int)
    p.add_argument('--production',action='store_true');p.add_argument('--indices',help='Comma-separated case indices for affected checks')
    p.add_argument('--level',type=int,default=35);a=p.parse_args()
    if not 0<=a.level<=127:p.error('level 0..127')
    if a.output.exists():p.error('Preserve existing artifacts: output must be new')
    if not (a.backup/'manifest.json').is_file():p.error('Existing backup required')
    a.output.mkdir(parents=True);d=ReliableDevice('Melodee');streams=contextlib.ExitStack()
    quotas=json.loads(a.contract.read_text())['caps'];d.performance_fx=d.performance_modulation=True
    d.performance_metadata=True
    d.performance_production=a.production
    d.performance_track_level=a.level
    d.performance_patch_hook=corners;d.performance_step_hook=events
    try:
        d.silence();verify(d,a.backup);restore_runtime(d,a.backup)
        if a.production:
            m=metadata(d)
            if m['profiling'] or m['benchmark_support'] or m['admission']!=1 or m['half_frames']!=128:
                raise RuntimeError(f'Wrong production build: {m}')
        else:d.request(99,[0])
        _,g=param_ids()
        def shared_setup():
            for key,value in dict(G_ROUTE=0,G_CLOCK=0,G_FXMODE=0,G_MFXTYPE=9,G_MFXMIX=127,G_MFXRATE=127,G_MFXCHAR=127,
                                  G_MFX2TYPE=0,G_MFX3TYPE=0,G_FXRETURN=127).items():
                v=value+8192;d.request(3,[1,g[key],v&127,v>>7])
            time.sleep(1.8) # stopped kept-global persistence before measurement
        shared_setup()
        streams.enter_context(usb_streams(a.output/'corners.wav','int24'))
        with (a.output/'cases.jsonl').open('w') as f:
            selected=sorted(set(map(int,a.indices.split(',')))) if a.indices else range(a.start,a.stop or len(list(cases())))
            all_cases=list(cases())
            if a.production:
                selected=sorted(selected,key=lambda i:(all_cases[i]['cat'],all_cases[i]['variant']%3 if all_cases[i]['cat']==1 else 0,i))
            current_fm6_mode=None
            for index in selected:
                case=all_cases[index].copy()
                if a.production and case['cat']==1:
                    mode=case['variant']%3
                    if mode!=current_fm6_mode:
                        d.silence();restore_runtime(d,a.backup,fm6_mode=mode);d.performance_categories=[None]*4;shared_setup()
                        current_fm6_mode=mode
                    case['fm6_mode']=mode
                d.case=case;d.performance_note_base=case['note'];cat=[case['cat']]*4;key=sum(cat[k]*6**(3-k) for k in range(4))
                r=run(d,cat,quotas[key],case['variant'],a.seconds,False);r['case']=case;r['case_index']=index
                r['track_level']=a.level
                # One-shot circuits naturally finish between retriggers.
                r['deadline_safe']=r['peak_complete_upper_us']<.85*r['deadline_us'] and not any(r['delta'][k] for k in
                    ('audio_late','audio_deadline_misses','voices_shed','core1_faults','core1_rejected','core1_timeouts',
                     'resource_failures','missed_frames','play_underruns','play_overruns','cap_underruns','cap_overruns','bad_packets'))
                f.write(json.dumps(r)+'\n');f.flush();print(json.dumps(dict(case=case,peak=r['peak_complete_upper_us'],safe=r['deadline_safe'],delta=r['delta'])),flush=True)
    finally:
        streams.close()
        try:
            if not a.production:d.request(99,[0])
            d.silence();restore_settings(d,a.backup);restore_runtime(d,a.backup);verify(d,a.backup)
            expected=next(e for e in json.loads((a.backup/'manifest.json').read_text()) if e['id']==0)
            assert next(r for r in inventory(d) if r[0]==0)[1:]==(expected['length'],expected['crc'])
            (a.output/'restoration.json').write_text(json.dumps(dict(verified=True,runtime_crc=expected['crc'],persistent_objects=27))+'\n')
        finally:d.close()
if __name__=='__main__':main()
