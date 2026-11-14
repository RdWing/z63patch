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
/* G10 part 10d: playback of open-gate clips. Included by open_gate.c (same
 * translation unit). Declared by z63patch/gate_groups/open_gate_play.py.
 *
 * Old layout: native/open_gate_playback.S and the Z63_OPEN_GATE_PLAYBACK part of
 * native/open_gate.c. The old sites 0x40747DD0 (mov), 0x40748010 (ldr), 0x407E0558/0x407E05E4
 * (ldrh: leaf entries) and 0x407E9A44 (mov) are not branch words, so every record here is a
 * BL->BL call-site adapter instead (the rule for G10 parts):
 *
 *   setup     4 BL to the playback setup 0x40747DBC(media, object): the latch is cleared before
 *             the stock setup when the object is the main playback object (old site: instruction 5
 *             of the setup, before any call). C, pre.
 *   capture   BL 0x4074800C to the attribute getter 0x406F065C(0, 5, request) inside the setup:
 *             after the stock getter the attribute record (request +8) of media request +0 is
 *             classified and latched for the main object (x19 of the setup, read by the asm
 *             thunk; the old site read x19, x24 and [sp] right after this call). Asm thunk + C.
 *   class     15 + 2 BL to the composer aspect-class leaves 0x407E0558 / 0x407E05E4(image): after
 *             the stock leaf a class 1 (16:9 raster) of an open-gate N-RAW file becomes 0 (3:2).
 *             C, post. The leaves are pure; their callers read no caller-saved register after the
 *             call (static guard in the tests).
 *   film      BL 0x407E9A4C to 0x408088AC(record, 0) inside the film classifier 0x407E9A04, the
 *             call that follows the old site 0x407E9A44 on the movie-predicate path. For an N-RAW
 *             record (file kind 6) of 6048 x 4032 or 4032 x 2688 (w20/w19 of the classifier) the
 *             thunk returns 0 and sets the low half of w19 to the stock twin height (3402 / 2268),
 *             so the classifier's own dims compares give class 3 / 4, the old site's values. w19
 *             is read after this call only by those compares and is restored by the classifier's
 *             epilogue (static guard). Asm thunk + C.
 *
 * Decided on the clip (its attribute record, its file information), never on the current
 * setting. In PASS (z63g_phase 0) every adapter calls the stock function with
 * the caller's registers, returns its x0 and stores nothing outside its frame; the film thunk
 * leaves w19 as it was. No published-word read (no ACT hold).
 *
 * State (G10 +0x10 and +0x38..+0x3F, this part's only):
 *   +0x38 u64  play latch: media id | 1 << 32 | kind << 40 (kind 6 = 6K, 4 = 4K open gate), or 0.
 *              Written by the capture adapter (main object), cleared by the setup adapter (main
 *              object). The word a reader uses: valid iff bit 32, for the clip whose media id
 *              equals the current record 0xA04881950 +0 while its loaded byte +0x14 is set (old
 *              z63_og_pb_fit_latched). Read by G7's playback fit once G10-PLAY-REQUESTS R-PLAY-1
 *              lands; nothing in G10 reads it (the fit call sites are G7's records).
 *   +0x10 u8   kind of the latched clip (0, 6, 4), stored after +0x38 (diagnostic mirror; readers
 *              use +0x38).
 * Both are left as they are at restore (z63r_g10 is 10a's); every reader ANDs with the phase. */

#define OG_PB_OBJECT    0xa04881a70ull  /* main playback object (setup x1) */
#define OG_PB_CURRENT   0xa04881950ull  /* current record: media id +0, loaded byte +0x14 */
#define OG_PB_NCTG      0xa047fa6a0ull  /* maker-note tag record of the parsed movie */
#define OG_PB_LATCH     0x38u
#define OG_PB_N_KIND    6u              /* file kind of an N-RAW file (bits 20..22 of info +4) */

u64 stock_og_pb_setup(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x40747DBC playback setup */
u64 stock_og_pb_attr(u64,u64,u64,u64,u64,u64,u64,u64);     /* 0x406F065C attribute getter */
u64 stock_og_pb_class(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x407E0558 composer aspect class */
u64 stock_og_pb_class_t(u64,u64,u64,u64,u64,u64,u64,u64);  /* 0x407E05E4 the same, thumbnail rule */
u64 stock_og_pb_table(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x408088AC film class table */
u64 stock_og_pb_lookup(u64,u64,u64,u64,u64,u64,u64,u64);   /* 0x4081F9DC (id, &file, &information) */
u64 stock_og_pb_release(u64,u64,u64,u64,u64,u64,u64,u64);  /* 0x408A9258 (file) */
u64 stock_og_pb_kind(u64,u64,u64,u64,u64,u64,u64,u64);     /* 0x40808BE8 (record, 0): file kind */

static inline u32 og_pb_ld16(u64 a){return *(volatile const u16 *)(uintptr_t)a;}
static inline u32 og_pb_ld8(u64 a){return *(volatile const u8 *)(uintptr_t)a;}
/* The active sizes an open-gate file reports (its information record, the film getters). */
static inline u32 og_pb_active(u32 w,u32 h){return w==6048u&&h==4032u?6u:w==4032u&&h==2688u?4u:0u;}

/* 6 or 4 when the file with this id is an open-gate N-RAW file, else 0. The lookup's file
 * object is released as the stock callers do (old og_pb_file). */
__attribute__((noinline)) static u32 og_pb_file(u32 id) {
    u64 out[2]={0,0};              /* [0] file object, [1] information record */
    u32 k=0;
    if(stock_og_pb_lookup(id,(u64)(uintptr_t)&out[0],(u64)(uintptr_t)&out[1],0,0,0,0,0)&1u) {
        u64 info=out[1];
        if(info&&((z63g_ld32(info+4)>>20)&7u)==OG_PB_N_KIND)k=og_pb_active(og_pb_ld16(info+0xc),og_pb_ld16(info+0xe));
    }
    if(out[0])stock_og_pb_release(out[0],0,0,0,0,0,0,0);
    return k;
}

/* Kind of a clip from its attribute record (old og_pb_clip): N-RAW (+0x23C = 0x31) at an
 * open-gate active or coded size, or the 1920 x 1080 proxy (+0x23C = 2) whose maker note names an
 * open-gate N-RAW main movie. */
static u32 og_pb_clip(u64 a) {
    u32 codec=z63g_ld32(a+0x23c),w=z63g_ld32(a),h=z63g_ld32(a+4);
    if(codec==0x31u) {
        u32 k=og_pb_active(w,h);
        if(k)return k;
        return w==6060u&&h==4040u?6u:w==4040u&&h==2692u?4u:0u;
    }
    if(codec!=2u||w!=1920u||h!=1080u)return 0;
    if(og_pb_ld16(OG_PB_NCTG+0xa0)!=2u||og_pb_ld16(OG_PB_NCTG+0xa4)!=1920u||og_pb_ld16(OG_PB_NCTG+0xa6)!=1080u||
       og_pb_ld16(OG_PB_NCTG+0xfa)!=1u||og_pb_ld16(OG_PB_NCTG+0xfc)!=0x31u)return 0;
    return og_pb_active(og_pb_ld16(OG_PB_NCTG+0x102),og_pb_ld16(OG_PB_NCTG+0x104));
}
static void og_pb_put_latch(u64 p,u64 v) {
    __atomic_store_n((u64 *)(uintptr_t)(p+Z63_STATE_G10+OG_PB_LATCH),v,__ATOMIC_RELEASE);
    og_put8(p,OG_PLAY_LATCH,(u32)(v>>40)&0xffu);
}

/* ---- setup: 4 BL call sites of 0x40747DBC(media, object) ---- */
u64 g10_pb_setup(OG_ARGS) {
    u64 p=z63g_page();
    if(p&&z63g_phase(p,G10)&&a1==OG_PB_OBJECT)og_pb_put_latch(p,0);
    return stock_og_pb_setup(OG_PASS);
}

/* ---- the asm thunks' C halves ---- */
/* Phase before the stock call (the thunk's first call; z63g_page() first). */
__attribute__((used,noinline)) u32 g10_pb_phase(void) {
    u64 p=z63g_page();
    return p?z63g_phase(p,G10):0u;
}
/* Capture: after the stock getter. request: {u32 media, u32, u64 attribute record}; object: the
 * setup's x19. Only for the main object while the phase taken before the call still holds. */
__attribute__((used,noinline)) void g10_pb_latch(u64 request,u64 object,u32 before) {
    u64 p=z63g_page();
    if(!before||!p||z63g_phase(p,G10)!=before||object!=OG_PB_OBJECT||!request)return;
    u32 media=z63g_ld32(request);
    u64 attribute=z63g_ld64(request+8);
    u32 k=media&&attribute?og_pb_clip(attribute):0u;
    og_pb_put_latch(p,k?(u64)media|1ull<<32|(u64)k<<40:0u);
}
/* Film: after the stock table call. Returns {x0, x19}: (0, w19 with the twin height) for an
 * open-gate N-RAW record, else (the stock result, w19 unchanged). */
struct og_pb_pair{u64 x0,x19;};
__attribute__((used,noinline)) struct og_pb_pair g10_pb_film_c(u64 record,u64 result,u64 width,u64 height,u32 before) {
    struct og_pb_pair out={result,height};
    u32 k=og_pb_active((u32)width&0xffffu,(u32)height&0xffffu);
    if(!k||!before||!record)return out;
    u64 p=z63g_page();
    if(!p||z63g_phase(p,G10)!=before)return out;
    if((u32)stock_og_pb_kind(record,0,0,0,0,0,0,0)!=OG_PB_N_KIND)return out;
    out.x0=0;
    out.x19=(height&~0xffffull)|(k==6u?3402u:2268u);
    return out;
}

/* Thunks. Frame 0x60: x29/x30, x0-x8 of the caller (+0x10..+0x50), phase (+0x58). The acquire
 * load of P is the first access outside the stack. The stock callee gets x0-x8 as the caller
 * had them. */
#define OG_PB_SAVE \
    "  stp x29,x30,[sp,#-0x60]!\n" \
    "  mov x29,sp\n" \
    OG_LOAD_P \
    "  stp x0,x1,[sp,#0x10]\n" \
    "  stp x2,x3,[sp,#0x20]\n" \
    "  stp x4,x5,[sp,#0x30]\n" \
    "  stp x6,x7,[sp,#0x40]\n" \
    "  str x8,[sp,#0x50]\n" \
    "  bl g10_pb_phase\n" \
    "  str x0,[sp,#0x58]\n" \
    "  ldp x0,x1,[sp,#0x10]\n" \
    "  ldp x2,x3,[sp,#0x20]\n" \
    "  ldp x4,x5,[sp,#0x30]\n" \
    "  ldp x6,x7,[sp,#0x40]\n" \
    "  ldr x8,[sp,#0x50]\n"
__asm__(
".text\n"
".p2align 2\n"
".global g10_pb_attr\n"
".type g10_pb_attr,%function\n"
"g10_pb_attr:\n"
OG_PB_SAVE
"  bl stock_og_pb_attr\n"
"  str x0,[sp,#0x50]\n"
"  ldr x0,[sp,#0x20]\n"          /* request (caller's x2) */
"  mov x1,x19\n"                 /* the setup's object */
"  ldr w2,[sp,#0x58]\n"
"  bl g10_pb_latch\n"
"  ldr x0,[sp,#0x50]\n"
"  ldp x29,x30,[sp],#0x60\n"
"  ret\n"
".size g10_pb_attr,.-g10_pb_attr\n"
".p2align 2\n"
".global g10_pb_film\n"
".type g10_pb_film,%function\n"
"g10_pb_film:\n"
OG_PB_SAVE
"  bl stock_og_pb_table\n"
"  mov x1,x0\n"                  /* stock result */
"  ldr x0,[sp,#0x10]\n"          /* record (caller's x0) */
"  mov x2,x20\n"                 /* classifier: width */
"  mov x3,x19\n"                 /* classifier: height */
"  ldr w4,[sp,#0x58]\n"
"  bl g10_pb_film_c\n"
"  mov x19,x1\n"
"  ldp x29,x30,[sp],#0x60\n"
"  ret\n"
".size g10_pb_film,.-g10_pb_film\n"
);

/* ---- composer aspect class: 15 BL to 0x407E0558, 2 BL to 0x407E05E4 ---- */
static u64 og_pb_class_fix(u64 p,u32 ph,u64 image,u64 r) {
    if(!ph||(u32)r!=1u||!image||z63g_phase(p,G10)!=ph)return r;
    return og_pb_file(z63g_ld32(image))?0u:r;
}
u64 g10_pb_class(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u64 r=stock_og_pb_class(OG_PASS);
    return og_pb_class_fix(p,ph,a0,r);
}
u64 g10_pb_class_t(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u64 r=stock_og_pb_class_t(OG_PASS);
    return og_pb_class_fix(p,ph,a0,r);
}

/* ---- paused-movie preview: shared site pb_preview, role pre, G10 first (G7 after) ----
 * Old z63_og_pb_preview (R-PLAY-1): 0x408250C8(request, source). Kind 2 is the full-page paused
 * preview, a 1024 x 682 canvas into which the 1920 x 1080 preview is fitted 16:9; for an
 * open-gate file the destination becomes the whole canvas (origin 0, 1024 x 682). Kind 8 is a 3:2
 * thumbnail from the 640 x 360 preview with the crop byte set; for an open-gate file the crop is
 * cleared (the raster holds the whole 3:2 scene). Decided on the file (per-file lookup), never on
 * the setting. LIVE/DRAINING only (the core adapter calls it only then). */
static inline u32 og_pv16(const volatile u8 *b){return (u32)b[0]|(u32)b[1]<<8;}
static inline u32 og_pv32(const volatile u8 *b){return og_pv16(b)|og_pv16(b+2)<<16;}
static inline void og_pv_put32(volatile u8 *b,u32 v){b[0]=(u8)v;b[1]=(u8)(v>>8);b[2]=(u8)(v>>16);b[3]=(u8)(v>>24);}
void z63h_pb_preview_g10(u64 page,u32 phase,void *req,const void *src) {
    volatile u8 *request=(volatile u8 *)req;const volatile u8 *source=(const volatile u8 *)src;
    (void)page;
    if(!phase||!request||!source||og_pv32(request+0x50))return;
    u32 kind=og_pv32(request+0x34),want;
    if(kind==2u) {
        if(og_pv32(request+0x3c)!=(1024u|682u<<16)||og_pv16(request+0x30)||og_pv16(request+0x40)!=1024u)return;
        want=1920u|1080u<<16;
    } else if(kind==8u) {
        if(request[0x48]!=1u||og_pv16(request+0x4a)!=8u)return;
        u32 w=og_pv16(request+0x40),h=og_pv16(request+0x42);
        if(!w||!h||2u*w-3u*h+2u>4u)return;
        want=640u|360u<<16;
    } else return;
    if(og_pv32(source+0x58)||og_pv32(source+0x60)||og_pv32(source+0x64)!=want||og_pv32(source+0x68)!=want)return;
    if(!og_pb_file(og_pv32(source)))return;
    if(kind==2u){og_pv_put32(request+0x30,0);og_pv_put32(request+0x40,1024u|682u<<16);}
    else request[0x48]=0;
}
