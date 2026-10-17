#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Measure fixed voices on all 1296 ordered engine assignments. Creates no backups.

Requires the existing original-backup and opt-in AUDIO_PROFILE/AUDIO_BENCH
firmware. Only live runtime is edited; saved objects are verified before and
finally, and the existing runtime is restored in finally. Rows are evidence,
not unconditional guarantees. A fixed benchmark bypasses allocation pressure
and records emergency-shedding requests without reducing its own workload.
"""
import argparse,csv,itertools,json,time,contextlib,wave,zlib
from pathlib import Path
import mido
from prophet_device import Device,param_ids,verify,inventory,u32,pack
from usb_audio_stats import snapshot
NAMES=('PROPHET','FM6','CZ1','SID','808','909')
ENGINES=(19,12,15,3,10,10)
PRESETS=(3,4,0,8,0,1)
SPANS=('half','timer5','input','midi_rx','usb','events','allocation','attack','envelope','modulation',
       'track_control','prophet','fm6_control','fm6','cz','sid','808','909','mix','fx','output')
PANEL_SPANS=('input_blank','input_rows','input_shift','input_led','input_keys','input_encoders','input_diagnostics')
BATCH_SPANS=('batch_prepare','batch_merge','batch_cpu0','batch_cpu1')
DEADLINE_US=128*544/24
COUNTERS=('audio_late','audio_deadline_misses','audio_target_exceeded','voices_shed','voices_given_up',
          'capacity_steals','core1_faults','core1_rejected','core1_timeouts','resource_failures','missed_frames',
          'play_underruns','play_overruns','cap_underruns','cap_overruns','bad_packets','rx_packets','tx_packets')

class ReliableDevice(Device):
    diagnostic_retries=0
    def request(self,cmd,args=()):
        # Only idempotent laboratory/read requests retry automatically.
        # Never retry persistence transactions without restarting their state.
        for attempt in range(3):
            try:return super().request(cmd,args)
            except TimeoutError:
                safe=cmd in (66,98,99,100,68,75,27,31,3,8) or (cmd in (69,76) and args and args[0]==0) or (cmd==95 and args and args[0]==2)
                if not safe or attempt==2:raise
                self.diagnostic_retries+=1
                print(f'Retrying idempotent diagnostic {cmd}',flush=True)
                time.sleep(.05)

def restore_runtime(d,directory,fm6_mode=None):
    """Restart the whole RAM-only transaction after a lost acknowledgement."""
    raw=(directory/'0.bin').read_bytes()
    if fm6_mode is not None:
        # Construct a temporary live fixture in memory from the existing
        # artifact. No snapshot or modified backup is written to disk.
        raw=bytearray(raw);off=len(raw)-88
        if raw[:4]!=b'FBKK' or raw[off:off+4]!=b'FN61' or fm6_mode not in (0,1,2):raise ValueError('Native FBKK function fixture required')
        for p in range(4):raw[off+4+p*16+15]=fm6_mode
        checksum=2166136261
        for byte in raw[:-4]:checksum=((checksum^byte)*16777619)&0xffffffff
        raw[-4:]=checksum.to_bytes(4,'little')
    for attempt in range(3):
        try:
            d.request(74,[3]);time.sleep(.2)
            payloads=[[0,0,*u32(len(raw)),*u32(zlib.crc32(raw))]]
            payloads += [[1,0,*u32(off),*pack(raw[off:off+128])] for off in range(0,len(raw),128)]
            payloads += [[2,0]]
            for payload in payloads:
                r=d.request(67,payload)
                if len(r)<3 or r[2]:raise RuntimeError(f'Existing runtime restore failed: {r}')
                time.sleep(.003)
            print('Runtime restored',flush=True);return
        except TimeoutError:
            if attempt==2:raise
            print('Restarting a timed-out RAM restore',flush=True);time.sleep(.2)


def profile(d,reset=False):
    if getattr(d,'performance_production',False):return {}
    b=d.request(98,[int(reset)])
    if len(b)<3 or b[:2]!=[0,1] or b[2] not in (len(SPANS),len(SPANS+PANEL_SPANS),len(SPANS+PANEL_SPANS+BATCH_SPANS)) or len(b)!=3+(b[2]*3+5)*5:
        raise RuntimeError('Requires AUDIO_PROFILE schema 1')
    names=(SPANS+PANEL_SPANS+BATCH_SPANS)[:b[2]]
    vals=[sum(b[3+k*5+j]<<(7*j) for j in range(5)) for k in range(len(names)*3+5)]
    return {name:dict(ticks=vals[i*3],calls=vals[i*3+1],max_ticks=vals[i*3+2]) for i,name in enumerate(names)} | dict(
        worker_ticks=vals[-5],wait_ticks=vals[-4],worker_jobs=vals[-3],waits=vals[-2],clock_mhz=vals[-1])

def metadata(d):
    if not getattr(d,'performance_metadata',False):return {}
    b=d.request(100)
    if b[:3] not in ([0,1,11],[0,2,12]) or len(b)!=3+5*b[2]:raise RuntimeError('Requires timing/contract metadata')
    values=[sum(b[3+k*5+j]<<(j*7) for j in range(5)) for k in range(b[2])]
    return dict(clock_mhz=values[0],timer5_max_ticks=values[1],irq_max_ticks=values[2],profiling=values[3],benchmark_support=values[4],
                admission=values[5],half_frames=values[6],caps=values[7:11],**(dict(budget_points=values[11]) if b[1]==2 else {}))

def stats(d,reset=False):
    for attempt in range(3):
        try:
            s=snapshot(d.incoming,d.outgoing,reset,True,True,True,True,True)
            s['bench_shed_requests']=s['fixed_voice_bench']>>1
            s['fixed_voice_bench']&=1
            return s
        except TimeoutError:
            if attempt==2:raise

def setup(d,categories,voices,variant):
    d.silence()
    previous=getattr(d,'performance_categories',[None]*4)
    for p,c in enumerate(categories):
        if previous[p]==c:
            if c==0:
                r=d.request(95,[2,p,20,variant&1])
                if r[1]:raise RuntimeError('Prophet filter mode rejected')
            elif c==1:
                if not getattr(d,'performance_production',False) and d.request(99,[2,p,variant%3])!=[0]:raise RuntimeError('FM6 mode rejected')
                r=d.request(68,[0,p]);pk=r[3:];pk[110]=variant%32
                if d.request(69,[0,p,*pk])!=[0,p,0]:raise RuntimeError('FM6 algorithm rejected')
            elif c==3:d.params(p,dict(P_E0=variant&1,P_E2=(1,5,6)[variant%3]))
            continue
        # Track selection ignores tracks not in use; never load into another track.
        r=d.request(27,[p])
        if not r or r[0]!=p:raise RuntimeError(f'Track {p+1} is not in use; screening needs all four tracks')
        r=d.request(8,[ENGINES[c],PRESETS[c]])
        if r[:2]!=[ENGINES[c],PRESETS[c]]:raise RuntimeError(f'Preset rejected: {p}/{c}')
        common=dict(P_VOICE=0,P_LEVEL=getattr(d,'performance_track_level',35),P_ALLOC=0,P_MUTE=0,P_GLIDE=0,P_AMODE=0,P_QUANT=0,P_CHRD=0,P_TRANS=0,P_SPRD=0,
                    P_LD_PIT=0,P_LD_FLT=0,P_LD_SHP=0,P_LD_AMP=0,P_M1AMT=0,P_M2AMT=0,P_M3AMT=0,P_M4AMT=0,
                    P_FXTYPE=0,P_FX2TYPE=0,P_FX3TYPE=0,P_FXSEND=127 if getattr(d,'performance_fx',False) else 0,P_FXSEND2=0,P_FXSEND3=0)
        if getattr(d,'performance_modulation',False):
            common.update(P_M1SRC=1,P_M1DST=2,P_M1AMT=24,P_M2SRC=2,P_M2DST=1,P_M2AMT=24,
                          P_M3SRC=3,P_M3DST=3,P_M3AMT=24,P_M4SRC=4,P_M4DST=4,P_M4AMT=24)
        if c==0:
            for field,value in {3:1,4:1,5:1,6:1,7:1,10:1,14:100,15:90,17:85,18:80,20:variant&1,
                                32:70,33:75,34:1,35:1,36:1,44:0,48:127,50:72,51:1}.items():
                r=d.request(95,[2,p,field,value])
                if len(r)<2 or r[1]:raise RuntimeError(f'Prophet field rejected: {field}')
        elif c==1:
            if not getattr(d,'performance_production',False) and d.request(99,[2,p,variant%3])!=[0]:raise RuntimeError("FM6 benchmark mode rejected")
            r=d.request(68,[0,p]);pk=r[3:]
            if r[:3]!=[0,p,0] or len(pk)!=128:raise RuntimeError('FM6 patch read')
            pk[110]=variant%32;pk[111]=(pk[111]&~7)|7
            # All operators audible, while retaining the native PAD envelopes.
            for op in range(6):pk[op*17+14]=90
            if d.request(69,[0,p,*pk])!=[0,p,0]:raise RuntimeError('FM6 patch write')
        elif c==2:
            r=d.request(75,[0,p]);raw=bytearray(r[3+2*i]|r[4+2*i]<<4 for i in range(144));raw[0]=3
            nibbles=[n for v in raw for n in (v&15,v>>4)]
            if d.request(76,[0,p,*nibbles])!=[0,p,0]:raise RuntimeError('CZ tone write')
        elif c==3:common.update(P_E0=variant&1,P_E2=(1,5,6)[variant%3],P_SUS=127)
        else:common['P_E0']=4+(c==5)
        d.params(p,common)
    d.performance_categories=list(categories)
    d.request(27,[0]);time.sleep(.02)
    base=getattr(d,'performance_note_base',48)
    drum_notes=(36,38,39,42,45,37,56,49)
    notes=[(p,drum_notes[k] if c>=4 else min(127,base+p*2+7*k)) for p,c in enumerate(categories) for k in range(voices[p])]
    return notes

def run(d,categories,voices,variant,seconds,fixed):
    if fixed and d.request(99,[1])!=[0]:raise RuntimeError('Requires AUDIO_BENCH firmware')
    retry_start=d.diagnostic_retries
    notes=setup(d,categories,voices,variant)
    hook=getattr(d,'performance_patch_hook',None)
    if hook:notes=hook(d,categories,variant,notes)
    profile(d,True);before=stats(d,True)
    for p,n in notes:d.outgoing.send(mido.Message('note_on',channel=p,note=n,velocity=110))
    start=time.monotonic();rows=[]
    while time.monotonic()-start<seconds:
        time.sleep(min(.08,max(0,seconds-(time.monotonic()-start))))
        if fixed:d.request(99,[1])
        hook=getattr(d,'performance_step_hook',None)
        if hook:hook(d,categories,notes,len(rows)+1)
        # Drum circuits end by design: a retrigger keeps the fixed lane count.
        for p,n in notes:
            if categories[p]>=4:d.outgoing.send(mido.Message('note_on',channel=p,note=n,velocity=110))
        # Let this burst pass the musical lookback and render before reading
        # its counters. The last retrigger is part of the measured window too.
        time.sleep(.01);rows.append(stats(d))
    after=rows[-1];spans=profile(d);meta=metadata(d)
    delta={k:(after[k]-before[k])&0xffffffff for k in COUNTERS+('bench_shed_requests',)}
    peak=max(r['audio_full_max_us'] for r in rows)
    active_min=min(r['voices_active'] for r in rows)
    return dict(assignment=list(categories),engines=[NAMES[c] for c in categories],voices=list(voices),variant=variant,
                seconds=time.monotonic()-start,fixed_bench=fixed,diagnostic_retries=d.diagnostic_retries-retry_start,peak_complete_upper_us=peak,deadline_us=DEADLINE_US,
                headroom_pct=100*(1-peak/DEADLINE_US),voices_min=active_min,voices_expected=sum(voices),
                core1_online_all=all(r['core1_online'] for r in rows),delta=delta,resource_peak=after['resource_peak'],
                normal_budget_fades=max(0,delta['voices_given_up']-delta['voices_shed']) if 'budget_points' in meta else 0,
                resource_capacity=after['resource_capacity'],profile=spans,metadata=meta,
                usb_play_alt=after['play_alt'],usb_capture_alt=after['cap_alt'],
                passed=peak<=.8*DEADLINE_US and active_min==sum(voices) and all(r['core1_online'] for r in rows)
                       and after['fixed_voice_bench']==int(fixed)
                       and (not meta or (meta['timer5_max_ticks']<2400 and meta['clock_mhz']==360 and (fixed or (all(c>=v for c,v in zip(meta['caps'],voices)) if 'budget_points' in meta else meta['caps']==list(voices)))))
                       and (not delta['voices_given_up'] or 'budget_points' in meta)
                       and not any(delta[k] for k in ('audio_late','audio_deadline_misses','voices_shed',
                           'bench_shed_requests','core1_faults','core1_rejected','core1_timeouts','resource_failures')))

@contextlib.contextmanager
def usb_streams(path,format='int16'):
    import sounddevice as sd
    devices=sd.query_devices()
    inp=next(i for i,d in enumerate(devices) if d['name'] in ('Melodee', 'Melodee In') and d['max_input_channels'] > 0)
    out=next(i for i,d in enumerate(devices) if d['name'] in ('Melodee', 'Melodee Out') and d['max_output_channels'] >= 2)
    channels=min(2,devices[inp]['max_input_channels']);statuses=[]
    with wave.open(str(path),'wb') as wav:
        wav.setnchannels(channels);wav.setsampwidth(2 if format=='int16' else 3);wav.setframerate(44100)
        def capture(data,frames,timing,status):
            if status:statuses.append(str(status))
            wav.writeframesraw(bytes(data))
        def playback(data,frames,timing,status):
            if status:statuses.append(str(status))
            data[:]=bytes(len(data))
        with sd.RawInputStream(device=inp,channels=channels,samplerate=44100,dtype=format,callback=capture), \
             sd.RawOutputStream(device=out,channels=2,samplerate=44100,dtype=format,callback=playback):
            # Measure steady streaming after CoreAudio alternate/feedback
            # negotiation. Opening/configuration is outside these windows.
            time.sleep(1.3);yield statuses
    path.with_suffix('.status.json').write_text(json.dumps(dict(format=format,channels=channels,callback_statuses=statuses),indent=2)+'\n')

def proposed_voices(seed):
    voices=[2]*4;projected=seed['peak_complete_upper_us']+160.0
    for _ in range(4):
        if projected<=.8*DEADLINE_US-80.0:break
        candidates=[p for p in range(4) if voices[p]>1]
        def cost(p):
            c=seed['assignment'][p];name=('prophet','fm6','cz','sid','808','909')[c]
            return max(80.0,seed['profile'][name]['max_ticks']/24*2)
        p=max(candidates,key=cost);projected-=cost(p);voices[p]-=1
    return voices

def calibrated_run(d,cat,variant,seconds,fixed,seed=None):
    voices=proposed_voices(seed) if seed else [2]*4;attempts=[]
    for _ in range(5):
        r=run(d,cat,voices,variant,seconds,fixed);attempts.append(dict(voices=list(voices),peak=r['peak_complete_upper_us'],passed=r['passed']))
        if r['passed'] and r['peak_complete_upper_us']<=.8*DEADLINE_US-80.0:break
        candidates=[p for p in range(4) if voices[p]>1]
        if not candidates:break
        def cost(p):
            name=('prophet','fm6','cz','sid','808','909')[cat[p]]
            return r['profile'][name]['max_ticks']
        p=max(candidates,key=cost);voices[p]-=1
    r['attempts']=attempts;r['calibration_margin_us']=80
    return r

def restore_settings(d,directory):
    entries=json.loads((directory/'manifest.json').read_text())
    original=next(e for e in entries if e['id']==1)
    current=next(row for row in inventory(d) if row[0]==1)
    if current[1:]==(original['length'],original['crc']):return
    raw=(directory/'1.bin').read_bytes()
    payloads=[[0,1,*u32(len(raw)),*u32(original['crc'])]]
    payloads += [[1,1,*u32(off),*pack(raw[off:off+128])] for off in range(0,len(raw),128)]
    payloads += [[2,1]]
    for payload in payloads:
        reply=d.request(67,payload)
        if len(reply)<3 or reply[2]:raise RuntimeError(f'Existing settings restore failed: {reply}')
    print('Original settings restored from existing artifact',flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',default='Melodee');p.add_argument('--backup',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--seconds',type=float,default=.16)
    p.add_argument('--voices',type=int,default=2);p.add_argument('--assignment',help='four category digits, 0..5')
    p.add_argument('--normal-admission',action='store_true');p.add_argument('--calibrate',action='store_true');p.add_argument('--seed',type=Path);
    p.add_argument('--contract',type=Path,help='Use measured per-assignment quotas instead of --voices')
    p.add_argument('--fx',action='store_true',help='One fully wet shared Chorus, all tracks send')
    p.add_argument('--modulation',action='store_true',help='Four enabled modulation slots per track')
    p.add_argument('--note-base',type=int,default=48)
    p.add_argument('--level',type=int,default=35)
    p.add_argument('--track-voices',help='Four comma-separated voice counts, including 0 for idle tracks')
    p.add_argument('--budget',type=int,help='Set the live device shared allowance (8..32 points)')
    p.add_argument('--worst',type=int,help='Replay this many slowest rows from --seed, retaining their variants')
    p.add_argument('--event-bursts',action='store_true',help='Pitch/CC1 bursts and alternating releases/retriggers')
    p.add_argument('--production',action='store_true',help='Profiling and laboratory commands compiled out; normal admission only')
    p.add_argument('--metadata',action='store_true',help='Read complete timing allowance and compiled contract/clock')
    p.add_argument('--usb',action='store_true');p.add_argument('--usb-format',choices=('int16','int24'),default='int16');p.add_argument('--start',type=int,default=0);p.add_argument('--stop',type=int,default=1296)
    a=p.parse_args()
    if a.voices<1 or a.voices>4 or a.seconds<=0:p.error('voices 1..4; seconds >0')
    if not 0<=a.level<=127:p.error('level 0..127')
    requested=list(map(int,a.track_voices.split(','))) if a.track_voices else None
    if requested and (len(requested)!=4 or any(v<0 or v>16 for v in requested)):p.error('track voices: four counts 0..16')
    if a.budget is not None and not 8<=a.budget<=32:p.error('budget 8..32')
    if a.worst is not None and (not a.seed or a.worst<1 or a.calibrate):p.error('--worst requires --seed and a positive count, without --calibrate')
    if a.production and (not a.normal_admission or a.calibrate):p.error('Production requires --normal-admission, without --calibrate')
    if not (a.backup/'manifest.json').is_file() or not (a.backup/'0.bin').is_file():p.error('Existing complete backup required; never creates one')
    if a.output.exists():p.error('Output already exists; preserve earlier results')
    combinations=list(itertools.product(range(6),repeat=4))
    if a.assignment:
        if len(a.assignment)!=4 or any(c not in '012345' for c in a.assignment):p.error('assignment: four digits 0..5')
        combinations=[tuple(map(int,a.assignment))]
    else:combinations=combinations[a.start:a.stop]
    seeds={tuple(r['assignment']):r for r in (json.loads(l) for l in a.seed.read_text().splitlines())} if a.seed else {}
    if a.worst:combinations=[tuple(r['assignment']) for r in sorted(seeds.values(),key=lambda r:r['peak_complete_upper_us'],reverse=True)[:a.worst]]
    quotas=json.loads(a.contract.read_text())['caps'] if a.contract else None
    a.output.mkdir(parents=True);d=ReliableDevice(a.port);streams=contextlib.ExitStack()
    d.performance_fx=a.fx;d.performance_modulation=a.modulation;d.performance_note_base=a.note_base;d.performance_production=a.production
    d.performance_metadata=a.metadata or a.production
    d.performance_track_level=a.level
    if a.event_bursts:
        from audio_performance_adversarial import events
        d.performance_step_hook=events
    try:
        d.silence();verify(d,a.backup);restore_runtime(d,a.backup)
        if a.production:
            m=metadata(d)
            if m['profiling'] or m['benchmark_support'] or m['admission']!=1 or m['half_frames']!=128:
                raise RuntimeError(f'Wrong production build: {m}')
        _,g=param_ids()
        if a.budget is not None:
            if 'budget_points' not in metadata(d):raise RuntimeError('Requires adjustable shared-budget firmware')
            v=a.budget+8192;d.request(3,[1,g['G_BUDGET'],v&127,v>>7])
        for key,value in dict(G_ROUTE=0,G_CLOCK=0,G_MFXTYPE=0,G_MFX2TYPE=0,G_MFX3TYPE=0).items():
            v=value+8192;d.request(3,[1,g[key],v&127,v>>7])
        if a.fx:
            for key,value in dict(G_FXMODE=0,G_MFXTYPE=9,G_MFXMIX=127,G_MFXRATE=127,G_MFXCHAR=127,G_FXRETURN=127).items():
                v=value+8192;d.request(3,[1,g[key],v&127,v>>7])
        # CLOCK/ROUTE are persisted after 1500 ms. Let that stopped setup
        # finish before opening USB or measuring live notes; flash pauses
        # are not synthesis deadline cost and must not enter these windows.
        time.sleep(1.8)
        if a.usb:streams.enter_context(usb_streams(a.output/'matrix-capture.wav',a.usb_format))
        with (a.output/'matrix.jsonl').open('w') as f:
            for i,cat in enumerate(combinations,a.start):
                if a.worst:i=seeds[cat]['variant']
                q=requested or (quotas[((cat[0]*6+cat[1])*6+cat[2])*6+cat[3]] if quotas else [a.voices]*4)
                r=calibrated_run(d,cat,i,a.seconds,not a.normal_admission,seeds.get(cat)) if a.calibrate else run(d,cat,q,i,a.seconds,not a.normal_admission)
                r.update(shared_chorus=a.fx,modulation_slots=4 if a.modulation else 0,note_base=a.note_base,track_level=a.level,event_bursts=a.event_bursts)
                f.write(json.dumps(r)+'\n');f.flush();print(json.dumps({k:r[k] for k in ('engines','peak_complete_upper_us','voices_min','passed','delta')}),flush=True)
    finally:
        streams.close()
        try:
            if not a.production:
                try:d.request(99,[0])
                except TimeoutError:pass
            d.silence();restore_settings(d,a.backup);restore_runtime(d,a.backup);verify(d,a.backup)
            expected=json.loads((a.backup/'manifest.json').read_text());runtime=next(e for e in expected if e['id']==0)
            current=next(row for row in inventory(d) if row[0]==0)
            if current[1:]!=(runtime['length'],runtime['crc']):raise RuntimeError('Runtime CRC not restored')
            (a.output/'restoration.json').write_text(json.dumps(dict(runtime_crc=runtime['crc'],persistent_objects=len(expected)-1,verified=True),indent=2)+'\n')
        finally:streams.close();d.close()
    rows=[json.loads(l) for l in (a.output/'matrix.jsonl').read_text().splitlines()]
    with (a.output/'matrix.csv').open('w',newline='') as f:
        w=csv.writer(f);w.writerow(('track1','track2','track3','track4','voices','peak_complete_upper_us','deadline_misses','emergency_requests','emergency_cuts','headroom_pct','passed'))
        for r in rows:w.writerow((*r['engines'],'/'.join(map(str,r['voices'])),r['peak_complete_upper_us'],r['delta']['audio_deadline_misses'],r['delta']['bench_shed_requests'],r['delta']['voices_shed'],r['headroom_pct'],r['passed']))
    summary=dict(cases=len(rows),passed=sum(r['passed'] for r in rows),worst=max(rows,key=lambda r:r['peak_complete_upper_us']))
    (a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
if __name__=='__main__':main()
