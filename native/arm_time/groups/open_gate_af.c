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
/* G10 part 10c: AF transforms (area alias), AF area, zoom, navigator, display size and lit AF
 * points in open gate. Included by open_gate.c (same translation unit, after open_gate_geom.c).
 * Declared by z63patch/gate_groups/open_gate_af.py, which lists every record and its reason.
 *
 * Rules:
 *  - every record replaces a B or BL word (atomic install while half-press, touch or magnify run);
 *  - every adapter is the identity unless G10 is LIVE/DRAINING and og_plan_kind(p) != 0 (the plan
 *    latch in movie view: the kind stock last planned, moved only at stock's re-plan by g10_replan;
 *    re-plan build, before which this read og_size(), the choice): never in photo view;
 *  - the decisions are the old layout's (native/open_gate.c, native/open_gate_lit_points.S);
 *  - no clone, no rodata table (constants in code), no loop except the bounded exclusive shape.
 *
 * Area alias. The 16:9 crop decision of the forward 0x4104E4A8 / inverse 0x4104F124 transforms
 * (cmp of area - 4 with 4) sits on a non-branch word. A producer at each of their 10 BL callers
 * runs the transform on a copy of the plan whose area is the sentinel S = 0x80000004 (outside
 * 4..7, so the crop is skipped as the old 0x4104E5A4 / 0x4104F2E0 edits did); the three consumers
 * of the area inside each transform see 4 again (size-function call: a spec copy with area 4; family
 * call: family 0, the old family rule; 0x4105D2E0 call: area 4).
 * Provenance: a consumer acts only when the transform's saved return address (fwd [sp+0x148], inv
 * [sp+0x158] at the consumer's call) is the return address of a producer's ALIAS call and the area
 * is S. Then the plan register (x21 / x22, set once from x4, never stored through) holds the
 * producer's own copy, so S came from this patch. A 0x80000004 that stock might produce elsewhere
 * goes to stock unchanged. The consumers map in every phase (an integrity break or restore during
 * an aliased call must not leave S visible to stock); they store nothing outside their frames.
 * The producers hold ACT[G10 & 3] across the aliased call, so the restore pass (which waits for
 * every ACT count to be 0 before the first record goes back) never restores a consumer while an
 * aliased call is in flight; state +0x11 counts those calls (diagnostic). */
#ifndef OG_LOAD_P
#define OG_LOAD_P \
    "  movz x16,#0xae20\n" \
    "  movk x16,#0x028c,lsl #16\n" \
    "  movk x16,#0xa,lsl #32\n" \
    "  ldar x16,[x16]\n"
#endif
#ifndef OG_ARGS
#define OG_ARGS u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7
#define OG_PASS a0,a1,a2,a3,a4,a5,a6,a7
#endif

/* The replaced calls (each called only by the adapters of the records that replaced it). */
u64 stock_af_fwd(u64,u64,u64,u64,u64,u64,u64,u64);       /* 0x4104E4A8 forward AF transform */
u64 stock_af_inv(u64,u64,u64,u64,u64,u64,u64,u64);       /* 0x4104F124 inverse AF transform */
u64 stock_af_size(u64,u64,u64,u64,u64,u64,u64,u64);      /* 0x4105D550 size function */
u64 stock_af_family(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x4105D31C coordinate family */
u64 stock_af_d2e0(u64,u64,u64,u64,u64,u64,u64,u64);      /* 0x4105D2E0 */
u64 stock_af_prop(u64,u64,u64,u64,u64,u64,u64,u64);      /* 0x40F0C0B0 property read (w0, &key, &out) */
u64 stock_af_grid(u64,u64,u64,u64,u64,u64,u64,u64);      /* 0x40F286AC AF grid record (req, out 0x1C) */
u64 stock_af_cls_d24(u64,u64,u64,u64,u64,u64,u64,u64);   /* 0x40F0C1D8 (call after the class store 0x40F10D04) */
u64 stock_af_cls_e0ec(u64,u64,u64,u64,u64,u64,u64,u64);  /* 0x40D32F74 (call after the class store 0x40F1E0D8) */
u64 stock_zoom_get(u64,u64,u64,u64,u64,u64,u64,u64);     /* 0x403FFCD0 property getter (w0 = 0x2024) */
u64 stock_zoom_bounds(u64,u64,u64,u64,u64,u64,u64,u64);  /* 0x403973A4 zoom display bounds (record, w1, w2) */
u64 stock_disp_size(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x4105AA10 display size entry (area, row) */
u64 stock_lit_mode(u64,u64,u64,u64,u64,u64,u64,u64);     /* 0x40F1605C lit-point mode (area, w1) */
u64 stock_afa_join(u64,u64,u64,u64,u64,u64,u64,u64);     /* 0x40F18D2C join after the AF local-area override */

#define AF_S 0x80000004u

/* The old rule (native/open_gate.c og_af): open gate of size k, area 4 and the plan type of the
 * size (10/12 at 6K, 11/13 at 4K). */
static inline u32 af_rule(u32 k,u32 t,u32 a) {
    return a==4u&&((k==6u&&(t==10u||t==12u))||(k==4u&&(t==11u||t==13u)));
}
/* Open gate on for a LIVE/DRAINING G10: the plan latch (0 in PASS and outside movie view). */
static inline u32 af_on(u64 p,u32 ph){return p&&ph?og_plan_kind(p):0u;}

/* State +0x11: aliased calls in flight (bounded exclusive byte add; 1 or 0xFF). */
static void af_add8(u64 p,u32 k) {
    volatile u8 *a=og_st(p)+OG_AF_ALIAS;
    u32 v,s,t;
    __asm__ volatile(
        "mov %w[t],#64\n"
        "1: ldaxrb %w[v],[%[a]]\n"
        "add %w[v],%w[v],%w[k]\n"
        "stlxrb %w[s],%w[v],[%[a]]\n"
        "cbz %w[s],2f\n"
        "subs %w[t],%w[t],#1\n"
        "b.ne 1b\n"
        "2:\n"
        :[v]"=&r"(v),[s]"=&r"(s),[t]"=&r"(t):[a]"r"(a),[k]"r"(k):"memory","cc");
}

/* ---- F3 producers (asm frame, C decision) ----
 * g10_af_enter(plan): the page with ACT[G10 & 3] held when this call is aliased (LIVE, plan area
 * 4, the plan type of the latched kind), else 0 with nothing held. The hold now pins the latch: the
 * re-plan record does not move it while any ACT[G10 & 3] is counted. */
__attribute__((used)) u64 g10_af_enter(u64 plan) {
    u64 p=z63g_page();
    if(!p||!plan||!z63g_phase(p,G10))return 0;
    u32 t=z63g_ld32(plan),a=z63g_ld32(plan+4u);
    if(a!=4u||t<10u||t>13u)return 0;
    if(!z63g_act_enter(p,G10))return 0;
    u32 k=og_plan_kind_i(p);
    if(!af_rule(k,t,a)){z63g_act_leave(p,G10);return 0;}
    af_add8(p,1u);
    return p;
}
__attribute__((used)) void g10_af_leave(u64 p) {
    af_add8(p,0xffu);
    z63g_act_leave(p,G10);
}
/* After the aliased size call: the AF planes 1920 x 1080 -> 1920 x 1280 (3:2 as the active area),
 * old z63_og_af_geometry. Called only on provenance (the producer found the rule true for this
 * call), so the size comes from the plan type (10/12: 6K, 11/13: 4K) and not from the phase: one
 * aliased call is consistent from start to end even if the phase changes during it. spec: the
 * transform's spec (type at +0), d: the AF descriptor the size function wrote. */
__attribute__((used)) void g10_af_plane(u64 spec,u64 d) {
    if(!spec||!d)return;
    u32 t=z63g_ld32(spec);
    u32 active=(t==10u||t==12u)?4032u:(t==11u||t==13u)?2688u:0u;
    volatile u16 *v=(volatile u16 *)(uintptr_t)d;
    if(!active||v[0]!=1920u||v[1]!=1080u||v[9]!=1080u||v[0x13]!=1080u||v[0x15]!=1080u||v[7]!=active)return;
    v[1]=1280;v[9]=1280;v[0x13]=1280;v[0x15]=1280;
}

__asm__(
".text\n"
/* Producer: x0..x4 = point, size, out, out2, plan. Frame 0x90: args +0x10..+0x4F, page +0x50,
 * plan copy +0x58..+0x80 (0x28 bytes and the byte +0x28; the transform reads +0, +4, +8,
 * +0x10..+0x1F, +0x20 and the byte +0x28). Not aliased: the arguments back and a tail branch to
 * the stock transform (exactly the stock call). Aliased: the stock transform with x4 = the copy;
 * its return address is NAME_back (the consumers' provenance). */
".macro af_producer name, stock\n"
".p2align 2\n"
".global \\name\n"
".type \\name,%function\n"
"\\name:\n"
"  stp x29,x30,[sp,#-0x90]!\n"
"  mov x29,sp\n"
"  stp x0,x1,[sp,#0x10]\n"
"  stp x2,x3,[sp,#0x20]\n"
"  stp x4,x5,[sp,#0x30]\n"
"  stp x6,x7,[sp,#0x40]\n"
OG_LOAD_P
"  mov x0,x4\n"
"  bl g10_af_enter\n"
"  str x0,[sp,#0x50]\n"
"  cbz x0,1f\n"
"  ldr x4,[sp,#0x30]\n"
"  ldr x8,[x4]\n"
"  ldr x9,[x4,#8]\n"
"  ldr x10,[x4,#0x10]\n"
"  ldr x11,[x4,#0x18]\n"
"  ldr x12,[x4,#0x20]\n"
"  ldrb w13,[x4,#0x28]\n"
"  str x8,[sp,#0x58]\n"
"  str x9,[sp,#0x60]\n"
"  str x10,[sp,#0x68]\n"
"  str x11,[sp,#0x70]\n"
"  str x12,[sp,#0x78]\n"
"  strb w13,[sp,#0x80]\n"
"  movz w8,#4\n"
"  movk w8,#0x8000,lsl #16\n"
"  str w8,[sp,#0x5c]\n"
"  ldp x0,x1,[sp,#0x10]\n"
"  ldp x2,x3,[sp,#0x20]\n"
"  ldr x5,[sp,#0x38]\n"
"  ldp x6,x7,[sp,#0x40]\n"
"  add x4,sp,#0x58\n"
"  bl \\stock\n"
".global \\name\\()_back\n"
"\\name\\()_back:\n"
"  str x0,[sp,#0x10]\n"
"  ldr x0,[sp,#0x50]\n"
"  bl g10_af_leave\n"
"  ldr x0,[sp,#0x10]\n"
"  ldp x29,x30,[sp],#0x90\n"
"  ret\n"
"1:\n"
"  ldp x0,x1,[sp,#0x10]\n"
"  ldp x2,x3,[sp,#0x20]\n"
"  ldp x4,x5,[sp,#0x30]\n"
"  ldp x6,x7,[sp,#0x40]\n"
"  ldp x29,x30,[sp],#0x90\n"
"  b \\stock\n"
".size \\name,.-\\name\n"
".endm\n"
/* Size-function consumer (lookup: P first). OFF: the transform's saved x30 relative to the sp at
 * this call; BACK: the producer's alias return address. On provenance and area S: the size
 * function on a copy of the 0x20-byte spec with area 4 (nothing stored outside this frame), then
 * the AF planes (g10_af_plane). Otherwise a tail branch to the size function. */
".macro af_size name, off, back\n"
".p2align 2\n"
".global \\name\n"
".type \\name,%function\n"
"\\name:\n"
"  stp x29,x30,[sp,#-0x50]!\n"
"  mov x29,sp\n"
"  stp x0,x1,[sp,#0x10]\n"
OG_LOAD_P
"  ldr x16,[sp,#(0x50+\\off)]\n"
"  adr x17,\\back\n"
"  cmp x16,x17\n"
"  b.ne 1f\n"
"  ldr w16,[x0,#4]\n"
"  movz w17,#4\n"
"  movk w17,#0x8000,lsl #16\n"
"  cmp w16,w17\n"
"  b.ne 1f\n"
"  ldp x16,x17,[x0]\n"
"  stp x16,x17,[sp,#0x20]\n"
"  ldp x16,x17,[x0,#0x10]\n"
"  stp x16,x17,[sp,#0x30]\n"
"  mov w16,#4\n"
"  str w16,[sp,#0x24]\n"
"  add x0,sp,#0x20\n"
"  bl stock_af_size\n"
"  str x0,[sp,#0x40]\n"
"  ldp x0,x1,[sp,#0x10]\n"
"  bl g10_af_plane\n"
"  ldr x0,[sp,#0x40]\n"
"  ldp x29,x30,[sp],#0x50\n"
"  ret\n"
"1:\n"
"  ldp x0,x1,[sp,#0x10]\n"
"  ldp x29,x30,[sp],#0x50\n"
"  b stock_af_size\n"
".size \\name,.-\\name\n"
".endm\n"
/* Family / 0x4105D2E0 consumers: S class, no gate lookup, a pure map. On provenance and w1 == S:
 * family 0 (FAM 1: return w0 = 0, the old family rule, which held when the producer aliased) or
 * w1 = 4 and the stock function. Everything else: a tail branch to the stock function. Only x16,
 * x17 and NZCV change (a call boundary). */
".macro af_unmap name, off, back, stock, fam\n"
".p2align 2\n"
".global \\name\n"
".type \\name,%function\n"
"\\name:\n"
"  ldr x16,[sp,#\\off]\n"
"  adr x17,\\back\n"
"  cmp x16,x17\n"
"  b.ne 1f\n"
"  movz w16,#4\n"
"  movk w16,#0x8000,lsl #16\n"
"  cmp w1,w16\n"
"  b.ne 1f\n"
"  .if \\fam\n"
"  mov w0,wzr\n"
"  ret\n"
"  .else\n"
"  mov w1,#4\n"
"  .endif\n"
"1: b \\stock\n"
".size \\name,.-\\name\n"
".endm\n"
"af_producer g10_af_fwd, stock_af_fwd\n"
"af_producer g10_af_inv, stock_af_inv\n"
"af_size g10_af_size_f, 0x148, g10_af_fwd_back\n"
"af_size g10_af_size_i, 0x158, g10_af_inv_back\n"
"af_unmap g10_af_fam_f, 0x148, g10_af_fwd_back, stock_af_family, 1\n"
"af_unmap g10_af_fam_i, 0x158, g10_af_inv_back, stock_af_family, 1\n"
"af_unmap g10_af_d2e0_f, 0x148, g10_af_fwd_back, stock_af_d2e0, 0\n"
"af_unmap g10_af_d2e0_i, 0x158, g10_af_inv_back, stock_af_d2e0, 0\n"
);

/* ---- coordinate family at its 7 other callers (old entry hook 0x4105D31C: family 0 under the
 * rule, with w0 = plan type, w1 = area) ---- */
u64 g10_af_family(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    if(ph&&(u32)a1==4u&&(u32)a0-10u<=3u&&af_rule(og_plan_kind(p),(u32)a0,(u32)a1))return 0;
    return stock_af_family(OG_PASS);
}

/* ---- AF local area (old site 0x40F18D38 str w8,[sp,#0x158]; property 0x2024) ----
 * Getter path: the property read 0x40F0C0B0(1, &key, &out) at 0x40F18D14; with success (w0 bit 0)
 * an area 4 in [out] becomes 0 in open gate. Override path: B 0x40F18CF8 (w8 = [x23+0x94]) into
 * g10_afa_local_ovr, which keeps every register but w8 and branches to the join 0x40F18D2C. */
__attribute__((used)) u64 g10_af_area4(u64 w) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    return (u32)w==4u&&af_on(p,ph)?0u:(u32)w;
}
u64 g10_afa_local_get(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u64 r=stock_af_prop(OG_PASS);
    if(!ph||!(r&1u)||!a1||!a2||z63g_ld32(a1)!=0x2024u||z63g_ld32(a2)!=4u)return r;
    if(og_plan_kind(p))*(volatile u32 *)(uintptr_t)a2=0;
    return r;
}
__asm__(
".text\n"
".p2align 2\n"
".global g10_afa_local_ovr\n"
".type g10_afa_local_ovr,%function\n"
"g10_afa_local_ovr:\n"
"  stp x29,x30,[sp,#-0xa0]!\n"
"  mov x29,sp\n"
"  stp x0,x1,[sp,#0x10]\n"
"  stp x2,x3,[sp,#0x20]\n"
"  stp x4,x5,[sp,#0x30]\n"
"  stp x6,x7,[sp,#0x40]\n"
"  stp x8,x9,[sp,#0x50]\n"
"  stp x10,x11,[sp,#0x60]\n"
"  stp x12,x13,[sp,#0x70]\n"
"  stp x14,x15,[sp,#0x80]\n"
"  stp x16,x17,[sp,#0x90]\n"
OG_LOAD_P
"  mov w0,w8\n"
"  bl g10_af_area4\n"
"  str x0,[sp,#0x50]\n"
"  ldp x0,x1,[sp,#0x10]\n"
"  ldp x2,x3,[sp,#0x20]\n"
"  ldp x4,x5,[sp,#0x30]\n"
"  ldp x6,x7,[sp,#0x40]\n"
"  ldp x8,x9,[sp,#0x50]\n"
"  ldp x10,x11,[sp,#0x60]\n"
"  ldp x12,x13,[sp,#0x70]\n"
"  ldp x14,x15,[sp,#0x80]\n"
"  ldp x16,x17,[sp,#0x90]\n"
"  ldp x29,x30,[sp],#0xa0\n"
"  b stock_afa_join\n"
".size g10_afa_local_ovr,.-g10_afa_local_ovr\n"
);

/* ---- AF grid record 0x40F286AC(req, out) (old sites 0x40F28770 / 0x40F28828, z63_og_af_grid):
 * the record is table + kind * 0xFC + area * 0x1C, copied (0x1C bytes) to out. In open gate an area
 * 4 request reads the area-0 record when that record exists (not all zero) and the kind is <= 8;
 * otherwise the area-4 record. The function reads req +0..+0x1B only and stores only to out
 * (memcpy), so the area-0 record is read by the stock function on a copy of the request: first into
 * a frame buffer (does it exist?), then into out. Kind > 8 only on the path (type & ~1) == 0xC:
 * kind = table 0xA02B91E08[req+8 - 1] (76 entries), else 0. */
#define AF_GRID_KINDS 0xa02b91e08ull
static u32 af_grid_kind_ok(u64 req) {
    if((z63g_ld32(req)&~1u)!=0xcu)return 1;
    u32 k=z63g_ld32(req+8u)-1u;
    if(k>0x4bu)return 1;
    return z63g_ld64(AF_GRID_KINDS+8u*k)<=8u;
}
u64 g10_afa_grid(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    if(!ph||!a0||!a1||z63g_ld32(a0+0xcu)!=4u||!og_plan_kind(p)||!af_grid_kind_ok(a0))return stock_af_grid(OG_PASS);
    volatile u32 req[8],rec[8];
    #pragma clang loop unroll(full)
    for(u32 i=0;i<7u;i++)req[i]=z63g_ld32(a0+4u*i);
    req[3]=0;
    stock_af_grid((u64)(uintptr_t)req,(u64)(uintptr_t)rec,a2,a3,a4,a5,a6,a7);
    u32 any=0;
    #pragma clang loop unroll(full)
    for(u32 i=0;i<7u;i++)any|=rec[i];
    if(!any)return stock_af_grid(OG_PASS);
    return stock_af_grid((u64)(uintptr_t)req,a1,a2,a3,a4,a5,a6,a7);
}

/* ---- AF class (old sites 0x40F10D04 str w8,[x19,#0x3c8] and 0x40F1E0D8 str w8,[x21,#0x3c8]):
 * class = table 0xA02B857FC[area - 2] (area 4 -> 1); with info area +0x390 == 4 the old layout
 * stored 0. The next instruction that is a branch on the only path after each store is the BL at
 * 0x40F10D24 / 0x40F1E0EC (no other predecessor, nothing between reads +0x3C8 or writes +0x390):
 * before that call the class of info area 4 becomes 0, then a tail branch to the stock callee. */
__attribute__((used)) void g10_af_class_fix(u64 self) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    if(!ph||!self||z63g_ld32(self+0x390u)!=4u||!z63g_ld32(self+0x3c8u))return;
    if(og_plan_kind(p))*(volatile u32 *)(uintptr_t)(self+0x3c8u)=0;
}
__asm__(
".text\n"
".macro af_class name, reg, stock\n"
".p2align 2\n"
".global \\name\n"
".type \\name,%function\n"
"\\name:\n"
"  stp x29,x30,[sp,#-0x50]!\n"
"  mov x29,sp\n"
"  stp x0,x1,[sp,#0x10]\n"
"  stp x2,x3,[sp,#0x20]\n"
"  stp x4,x5,[sp,#0x30]\n"
"  stp x6,x7,[sp,#0x40]\n"
OG_LOAD_P
"  mov x0,\\reg\n"
"  bl g10_af_class_fix\n"
"  ldp x0,x1,[sp,#0x10]\n"
"  ldp x2,x3,[sp,#0x20]\n"
"  ldp x4,x5,[sp,#0x30]\n"
"  ldp x6,x7,[sp,#0x40]\n"
"  ldp x29,x30,[sp],#0x50\n"
"  b \\stock\n"
".size \\name,.-\\name\n"
".endm\n"
"af_class g10_cls_d24, x19, stock_af_cls_d24\n"
"af_class g10_cls_e0ec, x21, stock_af_cls_e0ec\n"
);

/* ---- zoom area (old z63_og_zoom_area): the property 0x2024 the magnify path and the navigator
 * read; area 4 becomes 0 in open gate. ---- */
u64 g10_zoom_area(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u64 r=stock_zoom_get(OG_PASS);
    if(ph&&(u32)a0==0x2024u&&(u32)r==4u&&og_plan_kind(p))return 0;
    return r;
}

/* The navigator inset is deferred: its exact site 0x40429A64 is G7's record (open_gate_af.py, R-AF-3). */

/* ---- zoom display bounds 0x403973A4(record, w1, w2) (old z63_og_zoom_bounds at its only return
 * path, 0x403974F8): a 16:9 source rectangle gets the full-height rows the same function writes
 * for its other cases. Record u16: +0 w, +2 h, +4 x0, +6 y0, +8 x1, +0xA y1. ---- */
static void af_bounds(volatile u16 *b,u32 w,u32 h,u32 x0,u32 y0,u32 x1,u32 y1,u32 ny0,u32 ny1,u32 *done) {
    if(*done||b[0]!=w||b[1]!=h||b[2]!=x0||b[3]!=y0||b[4]!=x1||b[5]!=y1)return;
    b[3]=(u16)ny0;b[5]=(u16)ny1;*done=1;
}
u64 g10_zoom_bounds(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u64 r=stock_zoom_bounds(OG_PASS);
    if(!ph||!a0||!og_plan_kind(p))return r;
    volatile u16 *b=(volatile u16 *)(uintptr_t)a0;
    u32 done=0;
    af_bounds(b,1024,682,0,54,1024,630,0,682,&done);          /* rear monitor */
    af_bounds(b,1600,1200,0,152,1600,1052,66,1132,&done);     /* viewfinder (display type 4) */
    af_bounds(b,1600,1200,160,238,1440,958,174,1026,&done);   /* viewfinder, small size */
    return r;
}

/* ---- fallback display size (old z63_og_display_size): 0x4105AA10(area, row) at 0x41058894; the
 * (area 4, row 2) request gets the area-0 entry in open gate. ---- */
u64 g10_disp_size(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u64 r=stock_disp_size(OG_PASS);
    if(ph&&(u32)a0==4u&&(u32)a1==2u&&og_plan_kind(p))return stock_disp_size(0,2,a2,a3,a4,a5,a6,a7);
    return r;
}

/* ---- lit AF points (photo layout): 0x40F1605C(area, w1) at 0x40F14E98;
 * mode 4 (16:9 layout) for area 4 with w1 != 6 becomes mode 0, the camera's photo layout. ---- */
u64 g10_lit_mode(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u64 r=stock_lit_mode(OG_PASS);
    if(ph&&(u32)r==4u&&(u32)a0==4u&&(u32)a1!=6u&&og_plan_kind(p))return 0;
    return r;
}
