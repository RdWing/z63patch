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
/* G3, indicators on the arm-time gate: the recording-
 * menu, live-view and i-menu frame-size icons follow the H.265 tier (G5) and the ProRes grade
 * (G4), and the 25 extension icon ids 0x7000..0x7018 resolve through the patch's own resource
 * table. Declared by z63patch/gate_groups/indicators.py; one translation unit linked alone at the
 * group's fixed range, so its bytes do not depend on the composition. The mapping rules are the
 * reviewed old-layout units native/video_menu_icons.c, native/video_lv_icons.c and
 * native/h265_setter_icons.c, rewritten without loops (the verifier refuses them) and without
 * PTR() casts; the readers are native/video_menu_icons.S without "br x16".
 *
 * Set "icons", install ascending, restore descending:
 *  ranks 1-2   the value reader's two consumers 0x402F844C, 0x402F87DC (BL; they replace the
 *              old entry record 0x40339B30 "mov x8,#0x2850", a non-branch word)
 *  ranks 3-6   the four BL call sites of the index reader 0x40339A60
 *  ranks 7-12  producers: recording-menu bind 0x4059652C, live-view tail 0x40430A18, i-menu
 *              tile 0x404EDCDC, setter call sites 0x4040A1E0, 0x4040A248, 0x40409F54
 * so no producer can hand out an extension id while a reader is stock.
 *
 * Readers (S class, no gate lookup, every phase): an id in 0x7000..0x7018 is answered from the
 * extension table; every other id runs the whole stock reader (index reader: tail b to
 * 0x40339A60; value reader: the stock reader runs as always, and its two consumers see the
 * table's values for an extension id). The table is fixed image
 * data and the readers read no setting, so they need no phase: an id produced while LIVE stays
 * drawable until the readers themselves are restored.
 *
 * Producers (C class): P acquire-loaded first; then, for a candidate number only, LIVE/DRAINING
 * of G3 and of an owner (G4 for file type 4, G5 for file types 2/3) under ACT[3], the published
 * word read once (z63g_effective) and decoded; map; the stock call inside the hold; leave. Any
 * other case (no page, bad magic/self, G3 or both owners not live, broken, not selected,
 * publication busy, an undecodable word) calls the stock function with the arguments unchanged
 * and stores nothing outside the frame. */
#include <stdint.h>
#define Z63_SETTINGS_SHARED_V3
#include "../gate_api.h"
#include "../settings_record.c"
#include "../../nv_quality.c"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;

#define G3 3u
#define TYPE_BYTE 0xa0343bed0ull   /* recording file type (2/3 H.265, 4 ProRes) */
#define NP 21u
/* Extension ids answered by the readers: 25, or 31 with G10 ENT variant 'mark' (rows 0x7019..0x701E
 * point at G10's derived marked icons; indicators.py passes -DZ63_G3_EXT=31). */
#ifndef Z63_G3_EXT
#define Z63_G3_EXT 25
#endif
#define Z63_G3_STR2(x) #x
#define Z63_G3_STR(x) Z63_G3_STR2(x)
#define Z63_G3_EXT_S Z63_G3_STR(Z63_G3_EXT)

/* ---- the extension resource table (derived asset 0x401AC000, 16-byte records: u16 flags, u16 0,
 * u32 bytes, u64 PNG pointer) ----
 * Made at build time from the user's MAIN (z63patch/video_lv_icons.py gate_g3_block, a Derived
 * asset of the declaration; never in the repository or a recipe): 25 records, the 21 derived
 * PNGs after them (0x7000..0x7008 recording-menu plain icons, 0x7009..0x7014 live-view non-star
 * twins), rows 0x7015..0x7018 pointing at the four Log PNGs G8 carries at 0x401B5500 (produced
 * only by G8's live-view Log icon record). The readers name it by the extern below. */
/* ---- readers ---- */
__asm__(
".text\n"
".p2align 2\n"
/* Index reader call sites (BL 0x40339A60; x0 = &u32 id, x1 = out). Uses x8, x9, NZCV (dead
 * after the four sites). The stock reader writes out+0 (u16), +4 (u32), +0x10 (u64). */
".global z63_g3_icon_index\n"
".type z63_g3_icon_index,%function\n"
"z63_g3_icon_index:\n"
"  ldr w8,[x0]\n"
"  sub w8,w8,#7,lsl #12\n"
"  cmp w8,#" Z63_G3_EXT_S "\n"
"  b.hs 1f\n"
"  adrp x9,z63_derived_401ac000\n"
"  add x9,x9,:lo12:z63_derived_401ac000\n"
"  add x9,x9,x8,lsl #4\n"
"  ldrh w8,[x9]\n"
"  strh w8,[x1]\n"
"  ldr w8,[x9,#4]\n"
"  str w8,[x1,#4]\n"
"  ldr x8,[x9,#8]\n"
"  str x8,[x1,#16]\n"
"  ret\n"
"1:b stock_icon_index\n"
".size z63_g3_icon_index,.-z63_g3_icon_index\n"
/* Value reader 0x40339B30 (its entry "mov x8,#0x2850" is not a branch word, and
 * the reader is reached only by "blr" through the pointer slot 0xA02FC0D78, which 0x402F6814
 * fills at run time from the provider record 0x42B6C1B0: no B/BL to it exists). Its only two
 * callers, F1 0x402F8370 (blr 0x402F8424) and F2 0x402F8644 (blr 0x402F8794), pass x1 = sp+0x28
 * (u32 bytes) and x2 = sp+0x20 (u64 png), store nothing in sp+0x20..0x2B afterwards and hand
 * sp+0x20 to one BL each: F1 bl 0x402F9140 at 0x402F844C (x0 = sp+0x20), F2 bl 0x402F92F8 at
 * 0x402F87DC (x2 = sp+0x20). The records are those two BLs: before the stock call, when the
 * caller took the slot path and the slot still holds the stock reader, an extension id gets the
 * table's bytes/png over what the stock reader wrote (its row 0 for an id past its count), which
 * is the old entry record's result. The id is read again from the caller's object (x19 +
 * 0x98/0xA4/0xB0, as the caller read it before the blr; x19 callee-saved, unchanged):
 *   kind 0xE700 and [x19+0x94] == 1 -> [x19+0x98]; kind 0xE701 and [x19+0xA0] == 1 -> [x19+0xA4];
 *   kind 0xE702 and [x19+0xAC] == 1 -> [x19+0xB0]; else the 0xA02FC0D70 slot or no blr.
 * Kind: F1 w21 (= w0 at entry), F2 [x21+4] (x21 = x0 at entry). Uses x16, x17, NZCV (dead at a
 * call) and x9/x10 saved on the stack; every other register reaches the stock call unchanged. */
".macro g3_pick kind, flag, idoff, val\n"
"  movz w16,#\\val\n"
"  cmp \\kind,w16\n"
"  b.ne 1f\n"
"  ldr w17,[x19,#\\flag]\n"
"  cmp w17,#1\n"
"  b.ne 1f\n"
"  ldr w17,[x19,#\\idoff]\n"
"  b 5f\n"
"1:\n"
".endm\n"
".macro g3_value out\n"           /* w17 = id; \\out = the caller's sp+0x20 */
"5:sub w17,w17,#7,lsl #12\n"
"  cmp w17,#" Z63_G3_EXT_S "\n"
"  b.hs 9f\n"
"  movz x16,#0x0d78\n"              /* the slot still holds the stock value reader */
"  movk x16,#0x02fc,lsl #16\n"
"  movk x16,#0xa,lsl #32\n"
"  ldr x16,[x16]\n"
"  movz x9,#0x9b30\n"
"  movk x9,#0x4033,lsl #16\n"
"  cmp x16,x9\n"
"  b.ne 9f\n"
"  adrp x16,z63_derived_401ac000\n"
"  add x16,x16,:lo12:z63_derived_401ac000\n"
"  add x16,x16,x17,lsl #4\n"
"  ldr w9,[x16,#4]\n"
"  str w9,[\\out,#8]\n"
"  ldr x9,[x16,#8]\n"
"  str x9,[\\out]\n"
".endm\n"
".p2align 2\n"
".global z63_g3_value_f1\n"
".type z63_g3_value_f1,%function\n"
"z63_g3_value_f1:\n"
"  stp x9,x10,[sp,#-16]!\n"
"  g3_pick w21, 0x94, 0x98, 0xe700\n"
"  g3_pick w21, 0xa0, 0xa4, 0xe701\n"
"  g3_pick w21, 0xac, 0xb0, 0xe702\n"
"  b 9f\n"
"  g3_value x0\n"
"9:ldp x9,x10,[sp],#16\n"
"  b stock_value_f1\n"
".size z63_g3_value_f1,.-z63_g3_value_f1\n"
".p2align 2\n"
".global z63_g3_value_f2\n"
".type z63_g3_value_f2,%function\n"
"z63_g3_value_f2:\n"
"  stp x9,x10,[sp,#-16]!\n"
"  ldr w10,[x21,#4]\n"
"  g3_pick w10, 0x94, 0x98, 0xe700\n"
"  g3_pick w10, 0xa0, 0xa4, 0xe701\n"
"  g3_pick w10, 0xac, 0xb0, 0xe702\n"
"  b 9f\n"
"  g3_value x2\n"
"9:ldp x9,x10,[sp],#16\n"
"  b stock_value_f2\n"
".size z63_g3_value_f2,.-z63_g3_value_f2\n"
/* Recording-menu bind 0x4059652C (BL TEXT_BIND 0x4040A1AC; x0 widget, w1 icon). TEXT_BIND reads
 * only x0 and w1 (its prologue moves w1 to w19, then writes x1 and x2), so w2 carries the file
 * type the refresh holds in w25 (SETTING_GET 0x06000F06 at 0x40596408). */
".p2align 2\n"
".global z63_g3_menu_draw_entry\n"
".type z63_g3_menu_draw_entry,%function\n"
"z63_g3_menu_draw_entry:\n"
"  mov w2,w25\n"
"  b z63_g3_menu_draw\n"
".size z63_g3_menu_draw_entry,.-z63_g3_menu_draw_entry\n");

/* ---- mapping (ported from the old-layout units; pair tables disjoint, every output a fixed
 * point, so the order in which owner and setter wrappers apply them does not matter) ---- */
static const u16 menu_pairs[NP][2]={
    {3300,3299},{3302,3301},{3304,3303},{3306,3305},{3308,3307},
    {4335,0x7006},{4337,0x7007},{4339,0x7008},{4427,4428},{4429,4430},
    {4707,4934},{4708,4935},{4743,0x7000},{4744,0x7001},
    {6177,0x7002},{6178,0x7003},{6179,6180},{6181,6182},{6183,6184},
    {6185,0x7004},{6186,0x7005}};
static const u16 lv_fill[NP][2]={
    {6215,6217},{6219,6221},{6223,6225},{2558,2758},{2557,2756},{1895,1897},
    {1891,1893},{1887,1889},{2052,2054},{2056,2058},{1417,1415},{1413,1411},
    {1409,1407},{1405,1403},{1401,1399},{6212,0x7009},{6213,0x700a},{2651,0x700b},
    {2650,0x700c},{6227,0x700d},{6229,0x700e}};
static const u16 lv_outline[NP][2]={
    {6216,6218},{6220,6222},{6224,6226},{2560,2759},{2559,2757},{1896,1898},
    {1892,1894},{1888,1890},{2053,2055},{2057,2059},{1418,1416},{1414,1412},
    {1410,1408},{1406,1404},{1402,1400},{6211,0x700f},{6214,0x7010},{2717,0x7011},
    {2716,0x7012},{6228,0x7013},{6230,0x7014}};

static inline __attribute__((always_inline)) u32 to_plain(const u16 (*t)[2],u32 icon) {
    u32 out=icon;
#pragma clang loop unroll(full)
    for(u32 i=0;i<NP;i++)out=icon==t[i][0]?t[i][1]:out;
    return out;
}
static inline __attribute__((always_inline)) u32 to_star(const u16 (*t)[2],u32 icon,u32 limit) {
    u32 out=icon;
#pragma clang loop unroll(full)
    for(u32 i=0;i<NP;i++)out=t[i][1]<limit&&icon==t[i][1]?t[i][0]:out;
    return out;
}
/* z63_video_menu_icon: file type 2/3 at tiers 1..4 star to plain; file type 4 at grades 0..3 the
 * three plain ProRes aliases to their derived ids, and at grade 0 plain to star. */
static u32 menu_icon(u32 icon,u32 type,u32 grade,u32 tier) {
    if((type==2||type==3)&&tier-1u<4u)return to_plain(menu_pairs,icon);
    if(type==4&&grade<4) {
        if(icon==4336)icon=0x7006;
        if(icon==4338)icon=0x7007;
        if(icon==4340)icon=0x7008;
        if(grade==0)return to_star(menu_pairs,icon,0x10000);
    }
    return icon;
}
/* z63_video_lv_icon: set 0 the 64x32 fill icons, set 1 the 68x36 outline icons. */
static u32 lv_icon(u32 icon,u32 set,u32 type,u32 grade,u32 tier) {
    if((type==2||type==3)&&tier-1u<4u)return set?to_plain(lv_outline,icon):to_plain(lv_fill,icon);
    if(type==4&&grade==0)return set?to_star(lv_outline,icon,0x7000):to_star(lv_fill,icon,0x7000);
    return icon;
}
/* z63_h265_setter_icon: the union, for the shared setter. */
static u32 union_icon(u32 icon,u32 type,u32 grade,u32 tier) {
    icon=menu_icon(icon,type,grade,tier);
    icon=lv_icon(icon,0,type,grade,tier);
    return lv_icon(icon,1,type,grade,tier);
}
/* Every number any table maps lies in 1399..6230 or 0x7000..0x7014 (checked by the tests): the
 * cheap test before the gate's ACT count on the hot TEXT_BIND path. */
static inline u32 candidate(u32 icon){return icon-1399u<=6230u-1399u||icon-0x7000u<=0x14u;}

/* ---- the hold ---- HELD | grade << 8 | tier with ACT[3] held (grade 4 = no ProRes rule, tier 0 =
 * no H.265 rule), or 0 with nothing held (pass through). Flattened so the decoder's result
 * stays in registers (no store outside the frame). */
#define HELD 0x80000000u
static __attribute__((flatten)) u32 hold(u64 p) {
    if(!z63g_phase(p,4)&&!z63g_phase(p,5))return 0;
    if(!z63g_act_enter(p,G3))return 0;
    u32 g4=z63g_phase(p,4),g5=z63g_phase(p,5);
    u64 e=z63g_effective(p);
    struct z63_feature_settings v;
    if((g4||g5)&&e&&z63_settings_decode((u32)e,&v)>=0)
        return HELD|(g4?v.prores:4u)<<8|(g5?v.h265:0u);
    z63g_act_leave(p,G3);
    return 0;
}
#define GRADE(h) (((h)>>8)&255u)
#define TIER(h) ((h)&255u)

u64 stock_text_bind(u64 widget,u64 icon);                              /* 0x4040A1AC */
u64 stock_lv_icons(u64 element,u64 a,u64 b,u64 fill,u64 outline);      /* 0x40435914 */
u64 stock_imenu_icon(u64 element,u64 kind,u64 number);                 /* 0x40409F44 */
u64 stock_setter(u64 element,u64 type,u64 number);                     /* 0x40331D04 */

u64 z63_g3_menu_draw(u64 widget,u64 icon,u64 type);
u64 z63_g3_menu_draw(u64 widget,u64 icon,u64 type) {
    u64 p=z63g_page();
    u32 h=p&&candidate((u32)icon)?hold(p):0;
    if(!h)return stock_text_bind(widget,icon);
    u64 r=stock_text_bind(widget,menu_icon((u32)icon,(u32)type,GRADE(h),TIER(h)));
    z63g_act_leave(p,G3);
    return r;
}

/* Live-view owner tail 0x40430A18 (b 0x40435914; x30 is the owner's caller): w3 fill, w4 outline. */
u64 z63_g3_lv_icons(u64 element,u64 a,u64 b,u64 fill,u64 outline);
u64 z63_g3_lv_icons(u64 element,u64 a,u64 b,u64 fill,u64 outline) {
    u64 p=z63g_page();
    u32 h=p&&(candidate((u32)fill)||candidate((u32)outline))?hold(p):0;
    if(!h)return stock_lv_icons(element,a,b,fill,outline);
    u32 type=*(volatile const u8 *)(uintptr_t)TYPE_BYTE;
    u64 r=stock_lv_icons(element,a,b,lv_icon((u32)fill,0,type,GRADE(h),TIER(h)),lv_icon((u32)outline,1,type,GRADE(h),TIER(h)));
    z63g_act_leave(p,G3);
    return r;
}

/* i-menu tile 0x404EDCDC (bl 0x40409F44(element, kind, number); result unused): kind 1 only. */
u64 z63_g3_imenu_icon(u64 element,u64 kind,u64 number);
u64 z63_g3_imenu_icon(u64 element,u64 kind,u64 number) {
    u64 p=z63g_page();
    u32 h=p&&(u32)kind==1&&candidate((u32)number)?hold(p):0;
    if(!h)return stock_imenu_icon(element,kind,number);
    u32 type=*(volatile const u8 *)(uintptr_t)TYPE_BYTE;
    u64 r=stock_imenu_icon(element,kind,lv_icon((u32)number,0,type,GRADE(h),TIER(h)));
    z63g_act_leave(p,G3);
    return r;
}

/* The three references to the shared setter 0x40331D04(element, type, number): BL 0x4040A1E0
 * (TEXT_BIND), BL 0x4040A248 (0x4040A210), B 0x40409F54 (0x40409F44, x30 its caller's). Type 1
 * only; the result (w0) is returned. Replaces the retired entry hook z63_h265_setter_entry. */
u64 z63_g3_setter(u64 element,u64 type,u64 number);
u64 z63_g3_setter(u64 element,u64 type,u64 number) {
    u64 p=z63g_page();
    u32 h=p&&(u32)type==1&&candidate((u32)number)?hold(p):0;
    if(!h)return stock_setter(element,type,number);
    u32 file_type=*(volatile const u8 *)(uintptr_t)TYPE_BYTE;
    u64 r=stock_setter(element,type,union_icon((u32)number,file_type,GRADE(h),TIER(h)));
    z63g_act_leave(p,G3);
    return r;
}
