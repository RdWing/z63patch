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
/* G4, ProRes grades on the arm-time gate.
 * Declared by z63patch/gate_groups/prores.py; one translation unit linked alone at the group's
 * fixed range, so its bytes do not depend on the composition. No allocation, no M set, no asset:
 * the old z63_parse_fourcc/z63_shoot_table/ensure() and the RATE_TABLES/TEMPLATES/SHOOT_TABLES
 * assets of native/prores.c and z63patch/prores.py are replaced by the wrappers below.
 *
 * The grade (0 HQ, 1 422, 2 LT, 3 Proxy) is the ProRes field of the published settings word
 * (payload bits 0-1, word bits 8-9, read as z63_settings_decode does; prores_field), once per ACT
 * hold (ACT[4 & 3] = ACT[0]) and used inside that hold. G2 writes it (row/value contract of
 * z63patch/gate_groups/menu.py); G4 never writes the settings word and allocates no bit.
 *
 * Records (all restore to the stock word; 1 and 3 install and restore under Q):
 *  1 0x40CEB2C0 "bl 0x40CE5E04" (the FourCC read before the parser's accept test for "apch" at
 *    0x40CEB330, the old record, now stock) -> bl g4_fourcc. FULL (LIVE/DRAINING, G4
 *    selected and not broken), on the path to 0x40CEB330: for the four ProRes 422 FourCCs the
 *    grade goes to G4 state byte 0 (0x80 | grade; any other FourCC clears it) and w0 becomes
 *    "apch", so the stock compare takes the accept path for all four grades. w0 is not read
 *    again on that path (every path writes x0 first; tests/test_arm_time_s7_prores.py proves it
 *    statically). Every register but w0 (FULL, substituted) is as the stock call leaves it.
 *    PASS: exactly the stock call.
 *  2 0x4067C56C "bl 0x40676378" (playback quality label) -> bl g4_play_label. FULL with a
 *    recorded grade 1..3 and x1 pointing at the stock text "ProRes 422 HQ\0" (the caller's
 *    buffer at sp+0x128, built at 0x4067C4D0..0x4067C550): the 14 bytes become the grade's
 *    label. Then the stock call with x0..x7. (The old layout wrote caller sp+0x80: wrong.)
 *  3 0x40D22B7C "bl 0x40D220D4" (the first call after the movie type store 0x40D22AE4;
 *    the old record, the non-branch word 0x40D22A2C "cmp w8,#2", is stock again) ->
 *    bl g4_type, which ends by a tail "b 0x40D220D4" with every register as the call site left
 *    it. The old record turned type 0 (w8 = the u32 at 0xA03324D68) into the published grade g
 *    in 1..3, so the store 0x40D22AE4 wrote g instead of 0 into 0xA03324DF4 (the type field,
 *    +0x18, of the start block 0xA03324DDC); nothing else differed (0x40D22AE8 reloads x8,
 *    0x40D22AFC sets the flags again). This adapter makes the same store after the fact: in FULL,
 *    when this start took the type front end (mode word 0xA03324D6C == 0, 0xA03324D74 - 1 < 3,
 *    0xA03324D70 - 1 < 3: the path that reached 0x40D22A2C) it takes the anchor as the old record
 *    did, and when the type was 0 and the store ran (x8 == 0xA03324E00, left only by
 *    0x40D22B5C..0x40D22B68 on the paths through the store; the seven other branches into
 *    0x40D22B78 leave a 32-bit value in x8) and 0xA03324DF4 still reads 0, it stores g there.
 *    Between 0x40D22AE4 and here no instruction reads 0xA03324DF4, and its only reader in MAIN is
 *    the copy 0x40CB3D5C (via BL 0x40D22BF0, later on this path), so the copy sees what the old
 *    record gave it. The request words are re-read: they are written only by the start-post copy
 *    0x40D23F1C, whose one caller then waits for this callback's completion flag, and by the
 *    movie init 0x40D23A6C (tests/test_arm_time_s7_prores.py Static). PASS: the tail alone.
 *  4 0x40CEFD38 "bl 0x40CE5C60" (MOV sample description, x1 = 0x42B7145A, w2 = 0x56) ->
 *    bl g4_template_call. FULL with a published grade 1..3: the stock template is copied to
 *    the stack and edited as z63patch/prores.py built the old asset (byte 7 = n/s/o, byte 50 =
 *    name length, bytes 51..81 = the name, zero padded), the stock call takes the copy.
 *  5 0x40519A8C "bl 0x40399624" (shooting-menu codec label) -> bl g4_shoot_label. FULL with a
 *    published grade 1..3 and x1 == 0x42BEB148 ("ProRes 422 HQ", the key-4 entry of the stock
 *    table 0x4408A060): x1 = the grade's label (read-only data in this slot).
 * Shared sites (hooks in the core's merged adapters):
 *  - prores_copy (first): after the stock rate-row copy 0x40D223B4(input = x0, row = x1), a
 *    row equal to stock row k < 12 of 0x42B72828 gets its u64 rate at +16 scaled by 147/102/45
 *    over 220 (the old RATE_TABLES rule). Returns the grade applied, or the tier it received.
 *  - nctg_class (after G5): stock class 0 for file type 4 becomes 1 when the record names the
 *    grade HQ (the old Z63_PRORES_CLASS rule of native/h265_class.c).
 * State (G4, 8 B at page +0x8B0): byte 0 = 0x80 | playback grade, or 0. Written only by
 * record 1 in FULL. Bytes 4..7: the anchor value word.
 * Movie-view publication: record 3 runs once per recording start, inside the movie
 * dispatcher's start path (0x40D22770 bit 0, movie state 0, which sets movie state 1 at
 * 0x40D22D08 before it returns); on the type front end's path it is the anchor read: it takes the anchor hold (no
 * publication until 8 idle polls without an anchor read) and keeps its grade as the last anchor
 * value. The
 * two writers of that recording's file (record 4, the MOV sample description, and the NCTG
 * class hook) use the last anchor value, so the file agrees with the type and rate the
 * recording started with even if a publication lands before the file is written. The
 * publication callback z63p_g4 puts a ProRes row in the global rate row in line with a newly
 * published grade (z63p_g4 below). */
#include <stdint.h>
#include "../gate_api.h"
#include "../settings_bind.h"
#include "../sites/prores_copy.h"
#include "../sites/nctg_class.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;

#define G4 4u
#define FOURCC_HQ    0x61706368u    /* apch */
#define FOURCC_422   0x6170636eu    /* apcn */
#define FOURCC_LT    0x61706373u    /* apcs */
#define FOURCC_PROXY 0x6170636fu    /* apco */
#define TEMPLATE     0x42b7145aull  /* 86-byte stock sample description ("apch", "Apple ProRes 422 HQ") */
#define TEMPLATE_LEN 0x56u
#define SHOOT_LABEL  0x42beb148ull  /* "ProRes 422 HQ" */
#define RATE_TABLE   0x42b72828ull  /* 12 stock rows of 24 B; u64 rate at +16 */
#define RATE_ROWS    12u
#define PLAY_VALID   0x80u
#define HOLD         (Z63_STATE_G4+4u)   /* Anchor value word (gate_api.h): state bytes 4..7 */
#define GLOBAL_ROW   0xa03325c10ull      /* the rate row task 0x40D2518C copies at movie-subsystem start */

u64 stock_play_text(u64,u64,u64,u64,u64,u64,u64,u64);
u64 stock_template(u64,u64,u64,u64,u64,u64,u64,u64);
u64 stock_shoot_label(u64,u64,u64,u64,u64,u64,u64,u64);
u32 stock_be32(u64 at);
u32 g4_fourcc_c(u32 fourcc,u64 raw);
u32 g4_type_c(u32 value,u64 raw,u64 at);
void g4_type_post(u64 x8,u64 raw);
void stock_type_next(void);
u64 g4_play_label(u64,u64,u64,u64,u64,u64,u64,u64);
u64 g4_template_call(u64,u64,u64,u64,u64,u64,u64,u64);
u64 g4_shoot_label(u64,u64,u64,u64,u64,u64,u64,u64);

/* Labels: read-only, in the group's slot. Playback labels are 14 bytes, zero padded. */
static const char play_labels[4][14]={"ProRes 422 HQ","ProRes 422","ProRes 422 LT","ProRes Proxy"};
static const char shoot_labels[4][14]={"ProRes 422 HQ","ProRes 422","ProRes 422 LT","ProRes Proxy"};
static const char names[4][23]={"Apple ProRes 422 HQ","Apple ProRes 422","Apple ProRes 422 LT","Apple ProRes 422 Proxy"};
/* Rate scale (numerator over 220), by grade. */
static const u32 rate_num[4]={220,147,102,45};

/* The value the asm adapters loaded from P (the first access), checked as z63g_page() does. */
static inline u64 page_of(u64 p) {
    if(p<Z63_RAM_LO||p>=Z63_PAGE_HI||(p&0xfffu))return 0;
    if(z63g_ld32(p+Z63_GATE_OFF+Z63_G_MAGIC)!=Z63_GMAGIC||z63g_ld64(p+Z63_GATE_OFF+Z63_G_SELF)!=p)return 0;
    return p;
}

/* The ProRes field of a settings word exactly as z63_settings_decode (native/feature_settings.c
 * with Z63_SETTINGS_SHARED_V3, and z63_nv_decode of native/nv_quality.c) reads it, in registers
 * only (the shared reader returns through a pointer, a store the verifier counts outside the
 * frame): 0..3 when the word is a legacy, v2 or v3 record, else 0xff (empty or foreign). */
static inline u32 crc_byte(u32 crc,u32 byte) {
    crc^=byte;
    #pragma clang loop unroll(full)
    for(u32 b=0;b<8u;b++)crc=((crc<<1)^((crc&128u)?7u:0u))&255u;
    return crc;
}
static inline u32 prores_field(u32 word) {
    if(!(word>>8))return 0xffu;                                    /* empty */
    u32 v=(word>>16)&255u;
    if(((word>>8)&255u)==0x63u&&v<=3u&&(word>>24)==crc_byte(crc_byte(0,0x63u),v))return v;   /* legacy */
    u32 payload=(word>>8)&65535u,m=payload&0xe000u;
    if((m!=0xa000u&&m!=0xc000u)||!z63_og_term_ok(payload,crc_byte(crc_byte(0,payload&255u),payload>>8)^(word>>24)))return 0xffu;
    if(((payload>>2)&7u)>=5u||((payload>>8)&3u)>=3u)return 0xffu;    /* z63_settings_valid: h265 < 5, log < 3 */
    return payload&3u;
}

/* Inside an ACT hold: the published grade when the word is a record that names it, else
 * 0xff (an empty or foreign word, or a publication that stayed busy: pass through). */
static u32 held_grade(u64 p) {
    u64 e=z63g_effective(p);
    if(!e)return 0xffu;
    return prores_field((u32)e);
}

/* ---- record 1: FourCC parser, the BL 0x40CEB2C0 (bl 0x40CE5E04) ----
 * The old record 0x40CEB330 (mov w8,#0x6368) was not a branch word. The BL
 * before it reads the FourCC: 0x40CE5E04 is the frameless big-endian u32 reader (x0 = pointer,
 * w0 = value or 0 for a null pointer; it writes only x0, x8, x9, x10). The stock code then
 * reaches 0x40CEB330 exactly when (s32)w0 <= (s32)"aprg" and w0 != "NRAW" (0x40CEB2CC b.le,
 * 0x40CEB328 b.eq; no other entry into 0x40CEB2C4..0x40CEB334, w0 not written on the way). The
 * adapter makes the stock call, and on that path applies the old record's rule to the result: in
 * FULL the four ProRes 422 FourCCs put their grade in G4 state byte 0 (any other FourCC there
 * clears it) and 422/LT/Proxy become "apch", which stays on the same path (apch <= aprg, not
 * NRAW) and takes the accept branch. Every register the stock call leaves (x1..x17) is restored; only w0 is substituted, and NZCV (dead: 0x40CEB2C4..0x40CEB2CC
 * write w8 and compare before any flag read). PASS: exactly the stock call. */
__asm__(
".text\n"
".p2align 2\n"
".global g4_fourcc\n"
".type g4_fourcc,%function\n"
"g4_fourcc:\n"
"  sub sp,sp,#160\n"
"  str x30,[sp,#144]\n"
"  str x16,[sp,#128]\n"             /* x16 as the caller left it (the stock call keeps it) */
"  movz x16,#0xae20\n"
"  movk x16,#0x028c,lsl #16\n"
"  movk x16,#0xa,lsl #32\n"
"  ldar x16,[x16]\n"                /* the first access: P (read again below, after the stock call) */
"  bl stock_be32\n"
"  stp x0,x1,[sp]\n"
"  stp x2,x3,[sp,#16]\n"
"  stp x4,x5,[sp,#32]\n"
"  stp x6,x7,[sp,#48]\n"
"  stp x8,x9,[sp,#64]\n"
"  stp x10,x11,[sp,#80]\n"
"  stp x12,x13,[sp,#96]\n"
"  stp x14,x15,[sp,#112]\n"
"  str x17,[sp,#136]\n"
"  movz w1,#0x7267\n"               /* the path to 0x40CEB330: (s32)w0 <= "aprg" ... */
"  movk w1,#0x6170,lsl #16\n"
"  cmp w0,w1\n"
"  b.gt 9f\n"
"  movz w1,#0x4157\n"               /* ... and w0 != "NRAW" */
"  movk w1,#0x4e52,lsl #16\n"
"  cmp w0,w1\n"
"  b.eq 9f\n"
"  movz x16,#0xae20\n"
"  movk x16,#0x028c,lsl #16\n"
"  movk x16,#0xa,lsl #32\n"
"  ldar x1,[x16]\n"
"  bl g4_fourcc_c\n"                /* w0 = the FourCC; 1: accept as apch */
"  cbz w0,9f\n"
"  movz w0,#0x6368\n"
"  movk w0,#0x6170,lsl #16\n"
"  str x0,[sp]\n"
"9:ldp x0,x1,[sp]\n"
"  ldp x2,x3,[sp,#16]\n"
"  ldp x4,x5,[sp,#32]\n"
"  ldp x6,x7,[sp,#48]\n"
"  ldp x8,x9,[sp,#64]\n"
"  ldp x10,x11,[sp,#80]\n"
"  ldp x12,x13,[sp,#96]\n"
"  ldp x14,x15,[sp,#112]\n"
"  ldp x16,x17,[sp,#128]\n"
"  ldr x30,[sp,#144]\n"
"  add sp,sp,#160\n"
"  ret\n"
".size g4_fourcc,.-g4_fourcc\n"
);

u32 g4_fourcc_c(u32 fourcc,u64 raw) {
    u64 p=page_of(raw);
    if(!p||!z63g_phase(p,G4))return 0;
    u32 grade;
    switch(fourcc) {
    case FOURCC_HQ:grade=0;break;
    case FOURCC_422:grade=1;break;
    case FOURCC_LT:grade=2;break;
    case FOURCC_PROXY:grade=3;break;
    default:z63g_state(p,Z63_STATE_G4)[0]=0;return 0;
    }
    z63g_state(p,Z63_STATE_G4)[0]=(u8)(PLAY_VALID|grade);
    return grade!=0;
}

/* ---- record 3: the call after the movie type store, BL 0x40D22B7C (bl 0x40D220D4) ----
 * Every register is saved, x8 (the call site's: 0xA03324E00 exactly on the paths through the
 * store 0x40D22AE4) goes to g4_type_post, every register is restored and the stock callee
 * 0x40D220D4 is entered by a tail b with x30 = 0x40D22B80, so it runs and returns as stock
 * (it is a leaf: 32 bytes from 0xA03324008 to x0). NZCV is not kept (a call does not keep it;
 * the callee writes no flags and reads none). */
__asm__(
".text\n"
".p2align 2\n"
".global g4_type\n"
".type g4_type,%function\n"
"g4_type:\n"
"  sub sp,sp,#160\n"
"  stp x0,x1,[sp]\n"
"  stp x2,x3,[sp,#16]\n"
"  stp x4,x5,[sp,#32]\n"
"  stp x6,x7,[sp,#48]\n"
"  stp x8,x9,[sp,#64]\n"
"  stp x10,x11,[sp,#80]\n"
"  stp x12,x13,[sp,#96]\n"
"  stp x14,x15,[sp,#112]\n"
"  stp x16,x17,[sp,#128]\n"
"  str x30,[sp,#144]\n"
"  movz x16,#0xae20\n"
"  movk x16,#0x028c,lsl #16\n"
"  movk x16,#0xa,lsl #32\n"
"  ldar x1,[x16]\n"                 /* the first access: P */
"  mov x0,x8\n"
"  bl g4_type_post\n"
"  ldp x0,x1,[sp]\n"
"  ldp x2,x3,[sp,#16]\n"
"  ldp x4,x5,[sp,#32]\n"
"  ldp x6,x7,[sp,#48]\n"
"  ldp x8,x9,[sp,#64]\n"
"  ldp x10,x11,[sp,#80]\n"
"  ldp x12,x13,[sp,#96]\n"
"  ldp x14,x15,[sp,#112]\n"
"  ldp x16,x17,[sp,#128]\n"
"  ldr x30,[sp,#144]\n"
"  add sp,sp,#160\n"
"  b stock_type_next\n"             /* 0x40D220D4, the replaced target */
".size g4_type,.-g4_type\n"
);

#define TYPE_MODE   0xa03324d6cull   /* request +0x6C: 0 takes the type front end (0x40D22984, 0x40D229AC) */
#define TYPE_RANGE1 0xa03324d74ull   /* request +0x74: - 1 < 3 on that path (0x40D229C0..0x40D229CC) */
#define TYPE_RANGE2 0xa03324d70ull   /* request +0x70: - 1 < 3 on that path (0x40D229F0..0x40D22A00) */
#define TYPE_VALUE  0xa03324d68ull   /* request +0x68: the value 0x40D22A24 loads and 0x40D22A2C compares */
#define TYPE_OUT    0xa03324df4ull   /* start block +0x18: the store 0x40D22AE4 */
#define TYPE_PASSED 0xa03324e00ull   /* x8 after 0x40D22B5C..0x40D22B68 (only on the paths through 0x40D22AE4) */

/* The old record's rule, for a start that reached 0x40D22A2C with `value`: in FULL the anchor
 * read, and 0x100 | g when value is 0 and the published grade g is 1..3 (else 0). With
 * `at` nonzero (the store ran) and the word there still 0, g is stored there inside the hold. */
u32 g4_type_c(u32 value,u64 raw,u64 at) {
    u64 p=page_of(raw);
    if(!p||!z63g_act_enter(p,G4))return 0;
    u32 g=z63g_hold_anchor(p,HOLD,held_grade(p));    /* The recording's anchor read */
    u32 out=value==0&&g>=1&&g<=3?0x100u|g:0;
    if(out&&at&&z63g_ld32(at)==0)*(volatile u32 *)(uintptr_t)at=g;
    z63g_act_leave(p,G4);
    return out;
}

/* Did this start reach the type front end 0x40D22A2C? Exactly the stock tests on its path. */
void g4_type_post(u64 x8,u64 raw) {
    if(z63g_ld32(TYPE_MODE)!=0||z63g_ld32(TYPE_RANGE1)-1u>=3u||z63g_ld32(TYPE_RANGE2)-1u>=3u)return;
    g4_type_c(z63g_ld32(TYPE_VALUE),raw,x8==TYPE_PASSED?TYPE_OUT:0);
}

/* ---- record 2: playback label (BL -> BL) ---- */
u64 g4_play_label(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=z63g_page();
    if(p&&z63g_phase(p,G4)) {
        u32 s=z63g_state(p,Z63_STATE_G4)[0];
        u32 g=s&3u;
        if((s&PLAY_VALID)&&g&&a1) {
            volatile u8 *b=(volatile u8 *)(uintptr_t)a1;
            u32 diff=0;
            #pragma clang loop unroll(full)
            for(u32 i=0;i<14u;i++)diff|=(u32)b[i]^(u8)play_labels[0][i];
            if(!diff) {
                #pragma clang loop unroll(full)
                for(u32 i=0;i<14u;i++)b[i]=(u8)play_labels[g][i];
            }
        }
    }
    return stock_play_text(a0,a1,a2,a3,a4,a5,a6,a7);
}

/* ---- record 4: MOV sample description (BL -> BL, stack copy) ---- */
u64 g4_template_call(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=z63g_page();
    if(!p||a1!=TEMPLATE||(u32)a2!=TEMPLATE_LEN||!z63g_act_enter(p,G4))
        return stock_template(a0,a1,a2,a3,a4,a5,a6,a7);
    u32 g=z63g_hold_last(p,HOLD,held_grade(p));       /* The grade the recording started with */
    u16 copy[48];
    if(g>=1&&g<=3) {
        const volatile u16 *t=(const volatile u16 *)(uintptr_t)TEMPLATE;
        #pragma clang loop unroll(full)
        for(u32 i=0;i<43u;i++)copy[i]=t[i];
        u8 *b=(u8 *)copy;
        b[7]=(u8)(g==1?'n':g==2?'s':'o');                  /* apcn / apcs / apco */
        b[50]=(u8)(g==1?16:g==2?19:22);                    /* strlen of the name */
        #pragma clang loop unroll(full)
        for(u32 i=0;i<31u;i++)b[51+i]=i<23u?(u8)names[g][i]:0;
    }
    z63g_act_leave(p,G4);
    if(g>=1&&g<=3)return stock_template(a0,(u64)(uintptr_t)copy,a2,a3,a4,a5,a6,a7);
    return stock_template(a0,a1,a2,a3,a4,a5,a6,a7);
}

/* ---- record 5: shooting-menu codec label (BL -> BL) ---- */
u64 g4_shoot_label(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=z63g_page();
    if(p&&a1==SHOOT_LABEL&&z63g_act_enter(p,G4)) {
        u32 g=held_grade(p);
        if(g>=1&&g<=3)a1=(u64)(uintptr_t)shoot_labels[g];
        z63g_act_leave(p,G4);
    }
    return stock_shoot_label(a0,a1,a2,a3,a4,a5,a6,a7);
}

/* ---- site prores_copy: the rate row after the stock copy 0x40D223B4(x0 request, x1 row)
 * (sites/prores_copy.h): reads nothing from the request, rewrites the row. ---- */
u32 z63h_prores_copy_g4(u64 page,u32 phase,void *request,void *out,u32 applied) {
    (void)phase;(void)request;
    volatile u64 *row=(volatile u64 *)out;        /* x1: the 24-byte row the stock copy wrote */
    if(!row||!z63g_act_enter(page,G4))return applied;
    u32 g=held_grade(page);
    if(g>=1&&g<=3) {
        u64 r0=row[0],r1=row[1],r2=row[2];
        const volatile u64 *t=(const volatile u64 *)(uintptr_t)RATE_TABLE;
        u32 hit=0;
        #pragma clang loop unroll(full)
        for(u32 k=0;k<RATE_ROWS;k++)hit|=(t[3*k]==r0&&t[3*k+1]==r1&&t[3*k+2]==r2);
        if(hit) {
            u64 den=220;
            row[2]=((r2*rate_num[g]+(den<<19))/(den<<20))<<20;
            applied=g;
        }
    }
    z63g_act_leave(page,G4);
    return applied;
}

/* ---- site nctg_class: class 0 -> 1 for ProRes 422 when the record names HQ ---- */
static u32 b32(const volatile u8 *b){return (u32)b[0]|(u32)b[1]<<8|(u32)b[2]<<16|(u32)b[3]<<24;}
u32 z63h_nctg_class_g4(u64 page,u32 phase,void *request,void *out,u32 result) {
    (void)phase;
    const volatile u8 *q=(const volatile u8 *)request;
    volatile u8 *o=(volatile u8 *)out;
    if(result!=1||!q||!o||q[0xec]||b32(q+0x20)!=4u||b32(o+0xa8)!=0)return result;
    if(!z63g_act_enter(page,G4))return result;
    if(z63g_hold_last(page,HOLD,held_grade(page))==0) {o[0xa8]=1;o[0xa9]=0;o[0xaa]=0;o[0xab]=0;}
    z63g_act_leave(page,G4);
    return result;
}

/* ---- publication callback (Group.publish) ----
 * The global rate row 0xA03325C10 is copied (through prores_copy) at movie-subsystem start. When
 * it holds ProRes stock row k (first 16 bytes) with the rate of stock row k or of one of its three
 * scaled grades, its u64 rate at +16 becomes the rate the copy hook gives for the newly published
 * grade (stock for HQ or a word that names no grade). One bounded CAS; 1 when it lost. Anything
 * else is left alone. (No constant-address read of +16 was found in MAIN; this keeps the cache
 * consistent with the word, as the hook would have made it.) */
static inline u64 scaled(u64 r,u32 g) {
    u64 den=220;
    return g>=1&&g<=3?((r*rate_num[g]+(den<<19))/(den<<20))<<20:r;
}
static u32 g4_cas64(u64 a,u64 old,u64 next) {
    u64 v;u32 s,t,r;
    __asm__ volatile(
        "mov %w[t],#64\n"
        "1: ldaxr %[v],[%[a]]\n"
        "cmp %[v],%[o]\n"
        "b.ne 3f\n"
        "stlxr %w[s],%[n],[%[a]]\n"
        "cbz %w[s],2f\n"
        "subs %w[t],%w[t],#1\n"
        "b.ne 1b\n"
        "3: clrex\n"
        "mov %w[r],#0\n"
        "b 4f\n"
        "2: mov %w[r],#1\n"
        "4:\n"
        :[v]"=&r"(v),[s]"=&r"(s),[t]"=&r"(t),[r]"=&r"(r):[a]"r"(a),[o]"r"(old),[n]"r"(next):"memory","cc");
    return r;
}
u32 z63p_g4(u64 page,u32 word) {
    (void)page;
    u32 g=prores_field(word);
    const volatile u64 *row=(const volatile u64 *)(uintptr_t)GLOBAL_ROW;
    const volatile u64 *t=(const volatile u64 *)(uintptr_t)RATE_TABLE;
    u64 r0=row[0],r1=row[1],r2=row[2],from=0,want=0;
    u32 hit=0;
    for(u32 k=0;k<RATE_ROWS;k++) {          /* bounded: 12 rows; the 16-byte prefixes are unique */
        if(t[3*k]!=r0||t[3*k+1]!=r1)continue;
        u64 s=t[3*k+2];
        hit=r2==s||r2==scaled(s,1)||r2==scaled(s,2)||r2==scaled(s,3);
        from=r2;want=scaled(s,g);
        break;
    }
    if(!hit||want==from)return 0;
    return g4_cas64(GLOBAL_ROW+16u,from,want)?0u:1u;
}
