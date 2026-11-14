// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-FileCopyrightText: 2026 RdWing
/**
 * z63patch - Nikon Z6III Modifications
 * AGPLv3 Open Source. Use is subject to license terms.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * @package z63patch
 * @license AGPLv3 License (https://opensource.org/licenses/AGPL-3.0)
 *
 *   Copyright (C) 2026 RdWing
 *
 */
/* G7, desqueeze of the movie live view, its AF/touch/lit-point/navigator maps and movie
 * playback, on the arm-time gate.
 * Declared by z63patch/gate_groups/desqueeze.py; one translation unit linked alone at the
 * group's fixed range, so its bytes do not depend on the composition. The arithmetic is that of
 * native/desqueeze_view.c (old layout, desqueeze-view units) without the corner marks
 * (deferred). Composition with G10 (open gate), all without calling
 * G10 code and without core cost except the shared preview site:
 *  - window: with open gate on (G10 +0) the movie window becomes the 3:2 rows 0..682 before the
 *    ratio (old z63_og_view_window, (0,170)-(1024,510) at 2.00x; R-PLAY-2);
 *  - playback fit: an open-gate clip latched by G10 (+0x38) is fitted as 3:2 (old og fit; R-PLAY-1);
 *  - navigator: the 16:9 inset becomes the 3:2 one in place before the squeeze (R-AF-3);
 *  - paused preview: the shared site pb_preview, G10's hook first (R-PLAY-1);
 *  - the two open-gate references that only an emulator or the camera can decide are compile-time
 *    switches named in z63patch/gate_groups/desqueeze.py (G7_OG_AF_REFERENCE, G7_OG_LIT_BASIS).
 *
 * Every record replaces a BL with a BL to an adapter here (no non-branch word). Each adapter
 * makes the acquire load of P first and, in PASS (no page, phase not LIVE/DRAINING, G7 broken or
 * not selected, ACT refused, publication busy through the bounded wait), calls the stock callee
 * with x0-x7 unchanged, returns its x0, and stores nothing outside its frame.
 *
 * Feature state (Z63_STATE_G7, 16 bytes, zero at arming): +0 native rows of the global view
 * window (y0 | y1 << 16), +4 shown rows, +8 the mask record (wanted + 1, or 0 before the first
 * pass; bit 3 of wanted = open gate), +12 the view decision (1 movie live view, 0 not). Written by
 * the window adapter (+0, +4, +12) and by the mask adapter (+8, +12) only.
 *
 * The view decision (g7_view): movie when the pipeline class 0xA03D42108 is a
 * movie-path class of 0x410538FC (6, 7, 8, 9, 0x0A, 0x0B), not movie when it is 5 or 12 (photo
 * live view, still capture), otherwise (0 with a menu open or the pipeline restarting, 3
 * magnified, a remapped class) movie when the readiness word 0xA048BB160 reads 1 (movie view with a
 * ready card: the earlier rule, so a ratio picked in the movie menu is already applied by
 * the view passes stock runs while the menu is open and at its close), else the last decision is
 * kept. Every caller evaluates the class itself (the lit points too; only the window and mask
 * adapters store the result), so a stored 0 is never read in place of a decisive class. The class
 * follows the live-view mode only (it has no card input), so a card pull, a full card or N-RAW on a
 * FAT32 card changes nothing in running movie live view. Earlier this was the
 * word 0xA048BB160, which is movie-RECORDING readiness: it dropped to 0 on a card pull in running
 * movie live view and turned the desqueeze off (analysis z63-movie-word-20261112).
 *
 * Photo live view: the window rule acts only in a movie view decision, so in photo LV the
 * recorded native and shown rows are equal and every map refuses (it needs shown rows strictly
 * shorter than native, or different with open gate on); the lit points need the movie decision
 * too; the mask records wanted 0 and forces a recompute only when the wanted value changes, which
 * in photo LV it does not; playback fit and the paused-movie preview act only on playback
 * requests (result/request shapes the live view never passes). The touch adapter calls the GUI
 * refresh 0x403A4BF0 only when recorded rows can map (never in photo LV).
 *
 * G10 (open gate): read only through z63g_peer_state(p, Z63_STATE_G10): +0 (og_active, u8), +3
 * (the plan mode, u8) and +0x38 (the play latch, u64), and only while G10 is selected and live
 * (z63g_phase(p, 10) != 0); +0 only while +3 is set (G10's reader rule og_plan_kind: G10 may
 * pre-set +0 in photo view with +3 clear). Without G10 in the build (not selectable at arming)
 * that is 0: open gate off. */
#include <stdint.h>
#define Z63_SETTINGS_SHARED_V3
#include "../gate_api.h"
#include "../settings_record.c"
#include "../../nv_quality.c"
#include "../sites/pb_preview.h"
#include "open_gate.h"                       /* G10's wanted-kind rule (og_rule_chosen, og_rule_slots): shared, not copied */
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef int16_t s16;typedef int32_t s32;

#define G7 7u
#define G10 10u
#define PIPE_STATE    0xa03d42100ull  /* live-view pipeline state (2 running) */
#define PIPE_CLASS    0xa03d42108ull  /* live-view pipeline class (0x410538FC) */
#define G7_MOVIE_CLASSES (1u<<6|1u<<7|1u<<8|1u<<9|1u<<10|1u<<11)  /* the movie path of 0x410538FC */
#define G7_PHOTO_CLASSES (1u<<5|1u<<12)                           /* photo live view, still capture */
#define GLOBAL_WINDOW 0xa03d75e78ull  /* the global view record's rectangle */
#define GUI_LV        0xa03394d80ull  /* the GUI's copy of the live-view window */
#define INSET_TABLE   0x44b189a8ull   /* per-output lit-point inset (s16) */
#define R16(a) (*(volatile const u16 *)(uintptr_t)(a))
#define ST32(p,o) (*(volatile u32 *)(uintptr_t)((p)+Z63_STATE_G7+(o)))

typedef u64 (*g7_fn)(u64,u64,u64,u64,u64,u64,u64,u64);
u64 stock_window(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x411894D8 LV window leaf */
u64 stock_memcmp(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x4163B3C0 */
u64 stock_af_rect(u64,u64,u64,u64,u64,u64,u64,u64);   /* 0x40394E88 AF frame builder */
u64 stock_touch(u64,u64,u64,u64,u64,u64,u64,u64);     /* 0x4045C1FC touch consumer */
u64 stock_gui_refresh(void);                          /* 0x403A4BF0 GUI LV refresh (no arguments) */
u64 stock_pb_fit(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x4074AA30 movie playback fit */
u64 stock_lit(u64,u64,u64,u64,u64,u64,u64,u64);       /* 0x403FF1B0 lit-points thunk */
u64 stock_set_rect(u64,u64,u64,u64,u64,u64,u64,u64);  /* 0x40409F5C set rect (by value) */
/* Switches (z63patch/gate_groups/desqueeze.py, the one place to change them; G10-PLAY-REQUESTS
 * R-PLAY-5 and R-PLAY-4; camera-emulator items):
 *  G7_OG_AF_REFERENCE  0 (default): with open gate on, AF frame, touch and navigator map from the
 *                      recorded native rows, as the old combined layout did (in its emulator
 *                      runs); 1: from the open-gate rows 0..682 (the old
 *                      Z63_OPEN_GATE_AF reference, G7's builder's choice).
 *  G7_OG_LIT_BASIS     0 (default): lit points rescaled from the 16:9 basis 9W/16 (576), as the
 *                      old combined monitor build; 1: with open gate on, from the 3:2 basis
 *                      round(2W/3), which matches 10c's photo-layout lit points in the old
 *                      viewfinder rule (layout leaf <= 2). */
#ifndef G7_OG_AF_REFERENCE
#define G7_OG_AF_REFERENCE 0
#endif
#ifndef G7_OG_LIT_BASIS
#define G7_OG_LIT_BASIS 0
#endif
#define G7_READINESS  0xa048bb160ull  /* movie-recording readiness: G10's reader rule (g7_og), view fallback */
#define PB_CURRENT    0xa04881950ull  /* playback: current record, media id +0, loaded byte +0x14 */
#define NAV_CLASS     0xa0343bdb0ull  /* navigator class byte (1: 16:9 inset) */
#define NAV_ASPECT    0xa0343bf0cull  /* navigator aspect byte (4: 16:9, 3, 2, else 3:2) */
#define NAV_TABLE     0x42ba4740ull   /* 16 records of 32 B: rects at +0 (3:2), +8, +0x10, +0x18 (16:9) */

static const u16 g7_numerator[8]={100,125,130,133,150,160,180,200};

static u32 g7_live(u64 *pp){u64 p=z63g_page();*pp=p;return p?z63g_phase(p,G7):0;}
/* G10's reader rule (og_plan_kind, open_gate.c) on its active byte (+0): the readiness word
 * 0xA048BB160 reads 1, or the plan mode (+3) is set while movie live view runs (state 2, movie
 * class). In photo view the latch may already hold the kind planned for the next movie entry
 * (z63p_g10); neither term holds there. */
__attribute__((noinline)) static u32 g7_og(u64 p){
    const volatile u8 *g=z63g_peer_state(p,Z63_STATE_G10);
    if(!z63g_phase(p,G10)||!g[0])return 0;
    if(z63g_ld32(G7_READINESS)==1u)return 1;
    u32 c=z63g_ld32(PIPE_CLASS);
    return g[3]&&z63g_ld32(PIPE_STATE)==2u&&c<32u&&((G7_MOVIE_CLASSES>>c)&1u);
}
/* ---- the menu-close pass ----
 * Emulator (stock and patched alike): at every menu close stock runs its window/mask pass (0x40F1E160
 * on one CPU, then 0x40F1B7F0 + 0x40F1BAD4 on the other ~0.4 s later) BEFORE the live-view applier
 * 0x40F681FC (37-85 ms after the mask), where g10_replan changes the open-gate latch, and before the
 * core publishes a changed desqueeze field (asynchronously, seconds later in idle movie view).
 * No user-neutral event runs the pass again (DISP, half-press, AF-ON, i-menu, playback do not; record
 * start/stop, card pull/in and the lever do), so the window stayed one state behind. A re-trigger
 * through stock's pass was rejected: the worker's flag that enables it (byte 0 of its x2 buffer,
 * read at 0x40F19B78 and 0x40F1A62C) also gates settings work (0x4100426C notification batch through
 * a native session, settings readers 0x41003D6C / 0x40F0C0B0, indirect calls).
 * So at that pass the adapters decide from what is about to apply:
 *  - the pass is a menu-close pass when the readiness word reads 1 (movie view with a ready card,
 *    the only case where g10_replan raises or keeps open gate), the pipeline is not running a movie
 *    or photo class (state != 2, or another class: menu 0, restart), nothing is recording (movie
 *    work and outstanding 0) and G2 is LIVE (the stored word is ours);
 *  - open gate: G10 LIVE and G10's own rule on the STORED word (og_rule_chosen with G10's stale
 *    marks, og_rule_slots): what g10_replan latches at the coming applier call. G10 not LIVE
 *    (DRAINING after a pull): the latch rule as before (g10_replan raises nothing there);
 *  - desqueeze: the ratio of the STORED word (published by the core when idle in movie view).
 * Every other pass decides as before (latch hold rule, published word). A re-plan refused at the
 * applier (ACT or TX counted at that instant) leaves the window one state ahead until the next pass
 * (record key, card event, lever). */
#define G7_STORED     0xa03f0eef4ull  /* = Z63S_WORD: the stored settings word */
#define G7_MOVIE_WORK 0xa03324ce0ull
#define G7_MOVIE_OUT  0xa03324dc8ull
static u32 g7_menu_pass(u64 p){
    if(z63g_ld32(G7_READINESS)!=1u)return 0;
    u32 c=z63g_ld32(PIPE_CLASS),bit=c<32u?1u<<c:0u;
    if(z63g_ld32(PIPE_STATE)==2u&&(bit&(G7_MOVIE_CLASSES|G7_PHOTO_CLASSES)))return 0;
    if(z63g_ld32(G7_MOVIE_WORK)||z63g_ld32(G7_MOVIE_OUT))return 0;
    return z63g_phase(p,2u)==Z63_LIVE;
}
/* G10's latch holds an open-gate kind (the geometry the readers give while readiness is 1). */
static u32 g7_og_latched(u64 p){
    if(!z63g_phase(p,G10))return 0;
    u32 k=z63g_peer_state(p,Z63_STATE_G10)[1];
    return k==6u||k==4u;
}
#define G7_ROWS_169_Y0 54u    /* stock 16:9 movie window rows (class 1, output 0, GUI 1024 x 682) */
#define G7_ROWS_169_Y1 630u
/* Open gate for the window and mask adapters: the coming plan at a menu-close pass, else g7_og. */
static u32 g7_og_pass(u64 p,u32 menu){
    if(!menu||z63g_phase(p,G10)!=Z63_LIVE)return g7_og(p);
    return og_rule_replan_kind(z63g_peer_state(p,Z63_STATE_G10),z63g_acq32(G7_STORED))!=0u;
}

/* The view decision (header): decide = 1 (window and mask adapters, inside their ACT hold) updates
 * +12 from the class; 0 (lit points) only reads it. */
__attribute__((noinline)) static u32 g7_view(u64 p,u32 decide){
    u32 v=ST32(p,12);
    u32 c=z63g_ld32(PIPE_CLASS),bit=c<32u?1u<<c:0u;
    /* Readiness 1 first: it reads 1 only in movie view with a ready card (never in photo view), and
     * at the lever to movie it turns 1 while the pipeline still runs the photo class (emulator:
     * R 1, pipe 2/5; the pass then decided photo and left the movie unsqueezed). */
    u32 next=z63g_ld32(G7_READINESS)==1u?1u:(bit&G7_MOVIE_CLASSES)?1u:(bit&G7_PHOTO_CLASSES)?0u:v;
    if(decide&&next!=v)ST32(p,12)=next;
    return next;
}
/* The clip in playback is an open-gate clip: G10's play latch (+0x38: media id | 1 << 32 |
 * kind << 40) names the current record while it is loaded (old z63_og_pb_fit_latched). */
static u32 g7_og_play(u64 p){
    if(!z63g_phase(p,G10))return 0;
    u64 l=__atomic_load_n((const u64 *)(uintptr_t)(p+Z63_STATE_G10+0x38u),__ATOMIC_ACQUIRE);
    return ((l>>32)&1u)&&(u32)l==z63g_ld32(PB_CURRENT)&&*(volatile const u8 *)(uintptr_t)(PB_CURRENT+0x14u);
}
/* The published ratio (0 for a foreign word, as in the old layout), for a caller holding ACT. */
static u32 g7_setting(u64 e){
    struct z63_feature_settings v;
    if(z63_settings_decode((u32)e,&v)==Z63_RECORD_FOREIGN||v.desqueeze>=8)return 0;
    return v.desqueeze;
}
static u32 g7_wanted(u32 movie,u64 e){return movie?g7_setting(e):0;}

/* ---- window (records 0x40F1BAD4, 0x40F1E160) ---- */
struct g7_rect{u16 x0,y0,x1,y1;};
/* Z8 rounding: nearest even height with ties to even, centred on an even origin. */
static void g7_window_rule(volatile struct g7_rect *w,u32 output,u32 window_class,u32 setting){
    if(output!=0||window_class>1||setting==0||setting>=8||w->x1<=w->x0||w->y1<=w->y0||w->x1>1024||w->y1>682)return;
    u32 h=(u32)w->y1-w->y0,denominator=2u*g7_numerator[setting];
    u32 scaled=h*100u,q=scaled/denominator,r=scaled%denominator;
    if(2u*r>denominator||(2u*r==denominator&&(q&1u)))++q;
    u32 next=2u*q;
    if(!next||next>=h)return;
    u16 y0=(u16)(w->y0+2u*((h-next)/4u));
    w->y0=y0;w->y1=(u16)(y0+next);
}
u64 g7_view_window(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7){
    u64 p;u32 ph=g7_live(&p);
    u64 r=stock_window(a0,a1,a2,a3,a4,a5,a6,a7);
    if(!ph||!(u32)r||!a3||!z63g_act_enter(p,G7))return r;
    u64 e=z63g_effective(p);
    if(e) {
        volatile struct g7_rect *w=(volatile struct g7_rect *)(uintptr_t)a3;
        u32 native=(u32)w->y0|(u32)w->y1<<16,movie=g7_view(p,1),menu=g7_menu_pass(p);
        if(menu)e=z63g_acq32(G7_STORED);
        /* Open gate (R-PLAY-2): the movie window is the 3:2 rows 0..682 before the ratio; the
         * recorded native rows stay the stock ones read above, as the old layout recorded them. */
        u32 og=movie&&g7_og_pass(p,menu),full=(u32)a0==0&&w->x0==0&&w->x1==1024;
        if(movie&&!og&&(u32)a0==0&&(u32)a1==0&&z63g_ld32(G7_READINESS)!=1u&&!g7_og_latched(p)) {
            /* Lever to photo (emulator: pipe 2/7 still, readiness 1 then 0): stock
             * already draws the photo window, class 0 (3:2 aspect from the dims query) with rows
             * 0..682. A stock movie plan is 16:9 (class 1); class 0 in movie view comes only from
             * G10's open-gate geometry, excluded here (latch 0). With readiness 0 too (the lever's
             * second pass; the first, readiness 1, is overwritten by it) this pass is for photo view:
             * no ratio, and the view decision goes to photo. Readiness 1 keeps the old layout's
             * class-0 movie squeeze. */
            movie=0;ST32(p,12)=0;
        }
        if(og&&full&&(u32)a1<=1&&w->y1>w->y0&&w->y1<=682) {
            w->y0=0;w->y1=682;
        } else if(movie&&menu&&full&&(u32)a1<=1u&&w->y0==0&&w->y1==682&&g7_og_latched(p)) {
            /* Lowering at the menu-close pass (emulator: regular pick or H.265 after 3:2):
             * stock's window follows G10's readers, which still hold the open-gate latch at this
             * pass (readiness 1): its class helper 0x40F1DF90 takes the aspect from the dims query
             * 0x4105A6A0 (3:2 -> class 0) and the window reads 0..682; the coming plan is the stock 16:9 one, whose
             * window is 54..630 (every non-open-gate pass of that run, and the no-card close where
             * the readers were already stock). Recorded as the native rows too. */
            w->y0=G7_ROWS_169_Y0;w->y1=G7_ROWS_169_Y1;
            native=G7_ROWS_169_Y0|G7_ROWS_169_Y1<<16;
        }
        g7_window_rule(w,(u32)a0,(u32)a1,g7_wanted(movie,e));
        if(a3==GLOBAL_WINDOW&&(u32)a0==0) {
            __atomic_store_n((u32 *)(uintptr_t)(p+Z63_STATE_G7+0),native,__ATOMIC_RELEASE);
            __atomic_store_n((u32 *)(uintptr_t)(p+Z63_STATE_G7+4),(u32)w->y0|(u32)w->y1<<16,__ATOMIC_RELEASE);
        }
    }
    z63g_act_leave(p,G7);
    return r;
}

/* ---- mask (records 0x40F1B7F0, 0x40F1B880: bl memcmp) ----
 * g7_mask_cmp: the acquire load of P first (x16), then z63_g7_mask_c with x3 = x22; returns the
 * stock memcmp's x0 and puts the mask into x22 (unchanged in PASS). w22 holds the change mask
 * from 0x40F1B75C until 0x40F1B9EC (mov w25,w22), read nowhere else (static check in the tests),
 * so this equals the old record at 0x40F1B9EC. */
struct g7_pair{u64 r,mask;};
struct g7_pair z63_g7_mask_c(u64 a0,u64 a1,u64 a2,u64 x22){
    u64 p;u32 ph=g7_live(&p);
    struct g7_pair out={stock_memcmp(a0,a1,a2,0,0,0,0,0),x22};
    if(!ph||!z63g_act_enter(p,G7))return out;
    u64 e=z63g_effective(p);
    if(e) {
        u32 menu=g7_menu_pass(p);
        if(menu)e=z63g_acq32(G7_STORED);
        u32 mask=(u32)x22,wanted=g7_wanted(g7_view(p,1),e)|(g7_og_pass(p,menu)?8u:0u),recorded=ST32(p,8);
        if(recorded!=wanted+1) {
            ST32(p,8)=wanted+1;
            /* Movie-view arming (design 3.5): "never recorded" counts as stock, so the first pass
             * after an arming in movie view with a stored ratio recomputes; in photo LV wanted is
             * 0 and a photo arming is unchanged. */
            if(recorded||wanted)mask|=5u;
        }
        if(mask!=(u32)x22)out.mask=mask;
    }
    z63g_act_leave(p,G7);
    return out;
}
__asm__(
".text\n"
".p2align 2\n"
".global g7_mask_cmp\n"
".type g7_mask_cmp,%function\n"
"g7_mask_cmp:\n"
"  stp x29,x30,[sp,#-16]!\n"
"  mov x29,sp\n"
"  movz x16,#0xae20\n"
"  movk x16,#0x028c,lsl #16\n"
"  movk x16,#0xa,lsl #32\n"
"  ldar x16,[x16]\n"
"  mov x3,x22\n"
"  bl z63_g7_mask_c\n"
"  mov x22,x1\n"
"  ldp x29,x30,[sp],#16\n"
"  ret\n"
".size g7_mask_cmp,.-g7_mask_cmp\n"
);

/* ---- maps from the recorded rows ---- */
struct g7_map{u32 y0n,hn,y0s,hs;};
static int g7_rows(u64 p,struct g7_map *m,u32 og){
    u32 native=__atomic_load_n((u32 *)(uintptr_t)(p+Z63_STATE_G7+0),__ATOMIC_ACQUIRE);
    u32 shown=__atomic_load_n((u32 *)(uintptr_t)(p+Z63_STATE_G7+4),__ATOMIC_ACQUIRE);
    u32 y0n=native&65535u,y1n=native>>16,y0s=shown&65535u,y1s=shown>>16;
    if(y1n<=y0n||y1s<=y0s||y1n>682||y1s>682)return 0;
    if(og?(y0s==y0n&&y1s==y1n):(y1s-y0s>=y1n-y0n))return 0;
    m->y0n=y0n;m->hn=y1n-y0n;m->y0s=y0s;m->hs=y1s-y0s;return 1;
}
/* The GUI's copy shows exactly the recorded shown rows. */
static int g7_gui(const struct g7_map *m){return R16(GUI_LV+0xa)==m->y0s&&R16(GUI_LV+0xe)==m->y0s+m->hs;}
static int g7_map(u64 p,struct g7_map *m){return g7_rows(p,m,g7_og(p))&&g7_gui(m);}
/* AF frame, touch, navigator: in open gate the reference is the 3:2 rows 0..682. */
static int g7_og_reference(u64 p,struct g7_map *m){
#if !G7_OG_AF_REFERENCE
    (void)p;(void)m;return 1;            /* old combined: the recorded native rows */
#endif
    if(!g7_og(p))return 1;
    if(m->y0s+m->hs>682u||(m->y0s==0&&m->hs==682u))return 0;
    m->y0n=0;m->hn=682u;return 1;
}
static s32 g7_round(s32 numerator,s32 denominator){
    s32 half=denominator/2;
    return (numerator<0?numerator-half:numerator+half)/denominator;
}

/* AF frame builder 0x40394E88(out, &centre x/y, &size w/h): mapped copies in the frame. */
u64 g7_af_rect(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7){
    u64 p;u32 ph=g7_live(&p);
    u16 c[4];struct g7_map m;
    if(ph&&a1&&a2&&g7_map(p,&m)&&g7_og_reference(p,&m)) {
        const volatile u16 *centre=(const volatile u16 *)(uintptr_t)a1,*size=(const volatile u16 *)(uintptr_t)a2;
        s32 y=(s32)m.y0s+g7_round(((s32)centre[1]-(s32)m.y0n)*(s32)m.hs,(s32)m.hn);
        u32 h=((u32)size[1]*m.hs+m.hn/2u)/m.hn;
        c[0]=centre[0];c[1]=(u16)(y<0?0:y>65535?65535:y);
        c[2]=size[0];c[3]=(u16)(h<2u?2u:h);
        a1=(u64)(uintptr_t)c;a2=(u64)(uintptr_t)(c+2);
    }
    return stock_af_rect(a0,a1,a2,a3,a4,a5,a6,a7);
}

/* Touch consumer 0x4045C1FC(&point x/y): the GUI copy is refreshed first (as the consumer does
 * on entry; it takes no arguments) when recorded rows can map, then the row goes back to the
 * native window. */
u64 g7_touch(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7){
    u64 p;u32 ph=g7_live(&p);
    u16 pt[2];struct g7_map m;
    if(ph&&a0&&g7_rows(p,&m,g7_og(p))) {
        stock_gui_refresh();
        if(g7_rows(p,&m,g7_og(p))&&g7_gui(&m)&&g7_og_reference(p,&m)) {
            const volatile u16 *point=(const volatile u16 *)(uintptr_t)a0;
            s32 y=(s32)m.y0n+g7_round(((s32)point[1]-(s32)m.y0s)*(s32)m.hn,(s32)m.hs);
            s32 low=(s32)m.y0n,high=(s32)(m.y0n+m.hn-1u);
            pt[0]=point[0];pt[1]=(u16)(y<low?low:y>high?high:y);
            a0=(u64)(uintptr_t)pt;
        }
    }
    return stock_touch(a0,a1,a2,a3,a4,a5,a6,a7);
}

/* Navigator 0x4042971C: the background rectangle (x0|y0<<16|x1<<32|y1<<48) at [sp+0x48] of the
 * caller, stored at 0x40429A58 or 0x40429F98 and first read by the set-rect call at 0x40429A64
 * (only 0x40429F9C enters 0x40429A5C from outside). Mapped in place there, so the second set-rect
 * call and the later inner rectangle and frame use it, as with the old stores. */
/* Open gate (R-AF-3, G10-AF-REQUESTS): the old layout made the navigator's class/aspect reads
 * choose the 3:2 inset. In place here: s = the rect offset stock used (16:9 for class 1 or
 * aspect 4), o = the one the old layout used (aspect 4 read as 3:2); every record whose rect at
 * +s equals the caller's must agree on its rect at +o, which then replaces it. */
static u32 g7_nav_off(u32 a){return a==3u?0x10u:a==2u?8u:0u;}
struct g7_nav{u64 rect,found;u32 s,o,n,bad;};
__attribute__((noinline)) static void g7_nav_rec(struct g7_nav *q,u32 k){
    u64 rec=NAV_TABLE+32u*k;
    if(*(volatile const u64 *)(uintptr_t)(rec+q->s)!=q->rect)return;
    u64 v=*(volatile const u64 *)(uintptr_t)(rec+q->o);
    if(q->n&&v!=q->found)q->bad=1;
    q->found=v;q->n++;
}
static void g7_nav_og(volatile u64 *at){
    struct g7_nav q;
    u32 cls=*(volatile const u8 *)(uintptr_t)NAV_CLASS,aspect=*(volatile const u8 *)(uintptr_t)NAV_ASPECT;
    q.s=cls==1u||aspect==4u?0x18u:g7_nav_off(aspect);q.o=g7_nav_off(aspect==4u?0u:aspect);
    if(q.s==q.o)return;
    q.rect=*at;q.found=0;q.n=0;q.bad=0;
#pragma clang loop unroll(full)
    for(u32 k=0;k<16u;k++)g7_nav_rec(&q,k);
    if(q.n&&!q.bad)*at=q.found;
}
u64 g7_nav_rect(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7){
    u64 p;u32 ph=g7_live(&p);
    struct g7_map m;
    if(ph&&a1&&!(a1&7u)&&g7_og(p))g7_nav_og((volatile u64 *)(uintptr_t)a1);
    if(ph&&a1&&!(a1&7u)&&g7_map(p,&m)&&g7_og_reference(p,&m)) {
        volatile u64 *at=(volatile u64 *)(uintptr_t)a1;
        u64 rect=*at;
        u32 y0=(u32)(rect>>16)&65535u,y1=(u32)(rect>>48);
        if(y1>y0) {
            u32 h=((y1-y0)*m.hs+m.hn/2u)/m.hn;
            if(h&&!(g7_og(p)&&h>y1))*at=(rect&~(0xffffull<<16))|((u64)((y1-h)&65535u)<<16);
        }
    }
    return stock_set_rect(a0,a1,a2,a3,a4,a5,a6,a7);
}

/* ---- lit AF points: 0x41188EC0(bits, &point record, count, out), via the thunk 0x403FF1B0 ----
 * Up to 299 rectangles of s16 x0,y0,x1,y1; no loop (verifier): 19 calls of 16 unrolled calls. */
struct g7_pts{s32 h,hd,inset;};
__attribute__((noinline)) static void g7_point(volatile s16 *r,const struct g7_pts *q){
    r[1]=(s16)(g7_round(((s32)r[1]-q->inset)*q->h,q->hd)+q->inset);
    r[3]=(s16)(g7_round(((s32)r[3]+q->inset)*q->h,q->hd)-q->inset);
}
__attribute__((noinline)) static void g7_points16(volatile s16 *out,u32 n,const struct g7_pts *q){
#pragma clang loop unroll(full)
    for(u32 k=0;k<16u;k++)if(k<n)g7_point(out+4u*k,q);
}
static void g7_points(u64 p,u32 result,const volatile u32 *record,u32 count,volatile s16 *out){
    struct g7_map m;struct g7_pts q;
    if(!(result&1u)||!record||!out)return;
    if(count>0x12bu)count=0x12bu;
    u32 output=record[0];
    if(!count||output>2u)return;
    u32 grid=output==1u?1280u:1024u;
    if((u32)R16(GUI_LV+0xc)-(u32)R16(GUI_LV+8)!=grid)return;
    if(!g7_view(p,0)||!g7_map(p,&m))return;
    q.h=(s32)m.hs;q.hd=(s32)(grid*9u/16u);
#if G7_OG_LIT_BASIS
    if(g7_og(p))q.hd=(s32)((2u*grid+1u)/3u);   /* 10c draws the photo (3:2) layout under open gate */
#endif
    s32 gap=q.h>q.hd?q.h-q.hd:q.hd-q.h;
    if(gap<=q.hd/64)return;
    s32 inset=(s16)R16(INSET_TABLE+2u*output);
    q.inset=(inset+(inset<0?1:0))/2;
#pragma clang loop unroll(full)
    for(u32 j=0;j<19u;j++)if(16u*j<count)g7_points16(out+64u*j,count-16u*j,&q);
}
u64 g7_lit(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7){
    u64 p;u32 ph=g7_live(&p);
    u64 r=stock_lit(a0,a1,a2,a3,a4,a5,a6,a7);
    if(ph)g7_points(p,(u32)r,(const volatile u32 *)(uintptr_t)a1,(u32)a2,(volatile s16 *)(uintptr_t)a3);
    return r;
}

/* ---- movie playback fit 0x4074AA30(media w, media h, mode, window record, result) ----
 * After the stock fit a result at most 1280 wide gets the height 8*round(h/(8S)), centred. The
 * published ratio applies whatever the current view, as on Z8. */
u64 g7_pb_fit(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7){
    u64 p;u32 ph=g7_live(&p);
    u64 media_h=a1;
    /* An open-gate clip (G10's play latch) is fitted as 3:2: (3, 2, 1), the old layout's fit
     * arguments (R-PLAY-1); the ratio below still tests the caller's own media height. */
    if(ph&&g7_og_play(p)){a0=3;a1=2;a2=1;}
    u64 r=stock_pb_fit(a0,a1,a2,a3,a4,a5,a6,a7);
    volatile u8 *result=(volatile u8 *)(uintptr_t)a4;const volatile u8 *window=(const volatile u8 *)(uintptr_t)a3;
    if(!ph||!result||!window||!(u32)media_h||!z63g_act_enter(p,G7))return r;
    u64 e=z63g_effective(p);
    u32 setting=e?g7_setting(e):0;
    if(setting&&result[0]) {     /* the caller's memory is read only with a ratio published */
        s32 width=(s32)(s16)(window[0xa]|window[0xb]<<8)-(s32)(s16)(window[6]|window[7]<<8);
        u32 n=g7_numerator[setting],h=(u32)result[4]|(u32)result[5]<<8;
        u32 next=8u*((h*100u+4u*n)/(8u*n));
        if(width<=1280&&next&&next<h) {
            s32 y0=(s32)(s16)(result[8]|result[9]<<8)+2*(s32)((h-next)/4u),y1=y0+(s32)next;
            result[4]=(u8)next;result[5]=(u8)(next>>8);
            result[8]=(u8)y0;result[9]=(u8)((u32)y0>>8);
            result[0xc]=(u8)y1;result[0xd]=(u8)((u32)y1>>8);
        }
    }
    z63g_act_leave(p,G7);
    return r;
}

/* ---- paused movie page: 0x408250C8(request, source), shared site pb_preview (pre, after G10) ----
 * For the 1920x1080 preview request (kind 2) on a canvas at most 1280 wide, the destination
 * height becomes the squeezed height by the window rule and moves down to stay centred. Edits
 * request +0x32 and +0x42 only, before the stock call (the old entry hook's order). */
static u32 g7_le16(const volatile u8 *p){return (u32)p[0]|(u32)p[1]<<8;}
static u32 g7_le32(const volatile u8 *p){return g7_le16(p)|g7_le16(p+2)<<16;}
static void g7_put16(volatile u8 *p,u32 v){p[0]=(u8)v;p[1]=(u8)(v>>8);}
void z63h_pb_preview_g7(u64 page,u32 phase,void *req,const void *src){
    volatile u8 *request=(volatile u8 *)req;const volatile u8 *source=(const volatile u8 *)src;
    if(phase&&request&&source&&z63g_act_enter(page,G7)) {
        u64 e=z63g_effective(page);
        u32 setting=e?g7_setting(e):0;
        if(setting&&g7_le32(request+0x34)==2u&&g7_le32(request+0x50)==0u&&!g7_le32(source+0x58)&&!g7_le32(source+0x60)&&
           g7_le32(source+0x64)==0x04380780u&&g7_le32(source+0x68)==0x04380780u&&g7_le16(request+0x3c)<=1280u) {
            u32 h=g7_le16(request+0x42),denominator=2u*g7_numerator[setting];
            u32 scaled=h*100u,q=scaled/denominator,rem=scaled%denominator;
            if(2u*rem>denominator||(2u*rem==denominator&&(q&1u)))++q;
            u32 next=2u*q;
            if(next&&next<h) {
                g7_put16(request+0x42,next);
                g7_put16(request+0x32,g7_le16(request+0x32)+2u*((h-next)/4u));
            }
        }
        z63g_act_leave(page,G7);
    }
}
