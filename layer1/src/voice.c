#include "../include/voice.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---- Velocity table: 8 steps pppp..ff ---- */
const float VEL_TABLE[8] = {
    0.125f, 0.250f, 0.375f, 0.500f,
    0.625f, 0.750f, 0.875f, 1.000f
};

/* ---- Emit an event (sorted insert is not needed: we build in order) ---- */
static int ev_push(EventStream *es, float beat, EvType type,
                   uint8_t pitch, float vel){
    if(es->n >= VOICE_MAX_EVENTS) return -1;
    Event *e = &es->events[es->n++];
    e->beat     = beat;
    e->type     = type;
    e->pitch    = pitch;
    e->velocity = vel;
    return 0;
}

/* ============================================================
   voice_compile
   Walks the VoiceProgram, expanding REPEAT blocks inline.
   Produces a flat event list in chronological order.
   ============================================================ */

/* Recursive helper that processes instrs[lo..hi) and advances *beat */
static int compile_range(const VInstr *code, int lo, int hi,
                          EventStream *es, float *beat){
    extern const float g_dur[7];
    int i = lo;
    while(i < hi){
        VInstr vi = code[i];
        uint8_t op    = VI_OP(vi);
        uint8_t pitch = VI_PITCH(vi);
        uint8_t di    = VI_DUR(vi);
        uint8_t veli  = VI_VEL(vi);

        float dur_beats = (di < 7) ? g_dur[di] : g_dur[4]; /* default 1/4 */
        float vel       = (veli < 8) ? VEL_TABLE[veli] : 0.75f;

        switch(op){
        case VI_NOTE:
            if(ev_push(es,*beat,EV_NOTE_ON, pitch,vel)<0) return -1;
            if(ev_push(es,*beat+dur_beats,EV_NOTE_OFF,pitch,vel)<0) return -1;
            *beat += dur_beats;
            break;

        case VI_REST:
            *beat += dur_beats;
            break;

        case VI_TIE:
            /* Extend last NOTE_OFF by dur_beats */
            for(int k=es->n-1;k>=0;k--){
                if(es->events[k].type==EV_NOTE_OFF){
                    es->events[k].beat += dur_beats;
                    break;
                }
            }
            *beat += dur_beats;
            break;

        case VI_GLIDE:
            /* Glide: note-on at new pitch immediately (no note-off between) */
            if(ev_push(es,*beat,EV_NOTE_ON, pitch,vel)<0) return -1;
            if(ev_push(es,*beat+dur_beats,EV_NOTE_OFF,pitch,vel)<0) return -1;
            *beat += dur_beats;
            break;

        case VI_REPEAT_BEGIN:
            /* Find matching REPEAT_END, track nesting */
            {
                int begin_i = i;
                int depth   = 1;
                int end_i   = -1;
                for(int j=i+1;j<hi;j++){
                    uint8_t jop = VI_OP(code[j]);
                    if(jop==VI_REPEAT_BEGIN) depth++;
                    else if(jop==VI_REPEAT_END){
                        depth--;
                        if(depth==0){ end_i=j; break; }
                    }
                }
                if(end_i<0) return -1; /* unmatched */
                int count = (int)VI_VEL(code[end_i]);
                if(count<1) count=1;
                for(int r=0;r<count;r++){
                    if(compile_range(code, begin_i+1, end_i, es, beat)<0)
                        return -1;
                }
                i = end_i; /* skip to end, outer loop does i++ */
            }
            break;

        case VI_REPEAT_END:
            /* Only reached when not inside a BEGIN scan — skip */
            break;

        default:
            break;
        }
        i++;
    }
    return 0;
}

int voice_compile(const VoiceProgram *vp, EventStream *es){
    memset(es,0,sizeof(*es));
    float beat=0.0f;
    int r = compile_range(vp->code, 0, vp->n, es, &beat);
    es->total_beats = beat;
    return r;
}

/* ============================================================
   VoiceRenderer
   Converts the event stream to audio by driving a Patch engine.
   Handles note-on/off scheduling within each audio block.
   ============================================================ */

void voice_renderer_init(VoiceRenderer *vr,
                         const EventStream  *es,
                         const PatchProgram *patch,
                         float bpm, float sr){
    memset(vr,0,sizeof(*vr));
    vr->es          = es;
    vr->patch_prog  = patch;
    vr->bpm         = bpm;
    vr->sr          = sr;
    vr->beat_time   = 0.0f;
    vr->sample_time = 0.0f;
    vr->ev_cursor   = 0;
    vr->has_active  = 0;
    vr->done        = 0;
}

/*
 * Render n_samples into out[].
 * Returns 0 while still playing, 1 when all events are done
 * and the last note has released.
 */
int voice_render_block(VoiceRenderer *vr, float *out, int n_samples){
    if(vr->done){ memset(out,0,n_samples*sizeof(float)); return 1; }

    float secs_per_beat = 60.0f / vr->bpm;
    float dt            = 1.0f  / vr->sr;

    memset(out,0,n_samples*sizeof(float));

    for(int s=0;s<n_samples;s++){
        float cur_beat = vr->beat_time;

        /* Process all events at or before cur_beat */
        while(vr->ev_cursor < vr->es->n){
            const Event *ev = &vr->es->events[vr->ev_cursor];
            if(ev->beat > cur_beat) break;

            if(ev->type == EV_NOTE_ON){
                patch_note_on(&vr->active, vr->patch_prog,
                              vr->sr, (int)ev->pitch, ev->velocity);
                vr->has_active = 1;
            } else { /* EV_NOTE_OFF */
                /* Trigger release stage in ADSR by setting stage=3 in state.
                   Each ADSR instruction owns state slots at (instr_idx*4).
                   We scan the program for OP_ADSR and set state[sb+0]=3. */
                if(vr->has_active){
                    const PatchProgram *pp = vr->patch_prog;
                    for(int k=0;k<pp->n_instrs;k++){
                        if(((pp->code[k]>>56)&0xFF)==0x1C){ /* OP_ADSR=28=0x1C */
                            int sb=(k*4)%MAX_STATE;
                            vr->active.st.state[sb+0]=3.0f; /* release */
                            vr->active.st.state[sb+2]=0.0f; /* reset timer */
                        }
                    }
                }
            }
            vr->ev_cursor++;
        }

        /* Synthesize one sample from active patch */
        float samp=0.0f;
        if(vr->has_active){
            patch_step(&vr->active,&samp,1);
        }
        out[s] = samp;

        /* Advance time */
        vr->sample_time += dt;
        vr->beat_time    = vr->sample_time / secs_per_beat;
    }

    /* Check done: all events processed and last note is silent */
    if(vr->ev_cursor >= vr->es->n){
        int all_silent = !vr->has_active;
        if(vr->has_active){
            /* Check if patch output is near-zero (tail ended) */
            float probe=0.0f;
            patch_step(&vr->active,&probe,1);
            if(fabsf(probe)<1e-5f) all_silent=1;
        }
        if(all_silent){ vr->done=1; return 1; }
    }
    return 0;
}
