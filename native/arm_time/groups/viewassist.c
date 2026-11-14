// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 NikonFirmwarePatch contributors
// SPDX-FileCopyrightText: 2026 RdWing
/**
 * z63patch - Nikon Z6III Modifications
 * Adapted from NikonFirmwarePatch (GPL-3.0-only); see NOTICE.md.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * @package z63patch
 * @license GPLv3 License (https://www.gnu.org/licenses/gpl-3.0.html)
 *
 *   Copyright (C) 2026 NikonFirmwarePatch contributors
 *   Copyright (C) 2026 RdWing (modifications)
 *
 */
/* G9, Log view assist per panel and Log playback on the arm-time gate (group id 9).
 * Declared by
 * z63patch/gate_groups/viewassist.py; one translation unit linked alone at 0x401D8000, so its
 * bytes do not depend on the composition. Ports native/log_view_assist.S / .c and
 * native/log_playback.S (old layout) onto the gate.
 *
 * Log type. va_type(): while a Log clip is in the GUI playback info (state byte +0 holds 0x80 |
 * clip type), the clip's type; otherwise the Log type of the published settings word (G8's
 * setting, decoded with the shared v2/v3 reader) when G8 is selected, not broken and LIVE or
 * DRAINING; otherwise 0 (stock N-Log assist). It is read only with ACT[9 & 3] held, so the
 * published word is one word per hold (the group interface rule).
 *
 * Records (all A class, BL->BL or B->B, restore under Q as the default):
 *  - selects 0x402D8F7C (b 0x402D8ED0, matrix, mode 1) and 0x402D91D0 (b 0x402D9060, curves,
 *    mode 1): B->B adapters that save x0-x17 and x30, make the acquire load of P their first
 *    access, ask z63_g9_table() for the type's pointer table and, when it gives one, replace the
 *    saved x10 (matrix) and x12 (curve B = x10 + 0x40); then restore and take the stock branch.
 *    x11 (curve A) stays stock. The stock code indexes the table by the current index [IX] and
 *    the display variant word 0xA028104D0 (0 rear monitor, 1 viewfinder); the patch tables hold
 *    the patch records for index 0 and the stock records for index 1, as in the old layout.
 *    z63_g9_table() gives 0 (stock) unless the index is <= 1 and the variant <= 1, so the stock
 *    code can never index past the 4-entry table. NZCV is not preserved (the verifier refuses
 *    msr nzcv); it is dead at both targets (the next flag reader follows a flag writer or a
 *    call on every path, G9 test).
 *  - cache calls 0x402D4D38, 0x402DF580 (bl 0x402D4E24, A) and 0x402DA7A4, 0x402DB6FC
 *    (bl 0x402DDB30, B): C wrappers. A and B skip their upload only when the cached byte
 *    CB 0xA02FA4FA1 equals the current index IX 0xA02FA4F9E (and the mode words and the pair
 *    bytes match). While a patch type T (1 or 2) is in effect the wrapper keeps CB = 0x80 | T
 *    between calls ("patch tables of type T in the hardware"), which can never equal IX while
 *    IX <= 1; before the stock call it turns 0x80 | T back into IX (stock may skip) or IX into
 *    0xFF (stock must upload), after a successful upload (CB == IX) it marks 0x80 | T again.
 *    With T == 0 it changes nothing, so a 0x80 | T left behind makes stock re-upload its own
 *    tables. Guard: IX above 1 (in particular IX >= 0x80, where 0x80 | T could equal IX) makes
 *    the wrapper pass through without counting anything. Every CB change is a bounded CAS of the
 *    aligned word 0xA02FA4FA0 from the value just read, so a concurrent store is never lost and
 *    a lost race leaves the decision to stock (no mark).
 *  - playback info copy 0x406B41A0 (bl 0x404106E8): the stock copy, then the GUI playback info
 *    0xA03439CA0 is classified into state byte +0 (0x80 | clip type for tone 2, else 0).
 *  - playback info builder calls 0x407FA000 and 0x407FB7FC (bl 0x407FA12C; re-site of the old
 *    exit hook 0x407FAC40): the stock builder, then its destination x2 is classified when it is
 *    the GUI playback info.
 *  - NCTG parse tail 0x40704940 (b 0x407047C0): B->B adapter; for a custom Log clip (name
 *    "S-LOG3" or "LOG3G10" in the 0x2000023 block at [x20 + 8] + 8) whose tone tag (block -
 *    0x2A8) is 1, the tag becomes 2 (N-Log), so the camera plays it as a Log clip.
 *  - playback detail label: re-sited from 0x4067CC00 (mov w9,#0x2d4e; the verifier admits no
 *    non-branch record with two exits) to the label copy 0x4067CC90 (bl 0x4163BBB0, copy of 6
 *    bytes from sp+0x90 to the label buffer sp+0x128). The stock copy runs; when it copied the
 *    tone label "N-Log", the GUI playback info has tone 2 and its name is a custom Log name, the
 *    first eight bytes of the buffer become "S-Log3" or "Log3G10" (with terminator), as the
 *    old adapter wrote them. A class; no Q record.
 *  - shared site lv_display (0x40EA9934): pre hook z63h_lv_display_g9 clears the playback
 *    state byte (entering the live-view display means playback has ended).
 *
 * PASS (no page, phase not LIVE/DRAINING, G9 broken or not selected): every adapter does
 * exactly the stock work and stores nothing outside its frame. Integrity break: z63g_phase()
 * gives 0 from the next call. */
#include <stdint.h>
#define Z63_SETTINGS_SHARED_V3
#include "../gate_api.h"
#include "../sites/lv_display.h"
#include "../settings_record.c"
#include "../../nv_quality.c"
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;

#define G9 9u
#define G8 8u
#define VA_IX       0xa02fa4f9eull   /* current index (byte) */
#define VA_CB_WORD  0xa02fa4fa0ull   /* aligned word holding the cached index CB as byte 1 */
#define VA_VARIANT  0xa028104d0ull   /* display variant: 0 rear monitor, 1 viewfinder */
#define PB_INFO     0xa03439ca0ull   /* GUI playback info (0x40410704 returns it) */
#define PB_TONE     0x1cd0u          /* u32; 2 = N-Log (0x4067CA7C) */
#define PB_NAME     0x5d4u           /* picture-control name, first 8 bytes compared */
#define NAME_SLOG3_LO 0x4f4c2d53u    /* "S-LO" */
#define NAME_SLOG3_HI 0x00003347u    /* "G3\0\0" */
#define NAME_L3G10_LO 0x33474f4cu    /* "LOG3" */
#define NAME_L3G10_HI 0x00303147u    /* "G10\0" */

u64 stock_cache_a(u64,u64,u64,u64,u64,u64,u64,u64);
u64 stock_cache_b(u64,u64,u64,u64,u64,u64,u64,u64);
u64 stock_pb_copy(u64,u64,u64,u64,u64,u64,u64,u64);
u64 stock_pb_build(u64,u64,u64,u64,u64,u64,u64,u64);
u64 stock_label_copy(u64,u64,u64,u64,u64,u64,u64,u64);

/* View-assist tables. Asset build (Z63_G9_ASSET, the asset range G9-REQUESTS.md asks for): the
 * tables of z63patch/log_view_assist.py blob() (0x280 B, sha256 8537cd29...; matrices of 24 B at
 * 0x00, k = 2 * (type - 1) + variant; curve B of 128 B at 0x80, same order) and the pointer
 * tables (matrix type 1, matrix type 2, curve B type 1, curve B type 2; 4 entries each: index 0
 * variant 0/1 the patch records, index 1 variant 0/1 the stock records of 0x41876B70 /
 * 0x41876C90; z63patch/log_view_assist.py pointers()) in the read-only asset at 0x401DB000. The
 * tables cannot sit in the code slot: curve words such as 0x16001400 decode as branches out of
 * the code, which the verifier refuses.
 * STUB build (no asset range for G9 in the registry yet): no tables; the patch entries point at
 * the stock N-Log records (index 0), so the substituted tables give exactly the stock N-Log
 * assist. Marked stub: it exercises every record, never shows the patch tables. */
struct va_tables {u8 data[0x280];u64 pointers[16];};
#define VA_STOCK_M0 0xa02810cb8ull, 0xa02810cd0ull
#define VA_STOCK_B0 0xa02811e48ull, 0xa02811ec8ull
#ifdef Z63_G9_ASSET
#define VA_D(o) (u64)(uintptr_t)&z63_g9_va_tables.data[o]
__attribute__((section(".z63asset.401db000"),used,aligned(16)))
const struct va_tables z63_g9_va_tables = {{
    0x01,0x01,0xcf,0x06,0x20,0xfd,0x12,0x00,0x44,0xff,0x61,0x05,0x5a,0xff,0x08,0x00,
    0xc4,0xfe,0x34,0x05,0x01,0x00,0x00,0x00,0x01,0x01,0xad,0x04,0x47,0xff,0x0d,0x00,
    0x9c,0xff,0x0f,0x05,0x56,0xff,0x07,0x00,0x45,0xff,0xb5,0x04,0x01,0x00,0x00,0x00,
    0x01,0x01,0xee,0x07,0x66,0xfc,0xac,0xff,0x4a,0xff,0x00,0x06,0xb6,0xfe,0x98,0xff,
    0xdc,0xfd,0x8c,0x06,0x01,0x00,0x00,0x00,0x01,0x01,0x77,0x05,0xec,0xfe,0x9d,0xff,
    0xa8,0xff,0x9f,0x05,0xb9,0xfe,0xb4,0xff,0x65,0xfe,0xe8,0x05,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,
    0x01,0x00,0x09,0x00,0x61,0x00,0x15,0x01,0xa7,0x01,0x8b,0x02,0x4b,0x03,0xd9,0x04,
    0x68,0x06,0x79,0x09,0xd9,0x0c,0xfd,0x12,0x5a,0x17,0xf0,0x19,0xb4,0x1b,0xe3,0x1c,
    0xb8,0x1d,0x4f,0x1e,0xc0,0x1e,0xfb,0x1e,0xc3,0x1f,0xcc,0x1f,0xcc,0x1f,0xcc,0x1f,
    0xcc,0x1f,0x02,0x00,0x04,0x00,0x06,0x00,0x08,0x00,0x0c,0x00,0x10,0x00,0x18,0x00,
    0x20,0x00,0x30,0x00,0x40,0x00,0x60,0x00,0x80,0x00,0xc0,0x00,0x00,0x01,0x80,0x01,
    0x00,0x02,0x00,0x03,0x00,0x04,0x00,0x06,0x00,0x08,0x00,0x0a,0x00,0x0c,0x00,0x0e,
    0x00,0x10,0x00,0x12,0x00,0x14,0x00,0x16,0x00,0x18,0x00,0x1a,0x00,0x1c,0x00,0x1e,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x19,0x00,
    0x50,0x00,0x1f,0x01,0x3d,0x02,0x44,0x03,0xcd,0x03,0xbb,0x04,0x95,0x05,0x38,0x07,
    0xd5,0x08,0xf0,0x0b,0x49,0x0f,0x84,0x14,0x43,0x18,0x85,0x1a,0x0f,0x1c,0x1c,0x1d,
    0xd9,0x1d,0x5f,0x1e,0xc9,0x1e,0xfe,0x1e,0xc3,0x1f,0xcc,0x1f,0xcc,0x1f,0xcc,0x1f,
    0xcc,0x1f,0x02,0x00,0x04,0x00,0x06,0x00,0x08,0x00,0x0c,0x00,0x10,0x00,0x18,0x00,
    0x20,0x00,0x30,0x00,0x40,0x00,0x60,0x00,0x80,0x00,0xc0,0x00,0x00,0x01,0x80,0x01,
    0x00,0x02,0x00,0x03,0x00,0x04,0x00,0x06,0x00,0x08,0x00,0x0a,0x00,0x0c,0x00,0x0e,
    0x00,0x10,0x00,0x12,0x00,0x14,0x00,0x16,0x00,0x18,0x00,0x1a,0x00,0x1c,0x00,0x1e,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x1c,0x00,0x60,0x00,0x3c,0x01,0x1e,0x02,0x84,0x03,0xce,0x04,0x5a,0x07,
    0xd6,0x09,0xf2,0x0e,0x6c,0x13,0xd8,0x18,0xa6,0x1b,0x3c,0x1d,0x2c,0x1e,0xc4,0x1e,
    0x24,0x1f,0x66,0x1f,0x94,0x1f,0xb4,0x1f,0xcc,0x1f,0xde,0x1f,0xea,0x1f,0xf6,0x1f,
    0xfe,0x1f,0x02,0x00,0x04,0x00,0x06,0x00,0x08,0x00,0x0c,0x00,0x10,0x00,0x18,0x00,
    0x20,0x00,0x30,0x00,0x40,0x00,0x60,0x00,0x80,0x00,0xc0,0x00,0x00,0x01,0x80,0x01,
    0x00,0x02,0x00,0x03,0x00,0x04,0x00,0x06,0x00,0x08,0x00,0x0a,0x00,0x0c,0x00,0x0e,
    0x00,0x10,0x00,0x12,0x00,0x14,0x00,0x16,0x00,0x18,0x00,0x1a,0x00,0x1c,0x00,0x1e,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x00,
    0x90,0x00,0x82,0x01,0x3c,0x02,0x62,0x03,0x4a,0x04,0xd0,0x05,0x2c,0x07,0xcc,0x09,
    0x4c,0x0c,0x1a,0x11,0xee,0x14,0x96,0x19,0x04,0x1c,0x6c,0x1d,0x40,0x1e,0xcc,0x1e,
    0x26,0x1f,0x66,0x1f,0x94,0x1f,0xb4,0x1f,0xcc,0x1f,0xde,0x1f,0xea,0x1f,0xf6,0x1f,
    0xfe,0x1f,0x02,0x00,0x04,0x00,0x06,0x00,0x08,0x00,0x0c,0x00,0x10,0x00,0x18,0x00,
    0x20,0x00,0x30,0x00,0x40,0x00,0x60,0x00,0x80,0x00,0xc0,0x00,0x00,0x01,0x80,0x01,
    0x00,0x02,0x00,0x03,0x00,0x04,0x00,0x06,0x00,0x08,0x00,0x0a,0x00,0x0c,0x00,0x0e,
    0x00,0x10,0x00,0x12,0x00,0x14,0x00,0x16,0x00,0x18,0x00,0x1a,0x00,0x1c,0x00,0x1e,
}, {
    VA_D(0x00), VA_D(0x18), VA_STOCK_M0, VA_D(0x30), VA_D(0x48), VA_STOCK_M0,
    VA_D(0x80), VA_D(0x100), VA_STOCK_B0, VA_D(0x180), VA_D(0x200), VA_STOCK_B0}};
#else
__attribute__((aligned(16)))
const struct va_tables z63_g9_va_tables = {{0}, {
    0xa02810bb0ull, 0xa02810bc8ull, VA_STOCK_M0, 0xa02810bb0ull, 0xa02810bc8ull, VA_STOCK_M0,
    0xa02811948ull, 0xa028119c8ull, VA_STOCK_B0, 0xa02811948ull, 0xa028119c8ull, VA_STOCK_B0}};
#endif

static inline u32 ld8(u64 a){return *(volatile const u8 *)(uintptr_t)a;}

/* 1 S-Log3, 2 Log3G10, else 0, from the first 8 name bytes at a (4-byte aligned). */
static inline u32 name_type(u64 a) {
    u32 lo=z63g_ld32(a),hi=z63g_ld32(a+4);
    if(lo==NAME_SLOG3_LO&&hi==NAME_SLOG3_HI)return 1;
    if(lo==NAME_L3G10_LO&&hi==NAME_L3G10_HI)return 2;
    return 0;
}

/* The Log type for view assist; the caller holds ACT[9 & 3]. */
static u32 va_type(u64 p) {
    u32 pb=*z63g_state(p,Z63_STATE_G9);
    if(pb&0x80u) {pb&=0x7fu;return pb<=2u?pb:0;}
    if(!z63g_phase(p,G8))return 0;                    /* G8 absent, not selected, broken or not live */
    u64 e=z63g_effective(p);
    if(!e)return 0;
    struct z63_feature_settings v;
    if(z63_settings_decode((u32)e,&v)<Z63_RECORD_V2)return 0;
    return v.log_colour==1u||v.log_colour==2u?v.log_colour:0;
}

/* The type the selects apply: 0 unless the index and the variant stay inside the 4-entry tables. */
static u32 va_effective(u64 p) {
    if(ld8(VA_IX)>1u||z63g_ld32(VA_VARIANT)>1u)return 0;
    return va_type(p);
}

/* For the select adapters (after their acquire load of P): the type's matrix pointer table
 * (curve B at +0x40), or 0 for stock. */
u64 z63_g9_table(void) {
    u64 p=z63g_page();
    if(!p||!z63g_act_enter(p,G9))return 0;
    u32 t=va_effective(p);
    z63g_act_leave(p,G9);
    return t?(u64)(uintptr_t)&z63_g9_va_tables.pointers[4u*(t-1u)]:0;
}

/* Move CB (byte 1 of the word at VA_CB_WORD) from `from` to `to` by one bounded CAS of the
 * word just read: 1 when stored. */
static u32 cb_move(u32 from,u32 to) {
    u32 v=z63g_ld32(VA_CB_WORD);
    if(((v>>8)&0xffu)!=from)return 0;
    return z63g_cas32(VA_CB_WORD,v,(v&~0xff00u)|(to<<8));
}

static inline __attribute__((always_inline)) u64 cache(u32 b,u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=z63g_page();
    if(!p||!z63g_phase(p,G9)||ld8(VA_IX)>1u||!z63g_act_enter(p,G9))
        return b?stock_cache_b(a0,a1,a2,a3,a4,a5,a6,a7):stock_cache_a(a0,a1,a2,a3,a4,a5,a6,a7);
    u32 t=va_effective(p),ok=0,ix=0;
    if(t) {
        ix=ld8(VA_IX);
        u32 cb=(z63g_ld32(VA_CB_WORD)>>8)&0xffu;
        if(cb==(0x80u|t))ok=cb_move(cb,ix);           /* patch T in the hardware: stock may skip */
        else if(cb==ix)ok=cb_move(ix,0xffu);           /* stock tables in the hardware: force */
        else ok=1;                                     /* already a mismatch for stock */
    }
    u64 r=b?stock_cache_b(a0,a1,a2,a3,a4,a5,a6,a7):stock_cache_a(a0,a1,a2,a3,a4,a5,a6,a7);
    if(ok&&ld8(VA_IX)==ix)cb_move(ix,0x80u|t);        /* uploaded (or a valid skip): mark patch T */
    z63g_act_leave(p,G9);
    return r;
}
u64 z63_g9_cache_a(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7){return cache(0,a0,a1,a2,a3,a4,a5,a6,a7);}
u64 z63_g9_cache_b(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7){return cache(1,a0,a1,a2,a3,a4,a5,a6,a7);}

/* Classify the GUI playback info into the state byte. */
static void classify(u64 p) {
    u32 tone=z63g_ld32(PB_INFO+PB_TONE);
    *z63g_state(p,Z63_STATE_G9)=tone==2u?(u8)(0x80u|name_type(PB_INFO+PB_NAME)):0;
}
u64 z63_g9_pb_copy(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G9):0;
    u64 r=stock_pb_copy(a0,a1,a2,a3,a4,a5,a6,a7);
    if(ph)classify(p);
    return r;
}
u64 z63_g9_pb_build(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G9):0;
    u64 r=stock_pb_build(a0,a1,a2,a3,a4,a5,a6,a7);
    if(ph&&a2==PB_INFO)classify(p);
    return r;
}

/* Label copy 0x4067CC90: x0 the label buffer (caller's sp+0x128), x1 the tone text (sp+0x90), x2 6. */
u64 z63_g9_label(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G9):0;
    u64 r=stock_label_copy(a0,a1,a2,a3,a4,a5,a6,a7);
    if(!ph||a2!=6u||(a0&7u)||(a1&3u))return r;
    /* "N-Lo" "g\0": the stock tone-2 text (0x4067CC00..0x4067CC14). */
    if(z63g_ld32(a1)!=0x6f4c2d4eu||(z63g_ld32(a1+4)&0xffffu)!=0x0067u)return r;
    if(z63g_ld32(PB_INFO+PB_TONE)!=2u)return r;
    u32 t=name_type(PB_INFO+PB_NAME);
    if(t)*(volatile u64 *)(uintptr_t)a0=t==1u?0x000033676f4c2d53ull:0x0030314733676f4cull;   /* "S-Log3\0\0", "Log3G10\0" */
    return r;
}

/* Parse tail helper: x20 of the parse (the output descriptor), block at [x20 + 8]. */
void z63_g9_parse_c(u64 desc) {
    u64 p=z63g_page();
    if(!p||!z63g_phase(p,G9))return;
    u64 block=z63g_ld64(desc+8);
    if(block&3u||!name_type(block+8))return;
    volatile u32 *tag=(volatile u32 *)(uintptr_t)(block-0x2a8u);
    if(*tag==1u)*tag=2u;
}

/* Shared site lv_display, role pre (sites/lv_display.h): playback has ended. */
void z63h_lv_display_g9(u64 page,u32 phase,void *self,u32 flag) {
    (void)phase;(void)self;(void)flag;
    volatile u8 *s=z63g_state(page,Z63_STATE_G9);
    if(*s)*s=0;
}

/* B->B adapters: x0-x17 and x30 saved in a 0xA0-byte frame; the acquire load of P is the first
 * access outside it. */
#define Z63_G9_SAVE \
    "  sub sp,sp,#0xa0\n" \
    "  stp x0,x1,[sp,#0x00]\n  stp x2,x3,[sp,#0x10]\n  stp x4,x5,[sp,#0x20]\n  stp x6,x7,[sp,#0x30]\n" \
    "  stp x8,x9,[sp,#0x40]\n  stp x10,x11,[sp,#0x50]\n  stp x12,x13,[sp,#0x60]\n  stp x14,x15,[sp,#0x70]\n" \
    "  stp x16,x17,[sp,#0x80]\n  str x30,[sp,#0x90]\n" \
    "  movz x16,#0xae20\n  movk x16,#0x028c,lsl #16\n  movk x16,#0xa,lsl #32\n  ldar x16,[x16]\n"
#define Z63_G9_RESTORE \
    "  ldp x0,x1,[sp,#0x00]\n  ldp x2,x3,[sp,#0x10]\n  ldp x4,x5,[sp,#0x20]\n  ldp x6,x7,[sp,#0x30]\n" \
    "  ldp x8,x9,[sp,#0x40]\n  ldp x10,x11,[sp,#0x50]\n  ldp x12,x13,[sp,#0x60]\n  ldp x14,x15,[sp,#0x70]\n" \
    "  ldp x16,x17,[sp,#0x80]\n  ldr x30,[sp,#0x90]\n  add sp,sp,#0xa0\n"
__asm__(
".text\n"
".p2align 2\n"
".global z63_g9_matrix\n"
".type z63_g9_matrix,%function\n"
"z63_g9_matrix:\n"
Z63_G9_SAVE
"  bl z63_g9_table\n"
"  cbz x0,1f\n"
"  str x0,[sp,#0x50]\n"                       /* saved x10: the type's matrix table */
"1:\n"
Z63_G9_RESTORE
"  b z63_g9_back_matrix\n"                    /* 0x402D8ED0 */
".size z63_g9_matrix,.-z63_g9_matrix\n"
".p2align 2\n"
".global z63_g9_curves\n"
".type z63_g9_curves,%function\n"
"z63_g9_curves:\n"
Z63_G9_SAVE
"  bl z63_g9_table\n"
"  cbz x0,1f\n"
"  str x0,[sp,#0x50]\n"                       /* saved x10: the type's matrix table */
"  add x0,x0,#0x40\n"
"  str x0,[sp,#0x60]\n"                       /* saved x12: the type's curve-B table */
"1:\n"
Z63_G9_RESTORE
"  b z63_g9_back_curves\n"                    /* 0x402D9060 */
".size z63_g9_curves,.-z63_g9_curves\n"
".p2align 2\n"
".global z63_g9_parse\n"
".type z63_g9_parse,%function\n"
"z63_g9_parse:\n"
Z63_G9_SAVE
"  mov x0,x20\n"
"  bl z63_g9_parse_c\n"
Z63_G9_RESTORE
"  b z63_g9_back_parse\n"                     /* 0x407047C0 */
".size z63_g9_parse,.-z63_g9_parse\n"
);
