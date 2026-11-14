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
/* G5, H.265 quality tiers on the arm-time gate.
 * Declared by z63patch/gate_groups/h265.py; one translation unit linked alone at the group's
 * fixed range, so its bytes do not depend on the composition. Reuses the reviewed H.265 units
 * (native/h265_quality.c, native/feature_settings.c with the shared v2/v3 reader,
 * native/nv_quality.c) and the shapes of native/h265_hooks.S / native/h265_threshold.S.
 *
 * Two install records, both C class, restore under Q:
 *  - selector tail 0x402FBFCC (stock "b memcpy 0x4163B5C0", x8 = row index) -> b z63_g5_select.
 *    The adapter always does the stock copy; in LIVE/DRAINING with G5 selected and not broken it
 *    holds ACT[1] (5 & 3) across reading the published settings word and applying the stored
 *    tier (z63_h265_quality) to the fresh copy of the stock row. PASS (no ACT, no published word,
 *    no store outside the frame other than the stock copy itself) in every other phase, with no
 *    page, when G5 is broken or not selected, or while a publication is in progress.
 *    In DRAINING the published word is frozen (the armed poll publishes only in LIVE), so a
 *    recording keeps one behaviour until the gate restores under Q.
 *  - threshold: the rate-class BL 0x402FF284 (bl 0x402FFA48) -> bl z63_g5_threshold (the
 *    old site 0x402FF294 "lsr w9,w8,#7" was not a branch word). FULL: the approved rule (stock value at most 0x752, the selector key word 0xA02FCD270 is 1 and
 *    (rate >> 6) > 0xC34) takes the divisor 800 the Z8 uses for these rates, applied to the two
 *    budgets the divisions read. PASS: the stock call alone. The rule does not read the tier. */
#include <stdint.h>
#define Z63_SETTINGS_SHARED_V3
#include "../gate_api.h"
#include "../../h265_quality.c"
#include "../settings_record.c"
#include "../../nv_quality.c"
typedef uint32_t u32;typedef uint64_t u64;

#define G5 5u
#define H265_ROWS 61u
#define H265_ROW_BYTES 36u
#define H265_TABLE 0x42ac02fcull
void *stock_memcpy(void *out,const void *input,u64 bytes);
u32 stock_rate_leaf(u32 index);
void *z63_g5_copy(void *out,const void *input,u64 bytes,u64 index);

__asm__(
".text\n"
".p2align 2\n"
".global z63_g5_select\n"
".type z63_g5_select,%function\n"
"z63_g5_select:\n"
"  mov x3,x8\n"
"  b z63_g5_copy\n"
".size z63_g5_select,.-z63_g5_select\n"
/* Threshold: the record is the BL 0x402FF284 (bl 0x402FFA48, the frameless
 * rate-class leaf: w0 in, w0 out, clobbers x8 and NZCV) two words before the old value site
 * 0x402FF294 (lsr w9,w8,#7, now stock). The adapter makes the stock call, then, when FULL and
 * the approved rule holds, divides the two budgets the stock divisions use by two in advance:
 * w27 (udiv 0x402FF2A4) and w24 (udiv 0x402FF2F4) = (w / 800) * 400, so the stock divisor 400
 * (w9 = rate >> 7 <= 0x752) gives exactly w / 800, the result the old site's w9 = 0x753 chose.
 * w9 (only the two cmp at 0x402FF298/0x402FF2D4 read it) stays stock. x24/x27 are loaded by
 * ldr w at 0x402FF274/0x402FF278 just before the call and are dead after those two udivs (x27
 * is reassigned at 0x402FF2E0, x24 reloaded each loop pass and restored at the epilogue).
 * The rate is [[caller sp + 0x18]], read here after the leaf as the stock code reads it at
 * 0x402FF290 (no store between). Gate test (in registers): P acquire-loaded first; the page in
 * range and aligned; gate magic and self; phase LIVE or DRAINING; G5 not broken (word bit 21);
 * G5 selected (state +28 bit 5). Otherwise PASS: only the stock call. Uses x16, x17, NZCV
 * (dead: the next flag use is the cmp at 0x402FF298) besides the leaf's x0/x8; x1 is saved. */
".p2align 2\n"
".global z63_g5_threshold\n"
".type z63_g5_threshold,%function\n"
"z63_g5_threshold:\n"
"  stp x29,x30,[sp,#-0x20]!\n"
"  mov x29,sp\n"
"  movz x16,#0xae20\n"
"  movk x16,#0x028c,lsl #16\n"
"  movk x16,#0xa,lsl #32\n"
"  ldar x16,[x16]\n"
"  bl stock_rate_leaf\n"
"  stp x0,x1,[sp,#0x10]\n"
"  movz x16,#0xae20\n"
"  movk x16,#0x028c,lsl #16\n"
"  movk x16,#0xa,lsl #32\n"
"  ldar x16,[x16]\n"
"  tst x16,#0xfff\n"
"  b.ne 2f\n"
"  movz x17,#0xa,lsl #32\n"
"  cmp x16,x17\n"
"  b.lo 2f\n"
"  movz x17,#0xf000\n"
"  movk x17,#0x3fff,lsl #16\n"
"  movk x17,#0xa,lsl #32\n"
"  cmp x16,x17\n"
"  b.hs 2f\n"
"  ldr w0,[x16,#0xf80]\n"
"  movz w1,#0x365a\n"
"  movk w1,#0x5447,lsl #16\n"
"  cmp w0,w1\n"
"  b.ne 2f\n"
"  ldr x0,[x16,#0xf88]\n"
"  cmp x0,x16\n"
"  b.ne 2f\n"
"  add x17,x16,#0xf84\n"
"  ldar w0,[x17]\n"
"  and w1,w0,#15\n"
"  sub w1,w1,#1\n"
"  cmp w1,#1\n"
"  b.hi 2f\n"
"  tbnz w0,#21,2f\n"
"  ldr w0,[x16,#0x9cc]\n"
"  tbz w0,#5,2f\n"
/* FULL: the approved rule on the stock rate (stock value w9 = rate >> 7 at most 0x752, the
 * selector key word 0xA02FCD270 is 1, (rate >> 6) > 0xC34). */
"  ldr x16,[x29,#0x38]\n"
"  ldr w16,[x16]\n"
"  lsr w17,w16,#7\n"
"  cmp w17,#0x752\n"
"  b.hi 2f\n"
"  lsr w16,w16,#6\n"
"  cmp w16,#0xc34\n"
"  b.ls 2f\n"
"  movz x16,#0xd270\n"
"  movk x16,#0x2fc,lsl #16\n"
"  movk x16,#0xa,lsl #32\n"
"  ldr w16,[x16]\n"
"  cmp w16,#1\n"
"  b.ne 2f\n"
"  mov w16,#800\n"
"  mov w17,#400\n"
"  udiv w27,w27,w16\n"
"  mul w27,w27,w17\n"
"  udiv w24,w24,w16\n"
"  mul w24,w24,w17\n"
"2:ldp x0,x1,[sp,#0x10]\n"
"  ldp x29,x30,[sp],#0x20\n"
"  ret\n"
".size z63_g5_threshold,.-z63_g5_threshold\n"
);

void *z63_g5_copy(void *out,const void *input,u64 bytes,u64 index) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G5):0;
    void *result=stock_memcpy(out,input,bytes);
    if(!ph||bytes!=H265_ROW_BYTES||index>=H265_ROWS||input!=(const void *)(uintptr_t)(H265_TABLE+H265_ROW_BYTES*index))
        return result;
    if(!z63g_act_enter(p,G5))return result;
    u64 e=z63g_effective(p);
    if(e) {
        struct z63_feature_settings v;
        z63_settings_decode((u32)e,&v);
        z63_h265_quality((u32)index,v.h265,out,(u32)bytes);
    }
    z63g_act_leave(p,G5);
    return result;
}
