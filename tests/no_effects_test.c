/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Real mixer, input, project and protocol paths must ignore legacy FX. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
static uint64_t render_hash(int legacy)
{
    int fds[2];assert(pipe(fds)==0);pid_t pid=fork();assert(pid>=0);
    if(!pid){
        close(fds[0]);ui_power_on();set_engine_of(TSEL,ENGI_FM6);apply_preset_to(TSEL,8);
        if(legacy){
            for(uint32_t p=0;p<NPART;p++)for(uint32_t i=0;i<P_COUNT;i++)if(param_reserved(i))trk[p].p[i]=127;
            for(uint32_t i=0;i<G_COUNT;i++)if(global_reserved(i))song.g[i]=127;
            settings.lowcut=2;favorites.factory[15][30]|=1u;
        }
        uint64_t h=1469598103934665603ull;int32_t out[CTL*2];
        mix_block(out,CTL);trk_note_on(TSEL,60,100);mix_block(out,CTL);uint32_t used=resource_used();
        for(uint32_t k=0;k<1024;k++){
            mix_block(out,CTL);for(uint32_t i=0;i<CTL*2;i++)h=(h^(uint32_t)out[i])*1099511628211ull;
            assert(resource_used()==used);
        }
        assert(write(fds[1],&h,sizeof h)==sizeof h);_exit(0);
    }
    close(fds[1]);uint64_t h;assert(read(fds[0],&h,sizeof h)==sizeof h);close(fds[0]);int status;
    assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && !WEXITSTATUS(status));return h;
}
int main(void)
{
    assert(render_hash(0)==render_hash(1));
    ui_power_on();project_t old;project_capture(&old);
    for(uint32_t k=0;k<NPART;k++){
        old.t[k].p[P_RESERVED33]=127;old.t[k].p[P_RESERVED45]=2;
        old.t[k].step[0]=(step_t){{60,64,67},3,ST_NOTE,0,100};
    }
    for(uint32_t i=0;i<G_COUNT;i++)if(global_reserved(i))old.g[i]=127;
    old.motion.count=1;old.motion.event[0]=(motion_event_t){0,P_RESERVED36,127};
    old.sum=proj_sum(&old);assert(proj_ok(&old));assert(!project_restore_runtime(&old));
    for(uint32_t k=0;k<NPART;k++){
        assert(trk[k].step[0].n==3 && trk[k].step[0].note[2]==67);
        for(uint32_t i=0;i<P_COUNT;i++)if(param_reserved(i))assert(trk[k].p[i]==0);
    }
    for(uint32_t i=0;i<G_COUNT;i++)if(global_reserved(i))assert(!song.g[i]);
    assert(motion_valid(&old.motion));assert(!motion_capture(TSEL,P_RESERVED36,100));
    uint32_t used=resource_used();midi_event(0xb0,0,91,127);midi_event(0xb0,0,93,127);
    assert(!TSEL->p[P_RESERVED34] && !TSEL->p[P_RESERVED36] && used==resource_used());
    for(uint32_t i=0;i<NPAGES;i++)assert(PG(i).fam<FAM_COUNT && !str_eq(PG(i).title,"SLICER"));
    ui_power_on();press(B_FX);assert(cur_page()->fam==FAM_FX);
    btn_down(B_FX);frames(800);assert(ui.layer==LAYER_FX);
    key_down(white(0));frame();assert(!gates() && kb_layer);key_up(white(0));btn_up(B_FX);frames(100);
    assert(!ui.layer);
    puts("No effects: old values/automation silent, notes survive, no legacy FX allocations or controls; legacy controls stay inert alongside master audio punch: OK");
    return 0;
}
