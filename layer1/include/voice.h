#pragma once
/*
 * SHMC Layer 1 — Voice DSL
 *
 * A Voice is an ordered sequence of VoiceInstr (instructions).
 * Instructions: NOTE, REST, TIE, GLIDE, REPEAT{...}
 *
 * Compilation:
 *   voice_compile()  →  EventStream  (sorted timed note-on/off events)
 *
 * The EventStream feeds into the Layer 0 Patch engine via voice_render().
 *
 * Pitch domain : MIDI 0-127
 * Duration     : index into DUR_TABLE  {1/64 1/32 1/16 1/8 1/4 1/2 1} beat
 * Velocity     : index into VEL_TABLE  {8 steps, 0.125..1.0}
 */

#include <stdint.h>
#include "../../layer0/include/patch.h"   /* PatchProgram, Patch, tables */

/* ---- Limits ---- */
#define VOICE_MAX_INSTRS  4096
#define VOICE_MAX_EVENTS  8192
#define VOICE_MAX_REPEAT   8    /* nesting depth */

/* ---- Duration table index (7 values) ---- */
#define DUR_1_64  0
#define DUR_1_32  1
#define DUR_1_16  2
#define DUR_1_8   3
#define DUR_1_4   4
#define DUR_1_2   5
#define DUR_1     6

/* ---- Velocity table (8 steps, pppp..ffff) ---- */
#define VEL_PPPP  0
#define VEL_PPP   1
#define VEL_PP    2
#define VEL_P     3
#define VEL_MP    4
#define VEL_MF    5
#define VEL_F     6
#define VEL_FF    7

extern const float VEL_TABLE[8];  /* 0.125, 0.25, ... 1.0 */

/* ---- VoiceInstr opcodes ---- */
typedef enum {
    VI_NOTE  = 0,   /* play pitch for duration           */
    VI_REST,        /* silence for duration              */
    VI_TIE,         /* extend previous note's duration   */
    VI_GLIDE,       /* portamento to new pitch           */
    VI_REPEAT_BEGIN,/* begin repeat block                */
    VI_REPEAT_END,  /* end repeat block (n times)        */
    VI_COUNT
} VIOp;

/* ---- Packed instruction (fits in 32 bits) ----
   [31:24] opcode  (8b)
   [23:16] pitch   (8b)  MIDI 0-127 / unused
   [15: 8] dur_idx (8b)  index into g_dur
   [ 7: 0] vel_idx (8b)  index into VEL_TABLE / repeat count
*/
typedef uint32_t VInstr;

#define VI_PACK(op,pitch,dur,vel) \
    (((uint32_t)(uint8_t)(op)<<24)|((uint32_t)(uint8_t)(pitch)<<16)| \
     ((uint32_t)(uint8_t)(dur)<<8)|((uint32_t)(uint8_t)(vel)))

#define VI_OP(i)    ((uint8_t)((i)>>24))
#define VI_PITCH(i) ((uint8_t)((i)>>16))
#define VI_DUR(i)   ((uint8_t)((i)>>8))
#define VI_VEL(i)   ((uint8_t)(i))

/* ---- VoiceProgram ---- */
typedef struct {
    VInstr code[VOICE_MAX_INSTRS];
    int    n;
} VoiceProgram;

/* ---- Event types ---- */
typedef enum { EV_NOTE_ON=0, EV_NOTE_OFF } EvType;

/* ---- Event: one note-on or note-off at a beat-time ---- */
typedef struct {
    float   beat;       /* time in beats from start  */
    EvType  type;
    uint8_t pitch;
    float   velocity;   /* 0..1                      */
} Event;

/* ---- EventStream: sorted list of Events ---- */
typedef struct {
    Event events[VOICE_MAX_EVENTS];
    int   n;
    float total_beats;  /* total duration of the voice */
} EventStream;

/* ---- VoiceRenderer: stateful playback of EventStream via Patch ---- */
typedef struct {
    const EventStream  *es;
    const PatchProgram *patch_prog;
    float               bpm;
    float               sr;
    float               beat_time;    /* current position in beats  */
    float               sample_time;  /* current position in seconds */
    int                 ev_cursor;    /* next event to process       */
    Patch               active;       /* currently playing patch     */
    int                 has_active;
    int                 done;
} VoiceRenderer;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Compilation ---- */
/* Compile VoiceProgram to EventStream.
   bpm: beats per minute (used only to check for empty).
   Returns 0 on success, -1 on overflow. */
int voice_compile(const VoiceProgram *vp, EventStream *es);

/* ---- Rendering ---- */
/* Initialize renderer.  Call before voice_render_block(). */
void voice_renderer_init(VoiceRenderer *vr,
                         const EventStream  *es,
                         const PatchProgram *patch,
                         float bpm, float sr);

/* Render one block of n_samples into out[].
   Mixes patch audio with proper note-on/off scheduling.
   Returns 0 while playing, 1 when done. */
int voice_render_block(VoiceRenderer *vr, float *out, int n_samples);

/* ---- VoiceProgram builder (inline assembler) ---- */
typedef struct {
    VoiceProgram vp;
    int          repeat_stack[VOICE_MAX_REPEAT]; /* stack of REPEAT_BEGIN indices */
    int          rsp;                             /* stack pointer */
    int          ok;
} VoiceBuilder;

static inline void vb_init(VoiceBuilder *b){
    b->vp.n=0; b->rsp=0; b->ok=0;
}
static inline void vb_emit(VoiceBuilder *b, VInstr vi){
    if(b->vp.n>=VOICE_MAX_INSTRS){b->ok=-1;return;}
    b->vp.code[b->vp.n++]=vi;
}
static inline void vb_note(VoiceBuilder *b, int pitch, int dur, int vel){
    vb_emit(b,VI_PACK(VI_NOTE,pitch,dur,vel));
}
static inline void vb_rest(VoiceBuilder *b, int dur){
    vb_emit(b,VI_PACK(VI_REST,0,dur,0));
}
static inline void vb_tie(VoiceBuilder *b, int dur){
    vb_emit(b,VI_PACK(VI_TIE,0,dur,0));
}
static inline void vb_glide(VoiceBuilder *b, int pitch, int dur, int vel){
    vb_emit(b,VI_PACK(VI_GLIDE,pitch,dur,vel));
}
static inline void vb_repeat_begin(VoiceBuilder *b){
    if(b->rsp>=VOICE_MAX_REPEAT){b->ok=-1;return;}
    b->repeat_stack[b->rsp++]=b->vp.n;
    vb_emit(b,VI_PACK(VI_REPEAT_BEGIN,0,0,0)); /* placeholder */
}
static inline void vb_repeat_end(VoiceBuilder *b, int n){
    if(b->rsp<=0){b->ok=-1;return;}
    b->rsp--;
    vb_emit(b,VI_PACK(VI_REPEAT_END,0,0,(uint8_t)n));
}
static inline VoiceProgram *vb_finish(VoiceBuilder *b){ return &b->vp; }

#ifdef __cplusplus
}
#endif
