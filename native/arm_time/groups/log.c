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
/* G8, log recording on the arm-time gate.
 * Declared by z63patch/gate_groups/log.py; one translation unit linked alone at the group's fixed
 * range, so its bytes do not depend on the composition. Ports the old-layout units
 * native/log_record.c/.S, native/log_keys.S and native/log_lv_icon.S (the Z8 patch's producer
 * substitution, NCTG tag and name, ProRes 422 colour key, live-view icon) and adds the tone-page
 * relabel.
 *
 * Log type: the published settings word (z63g_effective) decoded as a v2/v3 record, log field
 * 1 (S-Log3 / S-Gamut3.Cine) or 2 (Log3G10 / REDWideGamutRGB); anything else is 0 (stock). Every
 * read sits inside one ACT hold of group 8. PASS (no page, phase not LIVE/DRAINING, G8 broken or
 * not selected, publication busy past the bounded wait): exactly the stock word's effect. An
 * integrity break passes through from the next call (no latch).
 *
 * Producers. M1 0x4111CD60 and M2 0x4111D618 run as build-time clones only while the gate
 * holds ACT for a LIVE/DRAINING G8 with a nonzero type; otherwise the stock function. The clone
 * is stock but for one word: M1 +0x3CC (movk x13, the base row) is a BL to z63_g8_m1_site; M2
 * +0x708 (movk x9; M2 is a frameless leaf) is a B to z63_g8_m2_site, which returns by B to the
 * clone's +0x70C. RGB: the first row copy BL 0x411272D0 after the old value site 0x4112729C
 * (movk x10, a non-branch word) rebases the copy and the three callee-saved row
 * bases (x22, x19, x20) onto the custom rows when they are rows 0..2 of the stock base, which
 * happens exactly when w8 == 3 (the old site's rule). NLG is re-sited onto the two
 * memcpy BLs 0x411216B4 / 0x411258D4 (source swap of the N-Log row 0xA029EA808, which those
 * sites copy exactly when the old madd site's w9 is 1). CPXS: the BL 0x41121610 to 0x4111C854;
 * the custom table is stored at [x19+0xB0] after the stock call, as the old adapter did at
 * 0x41121614 (mov x0,x19, which follows the call).
 *
 * Movie-view publication. The producers (M1, M2, the three base sites, NLG,
 * CPXS) are anchor reads: the type they use is held (no publication until 8 consecutive idle
 * polls with no anchor read, or at once under Q) and kept as the last anchor value. The file
 * writers (NCTG tag and name, colour key copy, key count) use that last anchor value, so a
 * recording's metadata agrees with the curve it was produced with. The file-type labels, the
 * live-view icon and the tone-page refresh read the published word directly (display only). */
#include <stdint.h>
#include "../gate_api.h"
#include "../settings_bind.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;

#define G8 8u
#define G3 3u
#define HOLD (Z63_STATE_G8+0u)   /* Anchor value word (gate_api.h) */
#ifndef Z63_G8_SLOT
#error "the text slot comes from the declaration (TEXT_SLOTS registry)"
#endif
#ifndef Z63_G8_LABEL_WIDGET
#define Z63_G8_LABEL_WIDGET 0x4dc0u          /* NE: the tone row's label widget id (see the module) */
#endif
#define NLG_ROW1 0xa029ea808ull                /* stock NLG row 1 (N-Log), 229 u16 */
#define NLG_BYTES 0x1cau
#define CC_STOCK 0x441ba681ull                 /* "com.nikon.bt2020.n-log", 22 bytes */
#define CC_STOCK_BYTES 22u
#define CC_SLOG3_BYTES 20u

/* The Z8 LG_DATA tables (z63patch/log_record.py), read-only and resident in the G8 asset. */
__attribute__((section(".z63asset.401b4000"),used,aligned(16))) const u8 z63_g8_tables[0x1500]={
#include "log_tables.inc"
};
/* Offsets per part and type (1, 2), as native/log_record.c. */
#define T_RGB(t)  ((t)==1?0x0000u:0x0600u)
#define T_NLG(t)  ((t)==1?0x0c00u:0x0e00u)
#define T_M2(t)   ((t)==1?0x1000u:0x1020u)
#define T_M1(t)   ((t)==1?0x1040u:0x1060u)
#define T_CPXS(t) ((t)==1?0x1100u:0x1300u)
static inline u64 table(u32 offset){return (u64)(uintptr_t)z63_g8_tables+offset;}

u64 stock_m1(u64,u64,u64,u64,u64,u64,u64,u64);
u64 stock_m2(u64,u64,u64,u64,u64,u64,u64,u64);
u64 z63_clone_m1(u64,u64,u64,u64,u64,u64,u64,u64);
u64 z63_clone_m2(u64,u64,u64,u64,u64,u64,u64,u64);
void *stock_memcpy(void *out,const void *input,u64 bytes);
u64 stock_tone_tag(u64,u64,u64,u64,u64,u64,u64,u64);
u64 stock_text_store8(u64,u64,u64,u64,u64,u64,u64,u64);
u64 stock_pc_name(u64,u64,u64,u64,u64,u64,u64,u64);
void *stock_row_value(void *window,u32 item,u32 widget);
void stock_text_store(const char *text,u32 slot,u32 style);
void stock_text_bind(void *widget,u32 slot);

/* The log field of a settings word exactly as native/feature_settings.c (Z63_SETTINGS_SHARED_V3)
 * decodes it: a v2 (0xa000) or v3 (0xc000) record with its CRC and valid fields, log 1 or 2;
 * else 0. Written out here (no pointer out-argument) so the wrappers store nothing outside their
 * frame. A legacy ProRes record (byte 1 = 0x63, byte 2 <= 3) never carries a v2/v3 marker in
 * byte 2, so the decoder's legacy test changes nothing here. */
static inline u32 g8_log_field(u32 word) {
    u32 payload=(word>>8)&0xffffu,mark=payload&0xe000u,crc=0;
    if(!payload||(mark!=0xa000u&&mark!=0xc000u))return 0;
#pragma clang loop unroll(full)
    for(u32 i=0;i<2;i++) {
        crc^=(payload>>(i*8))&255u;
#pragma clang loop unroll(full)
        for(u32 b=0;b<8;b++)crc=((crc<<1)^((crc&128u)?7u:0u))&255u;
    }
    if(!z63_og_term_ok(payload,crc^(word>>24))||((payload>>2)&7u)>=5u)return 0;   /* Check terms */
    u32 log=(payload>>8)&3u;
    return log==1||log==2?log:0;
}
/* The type for a caller holding ACT[G8]: 0, 1 or 2. */
static inline u32 g8_type(u64 p) {
    u64 e=z63g_effective(p);
    return e?g8_log_field((u32)e):0;
}
/* An anchor read (takes or touches the hold, records the type) and a follower read (the
 * last anchor value). */
static u32 __attribute__((noinline)) g8_anchor(u64 p){return z63g_hold_recording(p,HOLD,g8_type(p));}
static u32 __attribute__((noinline)) g8_last(u64 p){return z63g_hold_last(p,HOLD,g8_type(p));}
/* One ACT hold around one read of the type; 0 in PASS. mode 0 fresh, 1 anchor, 2 follower. */
static u32 __attribute__((noinline)) g8_type_once(u32 mode) {
    u64 p=z63g_page();
    if(!p||!z63g_act_enter(p,G8))return 0;
    u32 t=mode==1?g8_anchor(p):mode==2?g8_last(p):g8_type(p);
    z63g_act_leave(p,G8);
    return t;
}

/* ---- R1-R4: the producer call sites (BL -> BL). The clone runs inside one ACT hold. ---- */
u64 z63_g8_m1(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h);
u64 z63_g8_m1(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h) {
    u64 p=z63g_page();
    if(!p||!z63g_act_enter(p,G8))return stock_m1(a,b,c,d,e,f,g,h);
    u64 r=g8_anchor(p)?z63_clone_m1(a,b,c,d,e,f,g,h):stock_m1(a,b,c,d,e,f,g,h);
    z63g_act_leave(p,G8);
    return r;
}
u64 z63_g8_m2(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h);
u64 z63_g8_m2(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h) {
    u64 p=z63g_page();
    if(!p||!z63g_act_enter(p,G8))return stock_m2(a,b,c,d,e,f,g,h);
    u64 r=g8_anchor(p)?z63_clone_m2(a,b,c,d,e,f,g,h):stock_m2(a,b,c,d,e,f,g,h);
    z63g_act_leave(p,G8);
    return r;
}

/* ---- value helpers called by the asm adapters below (their own lookup and hold) ---- */
/* M1 clone site: x13 = base (stock 0xA029E8E06); w12 == 1 selects the N-Log row 2. */
u64 z63_g8_m1_base(u64 base,u32 w12);
u64 z63_g8_m1_base(u64 base,u32 w12) {
    if(w12!=1)return base;
    u32 t=g8_type_once(1);
    return t?table(T_M1(t))-0x24u:base;
}
/* M2 clone site: x9 = base (stock 0xA029EDA64); w10 == 1 selects the N-Log row 1. */
u64 z63_g8_m2_base(u64 base,u32 w10);
u64 z63_g8_m2_base(u64 base,u32 w10) {
    if(w10!=1)return base;
    u32 t=g8_type_once(1);
    return t?table(T_M2(t))-0x12u:base;
}
/* RGB: 0x41127250 builds x22/x19/x20 = base + row * 0x1CA (base 0xA029E93CC, rows w9, w12, w8
 * from w8 = 3 x tone) and copies x22, x19, x20, x22 with four memcpy BLs; w8 == 3 gives rows 0, 1,
 * 2 and is the only w8 that does (every other w8 >= 6 gives rows >= 3). At the first copy BL
 * 0x411272D0: rows 0..2 of the stock base -> the custom table (the old site's x10), else 0. */
#define RGB_BASE 0xa029e93ccull
#define RGB_ROW 0x1cau
u64 z63_g8_rgb_rows(u64 r0,u64 r1,u64 r2,u64 src);
u64 z63_g8_rgb_rows(u64 r0,u64 r1,u64 r2,u64 src) {
    if(r0!=RGB_BASE||r1!=RGB_BASE+RGB_ROW||r2!=RGB_BASE+2u*RGB_ROW||src!=r0)return 0;
    u32 t=g8_type_once(1);
    return t?table(T_RGB(t)):0;
}
/* File-type labels: each label block of 0x40590218 builds its text at the caller's sp and passes
 * it to TEXT_STORE (x0 = sp). "N-Log\0" there is the block's N-Log path and nothing else (the other
 * paths store "SDR\0", "HLG\0" or "HLG*" as the first word); LIVE with a type it becomes "Log\0"
 * (bytes 4-5 keep "g\0", as the old movk site left them). Then the stock call, all eight registers. */
u64 z63_g8_label(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h);
u64 z63_g8_label(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h) {
    u64 p=z63g_page();
    if(p&&a&&!(a&3u)) {
        volatile u32 *text=(volatile u32 *)(uintptr_t)a;
        if(text[0]==0x6f4c2d4eu&&*(volatile u16 *)(uintptr_t)(a+4u)==0x0067u&&g8_type_once(0))text[0]=0x00676f4cu;
    }
    return stock_text_store8(a,b,c,d,e,f,g,h);
}
/* Key count (stock cinc w24,w8,eq at 0x406F4EEC): with tone N-Log (eq) and Log3G10, two keys (no colour
 * key), as the Z8 patch. */
u32 z63_g8_keycount_rule(u32 nlog);
u32 z63_g8_keycount_rule(u32 nlog) {
    return nlog&&g8_type_once(2)==2;
}
/* The S-Log3 colour-key value, 20 bytes, no terminator (Z8 LG_CC_SLOG3). */
static const char slog3_key[CC_SLOG3_BYTES]={'c','o','m','.','s','o','n','y','.','s','-','l','o','g','3','-','c','i','n','e'};
/* Next offset (stock add w8,w29,#0x2e at 0x406F54AC): decided by what the copy before it wrote at
 * x27 + w29 + 0x18 (the value area), so the two words always agree (two bytes shorter only
 * when the S-Log3 key was copied, which the stock copy never writes). */
u32 z63_g8_cc_next_value(const volatile u8 *base,u32 w29);
u32 z63_g8_cc_next_value(const volatile u8 *base,u32 w29) {
    u64 p=z63g_page();
    if(!p)return w29+0x2eu;
    const volatile u8 *v=base+(u32)(w29+0x18u);
    u32 diff=0;
#pragma clang loop unroll(full)
    for(u32 i=0;i<CC_SLOG3_BYTES;i++)diff|=(u32)v[i]^(u32)(u8)slog3_key[i];
    return diff?w29+0x2eu:w29+0x2cu;
}
/* Live-view icon: the type when G8 and G3 (whose readers resolve the private icons) are both
 * LIVE/DRAINING. */
u32 z63_g8_lv_icon_type(void);
u32 z63_g8_lv_icon_type(void) {
    u64 p=z63g_page();
    if(!p||!z63g_phase(p,G3)||!z63g_act_enter(p,G8))return 0;
    u32 t=g8_type(p);
    z63g_act_leave(p,G8);
    return t;
}

/* ---- C-class wrappers of call-swap and data records ---- */
/* R5 CPXS: the stock call, then the custom table pointer into the generator context. */
void z63_g8_cpxs_after(volatile u8 *ctx);
void z63_g8_cpxs_after(volatile u8 *ctx) {
    u64 p=z63g_page();
    if(!p||!z63g_act_enter(p,G8))return;
    u32 t=g8_anchor(p);
    if(t) {
        u64 tone=*(volatile u64 *)(ctx+0x98);
        if(*(volatile u8 *)(uintptr_t)(tone+0x31)==1)*(volatile u64 *)(ctx+0xb0)=table(T_CPXS(t));
    }
    z63g_act_leave(p,G8);
}
/* NLG (memcpy BLs 0x411216B4 / 0x411258D4): the N-Log row's copy takes the custom curve. */
void *z63_g8_nlg(void *out,const void *input,u64 bytes);
void *z63_g8_nlg(void *out,const void *input,u64 bytes) {
    u64 p=z63g_page();
    if(p&&input==(const void *)(uintptr_t)NLG_ROW1&&bytes==NLG_BYTES&&z63g_act_enter(p,G8)) {
        u32 t=g8_anchor(p);
        if(t)input=(const void *)(uintptr_t)table(T_NLG(t));
        z63g_act_leave(p,G8);
    }
    return stock_memcpy(out,input,bytes);
}
/* R13 colour-key copy (memcpy BL 0x406F54A8): the S-Log3 key instead of the N-Log key. */
void *z63_g8_cc_copy(void *out,const void *input,u64 bytes);
void *z63_g8_cc_copy(void *out,const void *input,u64 bytes) {
    u64 p=z63g_page();
    if(p&&input==(const void *)(uintptr_t)CC_STOCK&&bytes==CC_STOCK_BYTES&&z63g_act_enter(p,G8)) {
        if(g8_last(p)==1) {input=slog3_key;bytes=CC_SLOG3_BYTES;}
        z63g_act_leave(p,G8);
    }
    return stock_memcpy(out,input,bytes);
}
/* R6 NCTG tone tag (data word 0xA029935B0, stock leaf 0x40707108(descriptor, record)): the leaf,
 * then tag 2 (N-Log) with file class [descriptor+0x20] below 5 becomes 1 (the Rec.709 tag). */
u64 z63_g8_tag(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h);
u64 z63_g8_tag(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h) {
    u64 p=z63g_page();
    u64 r=stock_tone_tag(a,b,c,d,e,f,g,h);
    if(!p||!z63g_act_enter(p,G8))return r;
    volatile u32 *tag=(volatile u32 *)(uintptr_t)(b+0xb0);
    if(*tag==2&&*(volatile u32 *)(uintptr_t)(a+0x20)<5&&g8_last(p))*tag=1;
    z63g_act_leave(p,G8);
    return r;
}
/* R7 NCTG picture-control name (data word 0xA02993F70, stock writer 0x40707EA8(descriptor,
 * record)): the writer, then for descriptor tone 2 the 20-byte name at record +0x358+8. Byte
 * stores (the block is only known to be 4-byte aligned on the Z8). */
u64 z63_g8_name(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h);
u64 z63_g8_name(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h) {
    static const char names[2][8]={"S-LOG3","LOG3G10"};
    u64 p=z63g_page();
    u64 r=stock_pc_name(a,b,c,d,e,f,g,h);
    if(!p||!a||!b||!z63g_act_enter(p,G8))return r;
    const volatile u8 *dsc=(const volatile u8 *)(uintptr_t)(a+0x30);
    u32 tone=(u32)dsc[0]|(u32)dsc[1]<<8|(u32)dsc[2]<<16|(u32)dsc[3]<<24;
    u32 t=tone==2?g8_last(p):0;
    if(t) {
        volatile u8 *name=(volatile u8 *)(uintptr_t)(b+0x358+8);
#pragma clang loop unroll(full)
        for(u32 i=0;i<20;i++)name[i]=i<8?(u8)names[t-1][i]:0;
    }
    z63g_act_leave(p,G8);
    return r;
}
/* R16 tone page refresh (data word 0xA028F5998, stock 0x40590D70 = ret). The type's name goes
 * into the G8 text slot ("N-Log" for type 0, so a label bound earlier reads stock again), and the
 * label of each row whose recording the type changes (ProRes 422 HQ 0x30FC, H.265 10-bit
 * 0x30FF; not N-RAW 0x3187 or ProRes RAW 0x3189) is bound to it. PASS: return at once. */
void z63_g8_tone_refresh(void *window);
void z63_g8_tone_refresh(void *window) {
    static const char n_log[]="N-Log";
    static const char s_log3[]="S-Log3";
    static const char log3g10[]="Log3G10";
    u64 p=z63g_page();
    if(!p||!z63g_act_enter(p,G8))return;
    u32 t=g8_type(p);
    stock_text_store(t==1?s_log3:t==2?log3g10:n_log,Z63_G8_SLOT,4);
    if(t) {
        void *w=stock_row_value(window,0x30fcu,Z63_G8_LABEL_WIDGET);
        if(w)stock_text_bind(w,Z63_G8_SLOT);
        w=stock_row_value(window,0x30ffu,Z63_G8_LABEL_WIDGET);
        if(w)stock_text_bind(w,Z63_G8_SLOT);
    }
    z63g_act_leave(p,G8);
}

/* ---- asm adapters: value sites keep every general register but the one they produce. The
 * frame holds x0-x17, x29, x30 (0xa0 bytes); NZCV is dead after every site (checked on the
 * stock code, all paths to the next flag write) and is not restored (no MSR in group code). A
 * record adapter's first access outside the stack is the acquire load of P (the C helper then
 * does the full lookup). ---- */
__asm__(
".macro g8_save\n"
"  sub sp,sp,#0xa0\n"
"  stp x0,x1,[sp,#0x00]\n  stp x2,x3,[sp,#0x10]\n  stp x4,x5,[sp,#0x20]\n  stp x6,x7,[sp,#0x30]\n"
"  stp x8,x9,[sp,#0x40]\n  stp x10,x11,[sp,#0x50]\n  stp x12,x13,[sp,#0x60]\n  stp x14,x15,[sp,#0x70]\n"
"  stp x16,x17,[sp,#0x80]\n  stp x29,x30,[sp,#0x90]\n"
".endm\n"
".macro g8_restore\n"
"  ldp x0,x1,[sp,#0x00]\n  ldp x2,x3,[sp,#0x10]\n  ldp x4,x5,[sp,#0x20]\n  ldp x6,x7,[sp,#0x30]\n"
"  ldp x8,x9,[sp,#0x40]\n  ldp x10,x11,[sp,#0x50]\n  ldp x12,x13,[sp,#0x60]\n  ldp x14,x15,[sp,#0x70]\n"
"  ldp x16,x17,[sp,#0x80]\n  ldp x29,x30,[sp,#0x90]\n"
"  add sp,sp,#0xa0\n"
".endm\n"
".macro g8_p\n"                       /* the acquire load of P, first access outside the stack */
"  movz x16,#0xae20\n  movk x16,#0x028c,lsl #16\n  movk x16,#0xa,lsl #32\n  ldar x16,[x16]\n"
".endm\n"
".text\n"
/* M1 clone +0x3CC (BL from the clone; x30 is saved by M1's prologue). */
".p2align 2\n.global z63_g8_m1_site\n.type z63_g8_m1_site,%function\n"
"z63_g8_m1_site:\n"
"  movk x13,#0,lsl #48\n"
"  g8_save\n"
"  mov x0,x13\n  mov w1,w12\n  bl z63_g8_m1_base\n  str x0,[sp,#0x68]\n"
"  g8_restore\n  ret\n"
".size z63_g8_m1_site,.-z63_g8_m1_site\n"
/* M2 clone +0x708 (B from the frameless leaf: x30 is M2's return address and is kept). */
".p2align 2\n.global z63_g8_m2_site\n.type z63_g8_m2_site,%function\n"
"z63_g8_m2_site:\n"
"  movk x9,#0,lsl #48\n"
"  g8_save\n"
"  mov x0,x9\n  mov w1,w10\n  bl z63_g8_m2_base\n  str x0,[sp,#0x48]\n"
"  g8_restore\n  b z63_clone_m2_70c\n"
".size z63_g8_m2_site,.-z63_g8_m2_site\n"
/* RGB: the first row copy 0x411272D0 (bl memcpy; x0 = out, x1 = x22, x2 = 0x1CA). The memcpy
 * runs once with x0-x2 as given, or with x1 = the custom row 0; after it x22/x19/x20 (callee-saved,
 * read by the next three copies at 0x411272D8/E8/FC) are the custom rows 0/1/2. x9-x15 are not
 * read after the call before they are written (static guard in the tests). */
".p2align 2\n.global z63_g8_rgb\n.type z63_g8_rgb,%function\n"
"z63_g8_rgb:\n"
"  stp x29,x30,[sp,#-0x30]!\n  mov x29,sp\n"
"  stp x0,x1,[sp,#0x10]\n  str x2,[sp,#0x20]\n"
"  g8_p\n"
"  mov x3,x1\n  mov x0,x22\n  mov x1,x19\n  mov x2,x20\n  bl z63_g8_rgb_rows\n"
"  ldr x2,[sp,#0x20]\n"
"  cbz x0,1f\n"
"  mov x22,x0\n  add x19,x0,#0x1ca\n  add x20,x0,#0x394\n"
"  ldr x0,[sp,#0x10]\n  mov x1,x22\n  b 2f\n"
"1:\n  ldp x0,x1,[sp,#0x10]\n"
"2:\n  bl stock_memcpy\n"
"  ldp x29,x30,[sp],#0x30\n  ret\n"
".size z63_g8_rgb,.-z63_g8_rgb\n"
/* Key count: the B 0x406F4EF0 (b 0x406F4F04) right after "cmp w8,#2; mov w8,#2; cinc w24,w8,eq"
 * (old site 0x406F4EEC, stays stock): the flags of that cmp are still set (eq: tone N-Log), w24
 * holds the stock count; two keys for Log3G10 under N-Log, then the stock target. */
".p2align 2\n.global z63_g8_keycount\n.type z63_g8_keycount,%function\n"
"z63_g8_keycount:\n"
"  g8_save\n"
"  cset w0,eq\n"
"  g8_p\n"
"  bl z63_g8_keycount_rule\n"
"  cbz w0,1f\n  mov w24,#2\n"
"1:\n  g8_restore\n  b stock_keys_f04\n"
".size z63_g8_keycount,.-z63_g8_keycount\n"
/* Next offset: the B 0x406F54B0 (b 0x406F557C) right after "add w8,w29,#0x2e" (old site
 * 0x406F54AC, stays stock): w8 again from x27/w29 (unchanged since the add), then the target. */
".p2align 2\n.global z63_g8_cc_next\n.type z63_g8_cc_next,%function\n"
"z63_g8_cc_next:\n"
"  g8_save\n  g8_p\n"
"  mov x0,x27\n  mov w1,w29\n  bl z63_g8_cc_next_value\n  mov w0,w0\n  str x0,[sp,#0x40]\n"
"  g8_restore\n  b stock_keys_57c\n"
".size z63_g8_cc_next,.-z63_g8_cc_next\n"
/* CPXS 0x41121610 (BL -> BL): w0, x1-x3 pass to 0x4111C854 untouched; its x0 is returned. */
".p2align 2\n.global z63_g8_cpxs\n.type z63_g8_cpxs,%function\n"
"z63_g8_cpxs:\n"
"  stp x29,x30,[sp,#-0x20]!\n  mov x29,sp\n"
"  g8_p\n"
"  bl stock_cpxs\n  str x0,[sp,#0x10]\n"
"  mov x0,x19\n  bl z63_g8_cpxs_after\n"
"  ldr x0,[sp,#0x10]\n  ldp x29,x30,[sp],#0x20\n  ret\n"
".size z63_g8_cpxs,.-z63_g8_cpxs\n"
/* LV icon 0x404310A8 (b 0x404311A8 -> b here): w3/w4 = the private fill/outline pair. */
".p2align 2\n.global z63_g8_lv_icon\n.type z63_g8_lv_icon,%function\n"
"z63_g8_lv_icon:\n"
"  g8_save\n  g8_p\n"
"  bl z63_g8_lv_icon_type\n"
"  cbz w0,1f\n"
"  lsl w0,w0,#1\n  mov w1,#0x7013\n  add w0,w0,w1\n  str x0,[sp,#0x18]\n"
"  add w0,w0,#1\n  str x0,[sp,#0x20]\n"
"1:\n  g8_restore\n  b stock_lv_icon_tail\n"
".size z63_g8_lv_icon,.-z63_g8_lv_icon\n"
);
