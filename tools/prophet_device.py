#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Prototype-only device measurements and read-only musical backups.

Requires mido/python-rtmidi; --usb also uses sounddevice/numpy. This tool
never saves patches or projects. Firmware installation is a separate step.
"""
import argparse
import contextlib
import hashlib
import json
from pathlib import Path
import re
import time
import zlib
import wave
import mido
from usb_audio_stats import snapshot

HEADER = [0x7D, 0x46, 0x4C]
ROOT = Path(__file__).resolve().parents[1]

def u32(n): return [(n >> (7 * j)) & 127 for j in range(5)]
def r32(b): return sum(x << (7 * j) for j, x in enumerate(b))
def unpack(b):
    return bytes(x | (((b[i] >> j) & 1) << 7)
                 for i in range(0, len(b), 8) for j, x in enumerate(b[i+1:i+8]))
def pack(b):
    out = []
    for i in range(0, len(b), 7):
        group = b[i:i+7]
        out += [sum((x >> 7) << j for j, x in enumerate(group))] + [x & 127 for x in group]
    return out

def param_ids():
    source = re.sub(r'/\*.*?\*/', '', (ROOT/'firmware/src/core.h').read_text(), flags=re.S)
    def ids(start):
        body = source[source.index(start):].split('};', 1)[0]
        return {key: j for j, key in enumerate(re.findall(r'\b[PG]_[A-Z0-9_]+\b', body))}
    return ids('P_LEVEL,'), ids('G_BPM,')

class Device:
    def __init__(self, port):
        self.incoming = mido.open_input(port)
        try: self.outgoing = mido.open_output(port)
        except Exception:
            self.incoming.close()
            raise
        time.sleep(.2)
    def close(self):
        self.incoming.close(); self.outgoing.close()
    def request(self, cmd, args=(), timeout=5):
        self.outgoing.send(mido.Message('sysex', data=HEADER+[cmd]+list(args)))
        end = time.monotonic()+timeout
        while time.monotonic()<end:
            for message in self.incoming.iter_pending():
                if message.type=='sysex' and list(message.data[:4])==HEADER+[cmd]:
                    return list(message.data[4:])
            time.sleep(.003)
        raise TimeoutError(f'No reply to editor command {cmd}')
    def params(self, track, values):
        params, _ = param_ids()
        for key, value in values.items():
            v = value+8192
            self.request(31,[track,params[key],v & 127,(v >> 7) & 127])
    def silence(self):
        self.outgoing.send(mido.Message('stop'))
        for ch in range(4):
            self.outgoing.send(mido.Message('control_change',channel=ch,control=64,value=0))
            self.outgoing.send(mido.Message('control_change',channel=ch,control=120,value=0))
        time.sleep(.3)
    def stats(self, reset=False): return snapshot(self.incoming,self.outgoing,reset,True)

def inventory(d):
    b=d.request(65)
    if b[:2]!=[1,0] or len(b)!=3+11*b[2]: raise RuntimeError(f'Bad backup inventory: {b[:3]}')
    return [(b[p],r32(b[p+1:p+6]),r32(b[p+6:p+11])) for p in range(3,len(b),11)]

def backup(d, directory):
    directory.mkdir(parents=True,exist_ok=False)
    entries=inventory(d);manifest=[]
    for ident,length,crc in entries:
        raw=bytearray()
        for off in range(0,length,256):
            count=min(256,length-off);reply=d.request(66,[ident]+u32(off)+[count & 127,count >> 7])
            if len(reply)<9 or reply[1]!=0: raise RuntimeError(f'Backup read failed: {ident}/{off}: {reply[:9]}')
            raw+=unpack(reply[9:])
        if len(raw)!=length or zlib.crc32(raw)!=crc: raise RuntimeError(f'Backup CRC failed: {ident}')
        (directory/f'{ident}.bin').write_bytes(raw)
        manifest.append(dict(id=ident,length=length,crc=crc,sha256=hashlib.sha256(raw).hexdigest()))
    (directory/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(dict(backup=str(directory),objects=len(entries))),flush=True)

def restore_runtime(d, directory):
    raw=(directory/'0.bin').read_bytes()
    if d.request(67,[0,0]+u32(len(raw))+u32(zlib.crc32(raw)))[2]: raise RuntimeError('Runtime begin failed')
    for off in range(0,len(raw),256):
        if d.request(67,[1,0]+u32(off)+pack(raw[off:off+256]))[2]: raise RuntimeError('Runtime chunk failed')
    if d.request(67,[2,0])[2]: raise RuntimeError('Runtime restore failed')
    print('Runtime restored',flush=True)

def verify(d, directory):
    expected=json.loads((directory/'manifest.json').read_text())
    current=inventory(d);lookup={ident:(length,crc) for ident,length,crc in current}
    # Runtime is separately restored; every persistent object's length and CRC must match.
    differences=[e['id'] for e in expected if e['id']!=0 and lookup.get(e['id'])!=(e['length'],e['crc'])]
    if differences: raise RuntimeError(f'Persistent objects changed: {differences}')
    print('All persistent musical objects retain their original CRC',flush=True)

@contextlib.contextmanager
def usb_streams(enabled):
    if not enabled:
        yield; return
    import sounddevice as sd
    devices=sd.query_devices()
    inp=next(i for i,x in enumerate(devices) if x['name'] in ('Melodee', 'Melodee In') and x['max_input_channels'] > 0)
    out=next(i for i,x in enumerate(devices) if x['name'] in ('Melodee', 'Melodee Out') and x['max_output_channels'] >= 2)
    def silence_output(outdata,frames,when,status): outdata.fill(0)
    with sd.InputStream(device=inp,channels=4,samplerate=44100,dtype='int16',callback=lambda *args:None), \
         sd.OutputStream(device=out,channels=2,samplerate=44100,dtype='int16',callback=silence_output):
        time.sleep(.3)
        yield

def patch(raw, mode, stress):
    raw=bytearray(raw);raw[20]=mode
    if stress:
        for k in (3,4,5,6,7,10,12,34,35,36,51): raw[k]=1
        for k in (14,15,18,33,48): raw[k]=127
        raw[0]=64;raw[1]=24;raw[8]=raw[9]=64;raw[17]=88;raw[37]=0
        raw[21]=120;raw[22]=120;raw[26]=60
        for k in (23,24,25,27,28,29,30,31,38,39,41,42):raw[k]=1
        raw[52]=0;raw[53]=5;raw[86]=11;raw[87]=3
        raw[32]=127;raw[43]=raw[44]=0;raw[47]=80;raw[49]=raw[50]=0
    return bytes([1,0x32,3]+pack(raw))

def measure(d,seconds,label,expected,before,expected_steals=0):
    results=[];end=time.monotonic()+seconds
    while time.monotonic()<end:
        time.sleep(min(.5,max(0,end-time.monotonic())))
        results.append(d.stats())
    last=results[-1]
    row=dict(label=label,samples=len(results),voices_expected=expected,
             voices_min=min(x['voices_active'] for x in results),
             allocation_steals_expected=expected_steals,
             cpu_percent_max=max(x['cpu_q8']*100/256 for x in results),
             half_render_max_us=max(x['audio_max_us'] for x in results),
             usb_play_alt=last['play_alt'],usb_capture_alt=last['cap_alt'],
             counter_delta={k:last[k]-before[k] for k in ('audio_late','voices_shed','voices_given_up',
                            'play_underruns','play_overruns','cap_underruns','cap_overruns','missed_frames')})
    row['pass']=row['voices_min']==expected and row['half_render_max_us']<int(128*1e6/44100*.85) and \
                all(value==(expected_steals if key=='voices_given_up' else 0) for key,value in row['counter_delta'].items())
    print(json.dumps(row),flush=True);return row

def bench(d,args):
    d.silence()
    _,globals_=param_ids()
    # Fixed channel routing (the original runtime is restored afterwards).
    d.request(3,[1,globals_['G_ROUTE'],0,64])
    scenarios=[('baseline-analog',None)] if args.baseline else [('ssi',0),('curtis',1)]
    raw=unpack(next(Path(args.factory).glob('Prophet*/P5*USER*.syx')).read_bytes()[6:158]) if args.factory else None
    rows=[]
    try:
        for label,mode in scenarios:
            for full_mix in (False,True):
                d.silence()
                for tr in range(4):
                    d.request(27,[tr]);d.request(8,[(0,3,2,5)[tr],0])
                    d.params(tr,dict(P_LEVEL=64,P_PAN=0,P_MUTE=0,P_VOICE=0,P_GLIDE=0,
                                     P_AMODE=0,P_AHOLD=0,P_RESERVED45=0,P_SUS=127,
                                     P_QUANT=0,P_CHRD=0,P_TRANS=0,P_ED_PIT=0,
                                     P_LD_PIT=0,P_LD_FLT=0,P_LD_AMP=0,P_ED_FLT=0,
                                     P_M1AMT=0,P_M2AMT=0,P_M3AMT=0,P_M4AMT=0,
                                     P_RESERVED33=30,P_RESERVED34=70,P_RESERVED35=70,P_RESERVED36=70))
                if mode is not None:
                    reply=d.request(95,[1,0]+list(patch(raw,mode,True)))
                    if reply[:2]!=[1,0]:raise RuntimeError(f'Native patch PUT failed: {reply[:3]}')
                    readback=d.request(95,[0,0])
                    if readback[3:]!=list(patch(raw,mode,True)):raise RuntimeError('Native patch readback mismatch')
                d.request(27,[0])  # Show the Prophet track while measuring the mix.
                with usb_streams(args.usb):
                    # Include note attack and overload shedding in the window.
                    # Resetting after a warmup hides the expensive onset.
                    d.stats(True);before=d.stats()
                    for k in range(5):d.outgoing.send(mido.Message('note_on',channel=0,note=args.note_base+3*k,velocity=110))
                    if full_mix:
                        for ch in range(1,4):d.outgoing.send(mido.Message('note_on',channel=ch,note=48+ch*7,velocity=100))
                    expected=(8 if args.baseline else args.mixed_expected) if full_mix else 5
                    row=measure(d,args.seconds,label+('-mixed' if full_mix else '-five'),expected,before,8-expected if full_mix else 0)
                    row['note_base']=args.note_base;rows.append(row)
    finally:
        Path(args.output).write_text(json.dumps(rows,indent=2)+'\n')
        d.silence()
        if args.backup:restore_runtime(d,Path(args.backup))

def recordings(d,args):
    """Dry device stems; identical musical settings for both filter modes."""
    import sounddevice as sd
    import numpy as np
    directory=Path(args.output);directory.mkdir(parents=True,exist_ok=True)
    raw=unpack(next(Path(args.factory).glob('Prophet*/P5*USER*.syx')).read_bytes()[6:158])
    devices=sd.query_devices()
    inp=next(i for i,x in enumerate(devices) if x['name'] in ('Melodee', 'Melodee In') and x['max_input_channels'] > 0)
    out=next(i for i,x in enumerate(devices) if x['name'] in ('Melodee', 'Melodee Out') and x['max_output_channels'] >= 2)
    rows=json.loads((directory/'manifest.json').read_text()) if args.action=='factory-recordings' and (directory/'manifest.json').exists() else []
    try:
        d.silence();_,g=param_ids();d.request(3,[1,g['G_ROUTE'],0,64])
        for tr in range(4):
            d.request(27,[tr]);d.request(8,[0,0])
            d.params(tr,dict(P_MUTE=int(tr!=0),P_LEVEL=64,P_PAN=0,P_VOICE=0,
                            P_AMODE=0,P_AHOLD=0,P_RESERVED45=0,P_QUANT=0,P_CHRD=0,P_TRANS=0,
                            P_RESERVED33=0,P_RESERVED34=0,P_RESERVED35=0,P_RESERVED36=0,P_LD_AMP=0,
                            P_LD_FLT=0,P_LD_PIT=0,P_ED_FLT=0,P_ED_PIT=0,
                            P_M1AMT=0,P_M2AMT=0,P_M3AMT=0,P_M4AMT=0))
        bank=next(Path(args.factory).glob('Prophet*/P5*USER*.syx')).read_bytes()
        programs=[bank[i:i+159] for i in range(0,len(bank),159)]
        native=args.action=='factory-recordings'
        choices=tuple(n-1 for n in args.programs) if native and args.programs else (0,2,3,5,14,16,23) if native else ('pad','brass','bass','sync','polymod')
        for sound in choices:
            for mode in (0,1):
                d.silence();r=bytearray(unpack(programs[sound][6:-1]) if native else raw)
                if not native:r[:55]=bytes(55)
                for k,value in ({} if native else {0:24,1:25,3:1,5:1,8:64,9:64,12:1,14:127,15:90,
                                17:65,18:50,20:mode,37:105,40:70,45:60,46:60,
                                47:30,48:127,49:65,50:65,51:1}).items():r[k]=value
                if native:r[20]=mode
                elif sound=='pad':r[43]=40;r[44]=65;r[49]=r[50]=80
                elif sound=='brass':r[43]=42;r[44]=15
                elif sound=='bass':r[17]=45;r[18]=75;r[48]=80
                else:
                    r[0]=64;r[1]=24;r[15]=0;r[17]=95;r[18]=35
                    if sound=='sync':r[10]=1;r[32]=90;r[34]=1
                    else:
                        r[5]=0;r[6]=1;r[4]=1;r[33]=95;r[34]=r[35]=r[36]=1
                body=(list(programs[sound][1:6]) if native else [1,0x32,3])+pack(r)
                if d.request(95,[1,0]+body)[:2]!=[1,0]:raise RuntimeError('Recording patch rejected')
                if d.request(95,[0,0])[3:]!=body:raise RuntimeError('Recording patch readback mismatch')
                d.request(27,[0])
                notes=([36] if sound==3 else [60] if sound in (16,23) else [60,64,67,71,74]) if native else \
                      [60,63,67,70,74] if sound in ('pad','brass') else [36 if sound=='bass' else 60]
                name=bytes(r[65:85]).decode('ascii',errors='replace').strip()
                stem=(f'{sound+1:03d}-'+re.sub(r'[^a-z0-9]+','-',name.lower()).strip('-')) if native else sound
                filename=stem+'-'+('curtis' if mode else 'ssi')+'.wav'
                (directory/filename.replace('.wav','.syx')).write_bytes(bytes([0xf0]+body+[0xf7]))
                chunks=[];statuses=[]
                def collect(data,frames,when,status):
                    chunks.append(data.copy())
                    if status:statuses.append(str(status))
                def silent(data,frames,when,status):data.fill(0)
                with sd.InputStream(device=inp,channels=4,samplerate=44100,dtype='int16',callback=collect), \
                     sd.OutputStream(device=out,channels=2,samplerate=44100,dtype='int16',callback=silent):
                    time.sleep(.2);chunks.clear();before=d.stats(True)
                    for note in notes:d.outgoing.send(mido.Message('note_on',channel=0,note=note,velocity=110))
                    time.sleep(2)
                    for note in notes:d.outgoing.send(mido.Message('note_off',channel=0,note=note,velocity=0))
                    time.sleep(4);stats=d.stats()
                pcm=np.concatenate(chunks)[:,0].astype('<i2')
                with wave.open(str(directory/filename),'wb') as file:
                    file.setnchannels(1);file.setsampwidth(2);file.setframerate(44100);file.writeframes(pcm.tobytes())
                row=dict(file=filename,frames=len(pcm),peak=int(np.abs(pcm.astype(np.int32)).max()),
                         clipped_samples=int(np.count_nonzero((pcm==32767)|(pcm==-32768))),
                         factory_program=sound+1 if native else None,patch_name=name,filter_mode=mode,
                         notes=notes,native_sha256=hashlib.sha256(bytes([0xf0]+body+[0xf7])).hexdigest(),
                         original_sha256=hashlib.sha256(programs[sound]).hexdigest() if native else None,
                         host_stream_status=statuses,device_stats=stats,
                         counter_delta={k:stats[k]-before[k] for k in ('audio_late','voices_shed','voices_given_up',
                            'play_underruns','play_overruns','cap_underruns','cap_overruns','missed_frames')})
                rows=[x for x in rows if x['file']!=filename]
                rows.append(row);print(json.dumps(row),flush=True)
    finally:
        (directory/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
        d.silence()
        if args.backup:restore_runtime(d,Path(args.backup))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['backup','restore-runtime','verify','bench','recordings','factory-recordings'])
    parser.add_argument('--port',default='Felucca');parser.add_argument('--backup')
    parser.add_argument('--factory');parser.add_argument('--baseline',action='store_true')
    parser.add_argument('--note-base',type=int,default=60)
    parser.add_argument('--programs',type=int,nargs='+',help='one-based factory program numbers for factory-recordings')
    parser.add_argument('--mixed-expected',type=int,choices=range(4,9),default=6)
    parser.add_argument('--usb',action='store_true');parser.add_argument('--seconds',type=float,default=5)
    parser.add_argument('--output',default='build/prophet5/device-performance.json')
    args=parser.parse_args()
    if not 0<=args.note_base<=115:parser.error('--note-base must be 0..115')
    if args.seconds<1:parser.error('--seconds must be at least 1')
    if args.programs and (args.action!='factory-recordings' or any(n<1 or n>200 for n in args.programs)):parser.error('--programs requires factory-recordings and program numbers 1..200')
    if args.action not in ('bench','recordings','factory-recordings') and not args.backup:parser.error('--backup directory is required')
    if (args.action in ('recordings','factory-recordings') or args.action=='bench' and not args.baseline) and not args.factory:parser.error('--factory is required for prototype measurements')
    d=Device(args.port)
    try:
        if args.action=='backup':backup(d,Path(args.backup))
        elif args.action=='restore-runtime':restore_runtime(d,Path(args.backup))
        elif args.action=='verify':verify(d,Path(args.backup))
        elif args.action in ('recordings','factory-recordings'):recordings(d,args)
        else:bench(d,args)
    finally:d.close()
if __name__=='__main__':main()
