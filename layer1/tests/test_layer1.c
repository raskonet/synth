/*
 * SHMC Layer 1 — Voice DSL Integration Test
 *
 * Build:
 *   gcc -O2 -Ilayer1/include -Ilayer0/include \
 *       layer1/tests/test_layer1.c \
 *       layer1/src/voice.c \
 *       layer0/src/patch_interp.c \
 *       layer0/src/tables.c \
 *       -lm -o test_layer1
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "voice.h"
#include "../../layer0/include/patch_builder.h"

#define SR      44100
#define BLK     512

/* ---- WAV writer ---- */
static void write_wav(const char *path, const float *b, int n){
    FILE *f=fopen(path,"wb"); if(!f){perror(path);return;}
    int16_t *p=(int16_t*)malloc(n*2);
    for(int i=0;i<n;i++){float v=b[i];if(v>1)v=1;if(v<-1)v=-1;p[i]=(int16_t)(v*32767.f);}
    uint32_t ds=(uint32_t)n*2,rs=36+ds;
    fwrite("RIFF",1,4,f);fwrite(&rs,4,1,f);fwrite("WAVEfmt ",1,8,f);
    uint32_t cs=16;fwrite(&cs,4,1,f);
    uint16_t af=1,ch=1;fwrite(&af,2,1,f);fwrite(&ch,2,1,f);
    uint32_t sr=SR,br=SR*2;fwrite(&sr,4,1,f);fwrite(&br,4,1,f);
    uint16_t ba=2,bi=16;fwrite(&ba,2,1,f);fwrite(&bi,2,1,f);
    fwrite("data",1,4,f);fwrite(&ds,4,1,f);fwrite(p,2,n,f);
    fclose(f);free(p);
    printf("  wrote %s\n",path);
}

/* ---- Render EventStream to float buffer ---- */
static float *render_voice(const EventStream *es, const PatchProgram *patch,
                             float bpm, int *out_n){
    int cap = (int)(SR * (es->total_beats * 60.0f / bpm + 2.0f)); /* +2s tail */
    float *buf=(float*)calloc(cap,sizeof(float));
    VoiceRenderer vr;
    voice_renderer_init(&vr,es,patch,bpm,(float)SR);
    float blk[BLK]; int pos=0;
    while(!vr.done && pos<cap){
        int chunk=cap-pos<BLK?cap-pos:BLK;
        voice_render_block(&vr,blk,chunk);
        memcpy(buf+pos,blk,chunk*sizeof(float));
        pos+=chunk;
    }
    *out_n=pos;
    return buf;
}

/* ---- Patches ---- */

static PatchProgram patch_piano(void){
    /* Bright FM + fast decay — "piano-like" */
    PatchBuilder b; pb_init(&b);
    int two=pb_const_f(&b,2.0f);
    int mod=pb_osc(&b,two);
    int car=pb_fm(&b,REG_ONE,mod,15);
    int env=pb_adsr(&b,0,14,8,10);
    pb_out(&b,pb_mul(&b,car,env));
    return *pb_finish(&b);
}

static PatchProgram patch_bass(void){
    /* Sawtooth + LP filter */
    PatchBuilder b; pb_init(&b);
    int saw=pb_saw(&b,REG_ONE);
    int flt=pb_lpf(&b,saw,28);
    int env=pb_adsr(&b,0,8,20,8);
    pb_out(&b,pb_mul(&b,flt,env));
    return *pb_finish(&b);
}

static PatchProgram patch_lead(void){
    /* Triangle + tanh saturation */
    PatchBuilder b; pb_init(&b);
    int tr=pb_tri(&b,REG_ONE);
    int gn=pb_const_f(&b,3.0f);
    int dr=pb_mul(&b,tr,gn);
    int st=pb_tanh(&b,dr);
    int env=pb_adsr(&b,1,10,22,12);
    pb_out(&b,pb_mul(&b,st,env));
    return *pb_finish(&b);
}

static PatchProgram patch_pad(void){
    PatchBuilder b; pb_init(&b);
    int o1=pb_osc(&b,REG_ONE);
    int dt=pb_const_f(&b,1.008f);
    int o2=pb_osc(&b,dt);
    int mx=pb_mix(&b,o1,o2,15,15);
    int fl=pb_lpf(&b,mx,42);
    int en=pb_adsr(&b,14,4,28,20);
    pb_out(&b,pb_mul(&b,fl,en));
    return *pb_finish(&b);
}

/* ====================================================================
   Test 1: C major scale (8 quarter notes)
   ==================================================================== */
static void test_scale(void){
    printf("[test_scale] C major scale, quarter notes\n");
    int scale[]={60,62,64,65,67,69,71,72};
    VoiceBuilder vb; vb_init(&vb);
    for(int i=0;i<8;i++) vb_note(&vb,scale[i],DUR_1_4,VEL_MF);

    EventStream es;
    if(voice_compile(vb_finish(&vb),&es)<0){printf("  FAIL compile\n");return;}
    printf("  events=%d  total_beats=%.2f\n",es.n,es.total_beats);

    PatchProgram pa=patch_piano();
    int n; float *buf=render_voice(&es,&pa,120.0f,&n);
    write_wav("/mnt/user-data/outputs/v1_scale.wav",buf,n);
    free(buf);
    printf("  PASS\n\n");
}

/* ====================================================================
   Test 2: Repeated motif (Alberti bass figure, 4x)
   C E G E  x4
   ==================================================================== */
static void test_repeat(void){
    printf("[test_repeat] Alberti bass figure x4\n");
    VoiceBuilder vb; vb_init(&vb);
    vb_repeat_begin(&vb);
        vb_note(&vb,48,DUR_1_8,VEL_MP);   /* C3 */
        vb_note(&vb,52,DUR_1_8,VEL_MP);   /* E3 */
        vb_note(&vb,55,DUR_1_8,VEL_MP);   /* G3 */
        vb_note(&vb,52,DUR_1_8,VEL_MP);   /* E3 */
    vb_repeat_end(&vb,4);

    EventStream es;
    if(voice_compile(vb_finish(&vb),&es)<0){printf("  FAIL compile\n");return;}
    printf("  events=%d  total_beats=%.2f\n",es.n,es.total_beats);

    PatchProgram pa=patch_bass();
    int n; float *buf=render_voice(&es,&pa,120.0f,&n);
    write_wav("/mnt/user-data/outputs/v1_repeat.wav",buf,n);
    free(buf);
    printf("  PASS\n\n");
}

/* ====================================================================
   Test 3: Rests + ties  (dotted quarter = quarter + tie-eighth)
   ==================================================================== */
static void test_rest_tie(void){
    printf("[test_rest_tie] Rests and ties\n");
    VoiceBuilder vb; vb_init(&vb);
    vb_note(&vb,60,DUR_1_4,VEL_F);       /* C4 quarter */
    vb_tie( &vb,DUR_1_8);                 /* + eighth = dotted quarter */
    vb_rest(&vb,DUR_1_8);                 /* eighth rest */
    vb_note(&vb,64,DUR_1_4,VEL_MF);      /* E4 quarter */
    vb_rest(&vb,DUR_1_4);                 /* quarter rest */
    vb_note(&vb,67,DUR_1_2,VEL_P);       /* G4 half */

    EventStream es;
    if(voice_compile(vb_finish(&vb),&es)<0){printf("  FAIL compile\n");return;}
    printf("  events=%d  total_beats=%.2f\n",es.n,es.total_beats);

    PatchProgram pa=patch_lead();
    int n; float *buf=render_voice(&es,&pa,100.0f,&n);
    write_wav("/mnt/user-data/outputs/v1_rest_tie.wav",buf,n);
    free(buf);
    printf("  PASS\n\n");
}

/* ====================================================================
   Test 4: Nested repeats  (phrase repeated 3x, inner figure 2x)
   ==================================================================== */
static void test_nested_repeat(void){
    printf("[test_nested_repeat] Nested REPEAT blocks\n");
    VoiceBuilder vb; vb_init(&vb);
    vb_repeat_begin(&vb);                       /* outer x3 */
        vb_note(&vb,60,DUR_1_4,VEL_MP);         /* C4 */
        vb_repeat_begin(&vb);                   /* inner x2 */
            vb_note(&vb,64,DUR_1_8,VEL_MP);     /* E4 */
            vb_note(&vb,62,DUR_1_8,VEL_MP);     /* D4 */
        vb_repeat_end(&vb,2);
        vb_note(&vb,60,DUR_1_4,VEL_MF);         /* C4 */
    vb_repeat_end(&vb,3);

    EventStream es;
    if(voice_compile(vb_finish(&vb),&es)<0){printf("  FAIL compile\n");return;}
    printf("  events=%d  total_beats=%.2f\n",es.n,es.total_beats);

    PatchProgram pa=patch_piano();
    int n; float *buf=render_voice(&es,&pa,130.0f,&n);
    write_wav("/mnt/user-data/outputs/v1_nested.wav",buf,n);
    free(buf);
    printf("  PASS\n\n");
}

/* ====================================================================
   Test 5: Glide (portamento feel via rapid note succession)
   chromatic rise
   ==================================================================== */
static void test_glide(void){
    printf("[test_glide] Glide / chromatic slide\n");
    VoiceBuilder vb; vb_init(&vb);
    /* Rapid 1/16 note chromatic rise then a held top note */
    for(int p=55;p<=67;p++)
        vb_glide(&vb,p,DUR_1_16,VEL_MF);
    vb_note(&vb,67,DUR_1_2,VEL_F);

    EventStream es;
    if(voice_compile(vb_finish(&vb),&es)<0){printf("  FAIL compile\n");return;}
    printf("  events=%d  total_beats=%.2f\n",es.n,es.total_beats);

    PatchProgram pa=patch_lead();
    int n; float *buf=render_voice(&es,&pa,100.0f,&n);
    write_wav("/mnt/user-data/outputs/v1_glide.wav",buf,n);
    free(buf);
    printf("  PASS\n\n");
}

/* ====================================================================
   Test 6: Full melody — Twinkle Twinkle first phrase
   ==================================================================== */
static void test_melody(void){
    printf("[test_melody] Twinkle Twinkle first phrase\n");
    /* C C G G A A G-  F F E E D D C- */
    int melody[]= {60,60,67,67,69,69,67, 65,65,64,64,62,62,60};
    int durs[]  = {4, 4, 4, 4, 4, 4, 2,  4, 4, 4, 4, 4, 4, 2};
    /* durs: 4=quarter(DUR_1_4), 2=half(DUR_1_2) */

    VoiceBuilder vb; vb_init(&vb);
    for(int i=0;i<14;i++){
        int d = (durs[i]==4)?DUR_1_4:DUR_1_2;
        vb_note(&vb,melody[i],d,VEL_MF);
    }

    EventStream es;
    if(voice_compile(vb_finish(&vb),&es)<0){printf("  FAIL compile\n");return;}
    printf("  events=%d  total_beats=%.2f\n",es.n,es.total_beats);

    PatchProgram pa=patch_pad();
    int n; float *buf=render_voice(&es,&pa,110.0f,&n);
    write_wav("/mnt/user-data/outputs/v1_melody.wav",buf,n);
    free(buf);
    printf("  PASS\n\n");
}

/* ====================================================================
   Test 7: Verify EventStream structure (unit test, no audio)
   ==================================================================== */
static void test_compile_structure(void){
    printf("[test_compile_structure] EventStream layout verification\n");
    VoiceBuilder vb; vb_init(&vb);
    vb_note(&vb,60,DUR_1_4,VEL_MF);
    vb_rest(&vb,DUR_1_8);
    vb_note(&vb,64,DUR_1_4,VEL_F);

    EventStream es;
    voice_compile(vb_finish(&vb),&es);

    /* Expected events:
       beat 0.00 : NOTE_ON  C4
       beat 0.25 : NOTE_OFF C4
       beat 0.375: NOTE_ON  E4   (after 1/8 rest = 0.125 beats)
       beat 0.625: NOTE_OFF E4
    */
    int pass=1;
    if(es.n!=4){ printf("  FAIL: expected 4 events, got %d\n",es.n); pass=0; }
    else {
        float eps=1e-4f;
        float b0=es.events[0].beat, b1=es.events[1].beat;
        float b2=es.events[2].beat, b3=es.events[3].beat;
        if(fabsf(b0-0.000f)>eps||es.events[0].type!=EV_NOTE_ON) {printf("  FAIL ev0\n");pass=0;}
        if(fabsf(b1-0.250f)>eps||es.events[1].type!=EV_NOTE_OFF){printf("  FAIL ev1\n");pass=0;}
        if(fabsf(b2-0.375f)>eps||es.events[2].type!=EV_NOTE_ON) {printf("  FAIL ev2\n");pass=0;}
        if(fabsf(b3-0.625f)>eps||es.events[3].type!=EV_NOTE_OFF){printf("  FAIL ev3\n");pass=0;}
        if(pass) printf("  events: %.3f ON  %.3f OFF  %.3f ON  %.3f OFF  ✓\n",b0,b1,b2,b3);
    }
    printf("  %s\n\n", pass?"PASS":"FAIL");
}

/* ====================================================================
   Main
   ==================================================================== */
int main(void){
    tables_init();
    printf("=== SHMC Layer 1  —  Voice DSL Test ===\n\n");

    test_compile_structure();
    test_scale();
    test_repeat();
    test_rest_tie();
    test_nested_repeat();
    test_glide();
    test_melody();

    printf("=== done ===\n");
    return 0;
}
