/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Fractional manual edits and range transforms use the existing precise event
 * format. One bounded snapshot makes conversion, move and copy atomic/undoable. */
typedef struct {
    step_t steps[NSTEP];
    uint32_t generation, pattern_gen, signature;
    motion_store_t motion;
    uint8_t valid, redo, track, cursor, len, clear;
    uint8_t pattern_sound, sound_clear;
    uint8_t slen;                /* LEN to swap in too (a doubled pattern), 0 = LEN stays */
    uint8_t pitch;               /* a range's pitch turns, still open: the next turn keeps this snapshot */
    uint16_t note_count;
    recorded_note_t notes[RECORD_MAX]; /* variable allocation tail */
} note_transaction_t;
static note_transaction_t *note_transaction_ptr;
#define note_transaction (*note_transaction_ptr)

static int32_t note_snap(int32_t value,int32_t delta,uint32_t quantum)
{
    if(!delta)return value;
    /* Every editor grid is a power of two; inputs are nonnegative. */
    int32_t base=(value+(delta<0?(int32_t)quantum-1:0))&~((int32_t)quantum-1);
    return base+clamp(delta,-128,128)*(int32_t)quantum;
}
static void note_duration(recorded_note_t *r,uint32_t duration)
{
    uint32_t exp=0;while(duration>65535u&&exp<7u){duration>>=1;exp++;}
    r->duration=(uint16_t)duration;recorded_set_exponent(r,exp);
}
static void note_onset(recorded_note_t *r,int32_t shift,uint32_t len,uint32_t quantum)
{
    int32_t position=recorded_step(r)*RECORD_UNIT+r->on+shift;
    /* Quantize grids are powers of two that divide a step; zero means move. */
    if(quantum)position=(position+(int32_t)quantum/2)&~((int32_t)quantum-1);
    int32_t loop=(int32_t)(len*RECORD_UNIT);position%=loop;if(position<0)position+=loop;
    uint32_t actual=(uint32_t)position/RECORD_UNIT;
    r->on=(uint16_t)((uint32_t)position%RECORD_UNIT);
    uint32_t rounded=r->on>=RECORD_UNIT/2u;
    recorded_set_step(r,actual,rounded,rounded&&actual+1u==len&&actual);
}
static int note_transaction_begin_track(track_t *t)
{
    recording_prune(t);
    uint32_t count=note_history_count(-1);
    note_transaction_ptr=resource_get(RES_NOTE_HISTORY,__builtin_offsetof(note_transaction_t,notes)+note_history_bytes(count));
    if(!note_transaction_ptr){ui_message("NO EDIT MEMORY");return 0;}

    note_transaction.note_count=(uint16_t)count;note_history_capture(note_transaction.notes,count,-1);
    memcpy(note_transaction.steps,t->step,sizeof note_transaction.steps);
    note_transaction.track=(uint8_t)trk_index(t);note_transaction.pattern_gen=t->pattern_gen;
    note_transaction.cursor=ui.cursor;note_transaction.len=(uint8_t)step_pattern_len(t);
    note_transaction.valid=1;note_transaction.redo=note_transaction.clear=note_transaction.sound_clear=note_transaction.slen=note_transaction.pitch=0;return 1;
}
static int note_transaction_begin(void){return note_transaction_begin_track(TSEL);}
static void note_transaction_pack(void)
{
    if(note_transaction.note_count==RECORD_MAX){
        note_transaction.note_count=(uint16_t)note_history_pack(note_transaction.notes);
        resource_resize(RES_NOTE_HISTORY,__builtin_offsetof(note_transaction_t,notes)+note_history_bytes(note_transaction.note_count));
    }
}
static void note_transaction_end(void)
{
    note_transaction_pack();
    note_transaction.generation=recording_generation;note_transaction.signature=steps_sig(&trk[note_transaction.track]);
    step_history_clear();ui.force=1;
}
static void note_swap(void *a,void *b,uint32_t size)
{
    uint8_t *left=a,*right=b;
    while(size--){uint8_t value=*left;*left++=*right;*right++=value;}
}
static int note_tools_history(int redo)
{
    if(!resource[RES_NOTE_HISTORY].size || !note_transaction_ptr || !note_transaction.valid||note_transaction.redo!=(uint32_t)redo||
       (!note_transaction.clear&&note_transaction.track!=song.sel))return 0;
    track_t *t=&trk[note_transaction.track];
    if(note_transaction.pattern_gen!=t->pattern_gen||note_transaction.len!=step_pattern_len(t)||
       note_transaction.generation!=recording_generation||note_transaction.signature!=steps_sig(t)||
       (song.playing&&rec_on(t)))return 0;
    fm1_irq_off();
    if(note_transaction.sound_clear && (chain_busy() || !pattern_sound_reserve(trk_index(t)))) {
        fm1_irq_on();ui_message(chain_busy()?"STOP TO EDIT":"NO SOUND MEMORY");return 1;
    }
    note_transaction_t *grown=resource_resize(RES_NOTE_HISTORY,sizeof *grown);
    if(!grown){fm1_irq_on();ui_message("NO EDIT MEMORY");return 1;}
    note_transaction_ptr=grown;note_history_expand(note_transaction.notes,note_transaction.note_count);note_transaction.note_count=RECORD_MAX;
    if(note_transaction.clear){
        motion_store_t current;motion_snapshot_track(t,&current);
        if(motion_replace_track(t,&note_transaction.motion)){note_transaction_pack();fm1_irq_on();ui_message("MOTION FULL");return 1;}
        note_transaction.motion=current;
        if(note_transaction.sound_clear){
            uint8_t id=pattern_sound[trk_index(t)][t->pattern];
            pattern_sound[trk_index(t)][t->pattern]=note_transaction.pattern_sound;note_transaction.pattern_sound=id;
            if(song.playing)pattern_sound_switch(trk_index(t),t->pattern);
        }
    }
    recording_stop(t);
    note_swap(recording,note_transaction.notes,sizeof recording);
    note_swap(t->step,note_transaction.steps,sizeof note_transaction.steps);
    if(note_transaction.slen){uint8_t len=(uint8_t)t->p[P_SLEN];t->p[P_SLEN]=note_transaction.slen;note_transaction.slen=len;note_transaction.len=(uint8_t)step_pattern_len(t);}
    recording_reindex();uint8_t cursor=ui.cursor;if(t==TSEL)cursor_set(note_transaction.cursor);note_transaction.cursor=cursor;
    ui.note_pick=0;note_transaction.redo^=1u;note_transaction_end();fm1_irq_on();ui_message(redo?"REDO":"UNDO");return 1;
}
static int note_clear_history(int redo)
{
    return resource[RES_NOTE_HISTORY].size&&note_transaction_ptr&&note_transaction.clear&&note_tools_history(redo);
}
/* Precise pattern clears need the same bounded event snapshot as transforms.
 * Preserve motion with the clear so undo restores the complete pattern. */
static int note_clear_pattern(track_t *t)
{
    fm1_irq_off();recording_stop(t);
    if(!note_transaction_begin_track(t)){fm1_irq_on();return 0;}
    motion_snapshot_track(t,&note_transaction.motion);note_transaction.clear=1;
    note_transaction.pattern_sound=pattern_sound[trk_index(t)][t->pattern];
    track_defaults_steps(t);note_transaction_end();fm1_irq_on();undo_seal();return 1;
}
/* Convert an entire chord together so its original manual notes cannot double
 * trigger. Preflight capacity before changing any note or tie. */
static int note_convert(uint32_t start)
{
    if(start>=NSTEP||!step_on(&TSEL->step[start]))return 0;
    step_t source;memcpy(&source,&TSEL->step[start],sizeof source);if(source.flags&SF_RECORDED)return 1;
    uint8_t nn[12],vv[12];uint32_t count=0,free=0,n=step_note_length(TSEL,start);
    for(uint32_t j=0;j<source.n;j++){nn[count]=source.note[j];vv[count++]=(source.flags&SF_ACCENT)?127:source.vel?source.vel:96;}
    if(drum_track(TSEL))for(uint32_t j=0;j<NLANE;j++)if(step_lanes(&source)&(1u<<j)){nn[count]=DRUM_LANE_NOTE[j];vv[count++]=(step_accents(&source)&(1u<<j))?127:source.vel?source.vel:96;}
    for(uint32_t i=0;i<RECORD_MAX;i++)free+=!recording[i].vel;
    if(count>free){ui_message("RECORDING FULL");return 0;}
    step_delete(TSEL,start);memcpy(&TSEL->step[start],&source,sizeof source);TSEL->step[start].flags|=SF_RECORDED;
    for(uint32_t j=0,i=0;j<count;j++){
        while(recording[i].vel)i++;
        recorded_note_t r={0};recorded_set_note(&r,nn[j]);r.vel=vv[j];recorded_set_owner(&r,recording_owner(TSEL));recorded_set_step(&r,start,0,0);recorded_set_gate(&r,drum_track(TSEL));
        note_duration(&r,n*RECORD_UNIT);recording_restore_note(TSEL,i,r);i++;
    }
    cursor_set((int32_t)start);ui.note_pick=0;notes_selected(TSEL);return 1;
}
static void note_rebuild_all(void)
{
    for(uint32_t i=0;i<NSTEP;i++)if(TSEL->step[i].flags&SF_RECORDED)step_delete(TSEL,i);
    for(uint32_t i=recording_head[recording_owner(TSEL)];i<RECORD_MAX;i=recording_next[i]){
        if(recorded_step(&recording[i])>=step_pattern_len(TSEL))continue;
        uint32_t view=recording_view(TSEL,&recording[i]);TSEL->step[view].time=ST_NOTE;TSEL->step[view].flags|=SF_RECORDED;
    }
    for(uint32_t i=0;i<NSTEP;i++)if(TSEL->step[i].flags&SF_RECORDED)notes_rebuild(TSEL,i);
}
/* Manual fractional length: all chord members retain their common duration.
 * Playback frees the note history every block, so a playing edit has no
 * snapshot: the conversion preflights itself and later turns edit the event. */
static void note_manual_length(int32_t delta,uint32_t quantum)
{
    if(chain_busy()||live_rec_sel()){ui_message("STOP TO EDIT");return;}
    uint32_t at=notes_manual_start(TSEL);if(at>=NSTEP){ui_message("SELECT A NOTE");return;}
    uint32_t snapshot=!song.playing;
    fm1_irq_off();
    if(!snapshot)recording_prune(TSEL);
    else if(!note_transaction_begin()){fm1_irq_on();return;}
    if(!note_convert(at)){if(snapshot)note_transaction.valid=0;fm1_irq_on();return;}
    for(uint32_t i=recording_head[recording_owner(TSEL)];i<RECORD_MAX;i=recording_next[i])if(recording_view(TSEL,&recording[i])==at){/* (the converted notes, not a swung neighbour stored in this interval) */
        uint32_t duration=recording[i].duration*(1u<<(recorded_exponent(&recording[i])));
        note_duration(&recording[i],(uint32_t)clamp(note_snap((int32_t)duration,delta,quantum),(int32_t)quantum,128*RECORD_UNIT));
    }
    recording_generation++;ui.note_pick=0;notes_selected(TSEL);
    if(snapshot)note_transaction_end();else{step_history_clear();ui.force=1;}
    fm1_irq_on();
}
/* Fine moves and range transforms snap to quarter-step boundaries. A range's
 * offsets stay intact; copy shifts a duplicate. */
static int bulk_note_has(uint32_t i) { return bulk.on==2u ? (bulk.notes[i/8u]>>(i%8u))&1u : bulk_has(recording_grid_step(TSEL,&recording[i])); }
static void notes_bulk_delete(void)
{
    if(chain_busy()||live_rec_sel()){ui_message("STOP TO EDIT");return;}
    fm1_irq_off();
    if(!note_transaction_begin()){fm1_irq_on();return;}
    uint64_t touched=0;
    for(uint32_t i=recording_head[recording_owner(TSEL)];i<RECORD_MAX;){
        uint32_t next=recording_next[i];
        if(bulk_note_has(i)){touched|=(uint64_t)1u<<recording_view(TSEL,&recording[i]);recording_remove(TSEL,i);}
        i=next;
    }
    if(bulk.on==1u)for(uint32_t s=0;s<step_pattern_len(TSEL);s++)
        if(bulk_has(s)&&step_on(&TSEL->step[s])&&!(TSEL->step[s].flags&SF_RECORDED))step_delete(TSEL,s);
    for(uint32_t s=0;s<NSTEP;s++)if((touched>>s)&1u)notes_rebuild(TSEL,s);
    note_transaction_end();fm1_irq_on();
    bulk_clear();ui.note_pick=0;ui_message("NOTES DELETED");
}
static int note_transform_has(uint32_t i,uint32_t chosen)
{
    return recording_active(TSEL,i)&&(bulk.on?bulk_note_has(i):i==chosen);
}
/* Quantize each onset independently; a normal move preserves group offsets.
 * Without a range, quantization applies to the current pattern. */
static void note_transform(int32_t delta,uint32_t quantum,int mode)
{
    if(song.playing||chain_busy()||live_rec_sel()){ui_message("STOP TO EDIT");return;}
    int copy=mode==1,quantize=mode==2;
    uint32_t at=notes_manual_start(TSEL),chosen=notes_selected(TSEL),len=step_pattern_len(TSEL);
    if(!bulk.on&&at>=NSTEP&&chosen>=RECORD_MAX){ui_message("SELECT A NOTE");return;}
    fm1_irq_off();
    uint32_t needed=0,free=0,changed=0;
    for(uint32_t i=0;i<RECORD_MAX;i++){free+=!recording[i].vel;if(note_transform_has(i,chosen)){needed+=copy;changed|=recording[i].on&(quantum-1u);}}
    if(quantize&&!changed){fm1_irq_on();ui_message("ALREADY ON GRID");return;}
    if(!bulk.on&&chosen<RECORD_MAX)at=NSTEP;
    for(uint32_t i=0;i<len;i++)if((!(TSEL->step[i].flags&SF_RECORDED)&&step_on(&TSEL->step[i])&&(bulk.on?bulk.on==1u&&bulk_has(i):i==at))){uint32_t count=TSEL->step[i].n+(drum_track(TSEL)?__builtin_popcount(step_lanes(&TSEL->step[i])):0u);needed+=count*(copy?2u:1u);}
    if(needed>free){fm1_irq_on();ui_message("RECORDING FULL");return;}
    if(!note_transaction_begin()){fm1_irq_on();return;}
    for(uint32_t i=0;i<len;i++)if((!(TSEL->step[i].flags&SF_RECORDED)&&step_on(&TSEL->step[i])&&(bulk.on?bulk.on==1u&&bulk_has(i):i==at)))note_convert(i);
    if(!bulk.on&&chosen>=RECORD_MAX)chosen=notes_selected(TSEL);
    uint8_t selected[RECORD_MAX/8u];memset(selected,0,sizeof selected);
    uint32_t reference=RECORD_MAX;
    for(uint32_t i=recording_head[recording_owner(TSEL)];i<RECORD_MAX;i=recording_next[i])if(note_transform_has(i,chosen)){
        selected[i/8u]|=(uint8_t)(1u<<(i%8u));if(reference==RECORD_MAX || recorded_step(&recording[i])*RECORD_UNIT+recording[i].on<recorded_step(&recording[reference])*RECORD_UNIT+recording[reference].on)reference=i;
    }
    if(reference==RECORD_MAX){note_transaction.valid=0;fm1_irq_on();return;}
    /* Only probability and slide survive overview rebuilding. */
    uint16_t metadata[NSTEP];
    for(uint32_t k=0;k<len;k++)metadata[k]=TSEL->step[k].probability|((uint16_t)TSEL->step[k].flags<<8);
    int32_t position=recorded_step(&recording[reference])*RECORD_UNIT+recording[reference].on;
    int32_t shift=note_snap(position,delta,quantum)-position;
    /* A move never overwrites a separate manual note; restore the conversion
     * too if any destination is occupied. */
    for(uint32_t i=0;i<RECORD_MAX;i++)if(selected[i/8u]&(1u<<(i%8u))){recorded_note_t r;note_assign(&r,&recording[i]);note_onset(&r,shift,len,quantize?quantum:0);uint32_t view=recording_view(TSEL,&r);
        if(!(TSEL->step[view].flags&SF_RECORDED)){uint32_t source=recording_view(TSEL,&recording[i]);metadata[view]=TSEL->step[source].probability|((uint16_t)TSEL->step[source].flags<<8);}
        if(notes_manual_occupied(TSEL,view)){
        /* Restore a sparse snapshot directly: rollback never needs a larger
         * allocation, including when the working arena is now exhausted. */
        uint32_t n=0,count=note_transaction.note_count;const uint8_t *map=(const uint8_t *)note_transaction.notes+count*sizeof(recorded_note_t);
        for(uint32_t j=0;j<RECORD_MAX;j++){
            if(count==RECORD_MAX)recording[j]=note_transaction.notes[j];
            else if(map[j/8u]&(1u<<(j%8u)))recording[j]=note_transaction.notes[n++];
            else memset(&recording[j],0,sizeof recording[j]);
        }
        memcpy(TSEL->step,note_transaction.steps,sizeof note_transaction.steps);recording_reindex();cursor_set(note_transaction.cursor);ui.note_pick=0;note_transaction.valid=0;fm1_irq_on();ui_message("STEP OCCUPIED");return;}}
    uint32_t focus=RECORD_MAX;
    uint8_t moved[RECORD_MAX/8u];memset(moved,0,sizeof moved);
    for(uint32_t i=0;i<RECORD_MAX;i++)if(selected[i/8u]&(1u<<(i%8u))){recorded_note_t r=recording_snapshot(i);note_onset(&r,shift,len,quantize?quantum:0);uint32_t dest=i;
        if(copy){for(dest=0;dest<RECORD_MAX&&recording[dest].vel;dest++);}
        else recording_remove(TSEL,i);
        recording_restore_note(TSEL,dest,r);moved[dest/8u]|=(uint8_t)(1u<<(dest%8u));if(i==reference)focus=dest;
    }
    note_rebuild_all();
    for(uint32_t k=0;k<len;k++)if(TSEL->step[k].flags&SF_RECORDED){TSEL->step[k].probability=(uint8_t)metadata[k];TSEL->step[k].flags|=(metadata[k]>>8)&SF_SLIDE;}
    if(focus<RECORD_MAX){cursor_set(recording_grid_step(TSEL,&recording[focus]));ui.note_pick=(uint16_t)(focus+1u);note_assign(&ui.note_identity,&recording[focus]);ui.note_generation=recording_generation;}
    if(bulk.on){bulk.on=2;memcpy(bulk.notes,moved,sizeof moved);bulk.generation=recording_generation;}note_transaction_end();fm1_irq_on();ui_message(quantize?"NOTES QUANTIZED":copy?"Copying..":"Moving..");
}
static void note_move_snapped(int32_t delta,uint32_t quantum,int copy){note_transform(delta,quantum,copy);}
static void note_quantize(uint32_t quantum)
{
    uint32_t selected=bulk.on;
    if(!selected){bulk.on=1;bulk.anchor=0;bulk.end=(uint16_t)(step_pattern_len(TSEL)-1u);}
    note_transform(0,quantum,2);
    if(!selected)bulk.on=0;
}
static void notes_pitch_seal(void);
static void note_move_arm(void) { notes_pitch_seal();ui.note_move=1;ui.note_copy=0;ui.force=1;ui_message("Moving.."); }
static void note_copy_arm(void) { note_move_arm();ui.note_copy=1;ui_message("Copying.."); }
/* PITCH on a range, or on a note being moved / copied: every selected note moves together and the turn stops where
 * the first one meets its edge, so the intervals survive. Copy armed: the copy is made in place first and takes the
 * pitch. One snapshot covers the copy and a run of turns (one undo); a new range or move seals it. Playing frees the
 * note history every block, so a playing shift has no undo (as note_manual_length). Drum events move their pitch.
 * The preview plays the selection only when it sits on one step: spread notes would sound as a chord. */
static void notes_pitch_seal(void) { if(resource[RES_NOTE_HISTORY].size&&note_transaction_ptr)note_transaction.pitch=0; }
static int notes_pitch_run(void)
{
    return resource[RES_NOTE_HISTORY].size&&note_transaction_ptr&&note_transaction.valid&&note_transaction.pitch&&!note_transaction.redo&&
        note_transaction.track==song.sel&&note_transaction.pattern_gen==TSEL->pattern_gen&&note_transaction.len==step_pattern_len(TSEL)&&
        note_transaction.generation==recording_generation&&note_transaction.signature==steps_sig(TSEL);
}
static int notes_bulk_manual(uint32_t s)
{
    return bulk.on==1u&&bulk_has(s)&&step_on(&TSEL->step[s])&&!(TSEL->step[s].flags&SF_RECORDED);
}
static void notes_range_pitch(int32_t delta)
{
    if(ui.note_copy){
        uint32_t gen=recording_generation;note_move_snapped(0,RECORD_UNIT/4u,1);
        if(gen==recording_generation)return;
        ui.note_copy=0;note_transaction.pitch=1;
    }
    uint32_t len=step_pattern_len(TSEL),drum=drum_track(TSEL),chosen=notes_selected(TSEL),found=0;
    int32_t low=-128,high=128;
    fm1_irq_off();
    for(uint32_t i=recording_head[recording_owner(TSEL)];i<RECORD_MAX;i=recording_next[i])if(note_transform_has(i,chosen)){
        int32_t v=drum?recording[i].pitch:(int32_t)recorded_note(&recording[i]),lo=(drum?-24:1)-v,hi=(drum?24:127)-v;
        if(lo>low)low=lo;if(hi<high)high=hi;found=1;
    }
    for(uint32_t s=0;s<len;s++)if(notes_bulk_manual(s))for(uint32_t j=0;j<TSEL->step[s].n;j++){
        int32_t v=TSEL->step[s].note[j];if(1-v>low)low=1-v;if(127-v<high)high=127-v;found=1;
    }
    delta=found?clamp(delta,low,high):0;
    if(!delta){fm1_irq_on();return;}
    uint32_t snapshot=!song.playing;
    if(snapshot&&!notes_pitch_run()){if(!note_transaction_begin()){fm1_irq_on();return;}note_transaction.pitch=1;}
    uint8_t selected[RECORD_MAX/8u];memset(selected,0,sizeof selected);
    for(uint32_t i=recording_head[recording_owner(TSEL)];i<RECORD_MAX;i=recording_next[i])
        if(note_transform_has(i,chosen))selected[i/8u]|=(uint8_t)(1u<<(i%8u));
    uint8_t nn[12],vv[12];uint32_t count=0,view=NSTEP,close=1;int32_t pitch=0;
    uint64_t touched=0;
    for(uint32_t i=0;i<RECORD_MAX;i++)if(selected[i/8u]&(1u<<(i%8u))){
        recorded_note_t r=recording_snapshot(i);
        if(drum)r.pitch=(int8_t)(r.pitch+delta);else recorded_set_note(&r,(uint8_t)((int32_t)recorded_note(&r)+delta));
        uint32_t at=recording_view(TSEL,&r);touched|=(uint64_t)1u<<at;recording_remove(TSEL,i);recording_restore_note(TSEL,i,r);
        if(view==NSTEP){view=at;pitch=r.pitch;}
        if(at!=view||count==12u)close=0;else{nn[count]=recorded_note(&r);vv[count++]=r.vel;}
    }
    for(uint32_t s=0;s<NSTEP;s++)if((touched>>s)&1u)notes_rebuild(TSEL,s);
    for(uint32_t s=0;s<len;s++)if(notes_bulk_manual(s)){
        step_t *st=&TSEL->step[s];if(view==NSTEP)view=s;
        for(uint32_t j=0;j<st->n;j++){
            st->note[j]=(uint8_t)(st->note[j]+delta);
            if(s!=view||count==12u)close=0;else{nn[count]=st->note[j];vv[count++]=st->vel?st->vel:96;}
        }
    }
    if(bulk.on==2u)bulk.generation=recording_generation;
    if(chosen<RECORD_MAX&&recording[chosen].vel){note_assign(&ui.note_identity,&recording[chosen]);ui.note_generation=recording_generation;}
    if(snapshot)note_transaction_end();else{step_history_clear();ui.force=1;}
    fm1_irq_on();
    if(close&&count&&notes_preview_on())notes_audition(nn,vv,count,drum?pitch:0,0);
}
