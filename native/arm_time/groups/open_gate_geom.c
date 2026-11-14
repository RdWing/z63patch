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
/* G10 part 10a: geometry records. Included by open_gate.c (same translation
 * unit). Declared by z63patch/gate_groups/open_gate_geom.py.
 *
 * Two adapter shapes:
 *
 *  og_tail_begin/og_set/og_tail_end (asm; B->B block tail): the five old value
 *    sites ("mov wREG,#STOCK", non-branch words) stay stock; the B that ends each straight-line
 *    block holding them becomes "b NAME". The block between does not touch the site registers and
 *    NZCV is dead at the B (static guards in the tests), so the adapter saves x0-x17, x29, x30,
 *    acquire-loads P (first access outside the stack), calls g10_value(STOCK, OPEN, SIZE) once per
 *    site in address order, puts each result in its wREG, restores every other register and
 *    leaves by "b" to the B's stock target. PASS: STOCK (the register keeps the stock value).
 *    Stack 0xA0 + g10_value.
 *
 *  call-site post-fix (C; BL->BL, or B->B call-and-return for a tail branch, like the lv_display
 *    site): z63g_page() first, the phase, then the stock function once with the caller's x0-x7,
 *    then (LIVE/DRAINING only) an edit of the output the stock function just wrote, only on the
 *    exact input path that executes the old layout's value site (predicate replicated from the
 *    inputs; tests replay the stock leaves with a code trace). Returns stock's x0 (the
 *    classifier: 8 for 10 on its path). The C calling convention is enough here: no caller reads
 *    a caller-saved register after these calls before writing it (static guard in the tests).
 *    No stack arguments at any of these call sites (<= 3 register arguments). */

/* Stock functions the records' adapters call (the replaced calls themselves). */
u64 stock_og_plan(u64,u64,u64,u64,u64,u64,u64,u64);        /* 0x4105D07C */
u64 stock_og_leaf_eb40(u64,u64,u64,u64,u64,u64,u64,u64);   /* 0x4105EB40 4K reduced pair */
u64 stock_og_leaf_ebc0(u64,u64,u64,u64,u64,u64,u64,u64);   /* 0x4105EBC0 coded pair */
u64 stock_og_leaf_ec38(u64,u64,u64,u64,u64,u64,u64,u64);   /* 0x4105EC38 active pair */
u64 stock_og_leaf_ee00(u64,u64,u64,u64,u64,u64,u64,u64);   /* 0x4105EE00 size class pair */
u64 stock_og_classify(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x41175E5C sensor-mode classifier */
u64 stock_og_dims(u64,u64,u64,u64,u64,u64,u64,u64);        /* 0x4105B32C dims query (plan call, then width/rows out) */
u64 stock_og_memcpy(u64,u64,u64,u64,u64,u64,u64,u64);      /* 0x4163B5C0 memcpy */

#define OG_LOAD_P \
    "  movz x16,#0xae20\n" \
    "  movk x16,#0x028c,lsl #16\n" \
    "  movk x16,#0xa,lsl #32\n" \
    "  ldar x16,[x16]\n"
#define OG_ARGS u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7
#define OG_PASS a0,a1,a2,a3,a4,a5,a6,a7

__asm__(
".macro og_set reg, stock, open, size\n"
"  movz w0,#\\stock\n"
"  movz w1,#\\open\n"
"  movz w2,#\\size\n"
"  bl g10_value\n"
"  .if \\reg < 18\n"
"  str x0,[sp,#(0x10+8*\\reg)]\n"
"  .else\n"
"  mov w\\reg,w0\n"
"  .endif\n"
".endm\n"
".macro og_tail_begin name\n"
".text\n"
".p2align 2\n"
".global \\name\n"
".type \\name,%function\n"
"\\name:\n"
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
".endm\n"
".macro og_tail_end name, target\n"
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
"  b \\target\n"
".size \\name,.-\\name\n"
".endm\n"
/* 2.2 size function 0x4105D550: the five old value sites (open_gate.S sites 24, 14, 15, 3, 4) stay
 * stock; each straight-line block that holds them ends in a B, which becomes the record. */
"og_tail_begin g10_t_d78c\n"                       /* b 0x4105DD44 after 0x4105D784 */
"og_set 20, 0xd68, 0xfc8, 6\n"                     /*   N-RAW 6K size readout rows (plans 10/12) */
"og_tail_end g10_t_d78c, stock_og_sz_dd44\n"
"og_tail_begin g10_t_d804\n"                       /* b 0x4105E078 after 0x4105D7F0, 0x4105D7F8 */
"og_set 23, 0xd68, 0xfc8, 4\n"                     /*   4K size readout rows */
"og_set 20, 0x8f0, 0xa84, 4\n"                     /*   4K size reduced rows */
"og_tail_end g10_t_d804, stock_og_sz_e078\n"
"og_tail_begin g10_t_da18\n"                       /* b 0x4105E184 after 0x4105D9F0, 0x4105DA0C */
"og_set 20, 0xd68, 0xfc8, 6\n"                     /*   size readout rows (plan 5 branch) */
"og_set 13, 0xd4a, 0xfc0, 6\n"                     /*   size active rows (plan 5 branch) */
"og_tail_end g10_t_da18, stock_og_sz_e184\n"
);

/* Value sites: STOCK in PASS; OPEN when the plan kind (og_plan_kind: the latch) is SIZE (0:
 * either). Re-plan build: the tables are no longer moved here; they move with the latch only
 * (g10_replan, the plan post-fix lowering, restore and break), so a site never changes them. */
__attribute__((used)) u64 g10_value(u32 stock,u32 open,u32 size) {
    u64 p=z63g_page();
    u32 k=og_plan_kind_i(p);
    return k&&(!size||size==k)?open:stock;
}

/* Common post-fix entry: the plan kind, 0x100 when live with no open-gate plan, 0 in PASS. Loads
 * only (the tables follow the latch, not the reader). */
static u32 og_post_kind(u64 p) {
    if(!p||!z63g_phase(p,G10))return 0;
    u32 k=og_plan_kind_i(p);
    return k?k:0x100u;
}
static u32 in32(u64 base,u32 at){return z63g_ld32(base+at);}
static u32 in8(u64 base){return *(volatile const u8 *)(uintptr_t)base;}
/* The record pointer must not alias the inputs (the predicate is read after the stock call). */
static u32 og_apart(u64 in,u64 out){return in&&out&&(out+4u<=in||out>=in+0x30u);}

/* 2.1 plan function 0x4105D07C(w0, w1, x2 = plan spec): records 0x4105B368 (bl) and 0x4105AE3C
 * (b, tail call-and-return). Jump table 0xA02BC76F8 on spec type [x2] (<= 0x17): types 5, 10, 12
 * and 6, 9 with area [x2+4] == 4 reach 0x4105D13C (6K: 6064 x 3432, frontend block 0xA02BC8090
 * into record 0xA0298ECA0 +0x202); types 11, 13 with area 4 reach 0x4105D178 (4K record
 * 0xA0298EEBC: only the block is copied). The old value sites 0x4105D140 / 0x4105D144 /
 * 0x4105D17C (3432 -> 4040, block 0xA02BC8090 -> 0xA02BC8070, the photo-path values) become
 * post-fixes of that record on exactly those paths. The plan latch in state follows. */
#define OG_REC6 0xa0298eca0ull
#define OG_REC4 0xa0298eebcull
#define OG_BLOCK_STOCK 0xa02bc8090ull
#define OG_BLOCK_OPEN 0xa02bc8070ull
static u32 og_plan_path(u64 spec) {
    if(!spec)return 0;
    u32 t=in32(spec,0),area=in32(spec,4);
    if(area!=4u)return 0;
    if(t==5u||t==6u||t==9u||t==10u||t==12u)return 6;
    if(t==11u||t==13u)return 4;
    return 0;
}
/* 1 when the stock plan took a branch that chooses and writes a record (every branch except the
 * fall-through to 0x4105D290, which writes no geometry), replicated from the jump table
 * 0xA02BC76F8 and its branches: types 0, 1, 23 always; 2, 3, 4, 8 with [x2+8] >= 2 in area 4 or
 * 5, else 4 and 8 in area 4; 5, 10, 12 in area 4; 6, 9, 11, 13 in area 4 or 5; everything else
 * (7, 14-22, > 23, and 2, 3 with [x2+8] < 2) only with w1 bit 0, in area 0 or 1. The stock
 * function rewrites its one output record (0xA0298ECA0; 0xA0298EEBC for the 4K branch) on each
 * such call, so the record the caller gets is the last call's: the latch follows that call
 * (R-PLAY-3: before this, only the open-gate paths wrote it, so after an open-gate plan any
 * other plan, e.g. the 4000-wide area-5 branch 0x4105D1DC or a 16:9 movie type 2/3/4/8, left
 * og_active at 1 and G7 used open-gate maps for a stock movie). */
static u32 og_plan_selects(u64 spec,u32 flags) {
    if(!spec)return 0;
    u32 t=in32(spec,0),area=in32(spec,4);
    u32 bit=t<32u?1u<<t:0u;        /* masks, not a switch: no jump table (no indirect branch) */
    if(bit&(1u<<0|1u<<1|1u<<23))return 1;
    if(bit&(1u<<2|1u<<3|1u<<4|1u<<8)) {
        if(in32(spec,8)>=2u)return area==4u||area==5u;
        if(bit&(1u<<4|1u<<8))return area==4u;
    } else if(bit&(1u<<5|1u<<10|1u<<12))return area==4u;
    else if(bit&(1u<<6|1u<<9|1u<<11|1u<<13))return area==4u||area==5u;
    return (flags&1u)&&area<=1u;
}
/* Re-plan build, per-frame rule (emulator: the plan function runs 20-100 times a second
 * on two CPUs, not only when stock re-plans; a card pull drops the readiness word with no re-plan).
 * The post-fix NEVER lowers the latch, the plan mode or the tables on a call whose plan the readers
 * still hold, and never decides open gate either: it only re-applies the latch to the record stock
 * has just rewritten, so per-frame calls keep record, tables and readers where the last real re-plan
 * put them. Lowering belongs to g10_replan (the applier, also with readiness 0 while the plan mode
 * holds), the photo-plan branch here, and the restore and break callbacks.
 *  - Hold (og_hold, from the one state snapshot): readiness word 0xA048BB160 == 1, or plan mode set
 *    and movie live view running (og_running_movie). The same rule as the readers (og_plan_kind_i),
 *    so the post-fix applies exactly while the readers act.
 *  - Photo plan: no hold and not an open-gate path. The readers are already stock (same rule), so
 *    the plan mode goes off and the tables to stock here (idempotent per frame: photo frames repeat it).
 *  - Apply: latch 6 or 4, hold, the path is the latched kind's, slots still allow it (file type 6,
 *    size slot == kind, rate slot 6..8; no readiness term). The tables go to the latch first, then
 *    the record's patched fields, each of which must hold its stock or open value (another core may
 *    be mid-way through either). A field holding neither, the wrong record or a wrong width marks
 *    OG_PLAN_MARK (+0x18) and leaves everything alone: the next g10_replan lowers. A slot change or
 *    another path also leaves everything alone (g10_replan re-decides from the slots at the applier).
 *  - Two cores: after its stores the post-fix re-reads the state word; if a writer changed it since
 *    the snapshot (a lowering by g10_replan or the photo branch, a raise to the other kind), it puts
 *    the tables to what that state holds and the record fields back to stock when no hold remains
 *    for the kind it applied: a lowering is never undone by a late apply. Lowering writers store
 *    the state before the tables, with a barrier, so either they see the apply's tables or the apply
 *    sees their state.
 *  - Everything runs inside an ACT hold (index OG_FIX_ACT; the break callback and the restore pass
 *    wait for every ACT count 0, and the hold re-reads the phase after its increment). */
static u32 og_plan_slots(u32 k) {
    if(z63g_ld32(OG_SLOT_FILE_TYPE)!=6u||z63g_ld32(Z63_SLOT_KEY_0F4E)!=k)return 0;
    u32 rate=z63g_ld32(Z63_SLOT_KEY_0F50);
    return rate>=6u&&rate<=8u;
}
#define OG_FIX_ACT 3u
static u32 og_fix_enter(u64 p) {
    volatile u32 *act=(volatile u32 *)(uintptr_t)(p+Z63_GATE_OFF+Z63_G_ACT+4u*OG_FIX_ACT);
    u32 r=z63g_inc8(act);
    if(r) {if(r==1)z63g_fault(p,Z63_F_OVERFLOW);return 0;}
    z63g_dmb();
    if(z63g_phase(p,G10))return 1;
    r=z63g_dec64(act);if(r)z63g_fault(p,r==1?Z63_F_UNDERFLOW:Z63_F_LEAK);
    return 0;
}
static void og_fix_leave(u64 p) {
    u32 r=z63g_dec64((volatile u32 *)(uintptr_t)(p+Z63_GATE_OFF+Z63_G_ACT+4u*OG_FIX_ACT));
    if(r)z63g_fault(p,r==1?Z63_F_UNDERFLOW:Z63_F_LEAK);
}
/* 1 when every u16 of the 16-byte block at `at` holds its stock or its open-gate value. */
static u32 og_block_mixed(u64 at) {
    u32 ok=1;
    #pragma clang loop unroll(full)
    for(u32 i=0;i<16u;i+=2u){u32 v=og_ld16(at+i);ok&=v==og_ld16(OG_BLOCK_STOCK+i)||v==og_ld16(OG_BLOCK_OPEN+i);}
    return ok;
}
static void og_put_block(u64 to,u64 from) {  /* 16 bytes, u16 stores (the record field is 2-aligned) */
    #pragma clang loop unroll(full)
    for(u32 i=0;i<16u;i+=2u)*(volatile u16 *)(uintptr_t)(to+i)=og_ld16(from+i);
}
static u32 og_row_mixed(u64 a){u32 v=og_ld16(a);return v==3432u||v==4040u;}
static void og_rec6_rows(u32 v) {
    *(volatile u16 *)(uintptr_t)(OG_REC6+2)=(u16)v;
    *(volatile u16 *)(uintptr_t)(OG_REC6+0x19e)=(u16)v;
    *(volatile u16 *)(uintptr_t)(OG_REC6+0x1f4)=(u16)v;
}
static u32 og_snap(u64 p){return z63g_acq32((u64)(uintptr_t)og_st(p));}
static u32 og_snap_kind(u32 s){u32 k=(s>>8)&0xffu;return k==6u||k==4u?k:0u;}
static u32 og_hold(u32 s){return z63g_ld32(OG_MOVIE_WORD)==1u||((s>>24)&0xffu&&og_running_movie());}
u64 g10_plan(OG_ARGS) {
    u64 p=z63g_page(),spec=a2;
    u32 ph=p?z63g_phase(p,G10):0;
    u32 selects=ph?og_plan_selects(spec,(u32)a1):0;     /* read before the call (the spec is the caller's) */
    u64 result=stock_og_plan(OG_PASS);
    if(!selects||!og_fix_enter(p))return result;
    u32 snap=og_snap(p),on=og_snap_kind(snap),path=og_plan_path(spec);
    if(!og_hold(snap)) {
        /* No hold: readers are stock. A photo plan (no open-gate path) turns the plan mode off and
         * the tables to stock (state first, then tables). An open-gate path without hold: nothing. */
        if(!path) {
            /* Leaving a movie plan (plan mode was on): the latch goes with it, as in g10_replan's
             * lowering form (mode and latch first, a barrier, then the tables). In the emulator,
             * a card pull, then the no-card menu close: this branch cleared the plan mode
             * and the tables but kept latch 6; the applier then refused (plan mode already 0), and at
             * card-in the readiness word came back to 1, which is a hold on the stale latch: a
             * per-frame call re-applied tables and record with no stock re-plan. With the latch at 0
             * open gate comes back only through g10_replan (a real re-plan) or, in photo view, the
             * pre-set z63p_g10 (plan mode off) followed by the movie re-plan. A latch set by
             * z63p_g10 in photo view (plan mode off) is left. */
            u32 left=(snap>>24)&0xffu;
            if(left)og_latch(p,0,0);
            else og_mode(p,0);
            z63g_dmb();
            if(og_st(p)[OG_SYNC_STATE])og_sync(p,0);
            if(left)og_put8(p,OG_REPLAN_LAST,0xA0u);     /* diagnostic: lowered by the plan post-fix */
        }
        og_fix_leave(p);
        return result;
    }
    if(!on||path!=on||!og_plan_slots(on)) {og_fix_leave(p);return result;}   /* not this latch's plan: left */
    og_sync(p,on);                                       /* tables to the latch first (FOREIGN: reported in +2, g10_replan lowers) */
    u32 applied=0;
    if(on==6u) {
        if(result==OG_REC6&&og_ld16(OG_REC6+6)==6064u&&og_row_mixed(OG_REC6+2)&&og_row_mixed(OG_REC6+0x19e)&&
           og_row_mixed(OG_REC6+0x1f4)&&og_block_mixed(OG_REC6+0x202)) {
            og_rec6_rows(4040);og_put_block(OG_REC6+0x202,OG_BLOCK_OPEN);applied=6;
        }
    } else if(result==OG_REC4&&og_block_mixed(OG_REC4+0x202)) {og_put_block(OG_REC4+0x202,OG_BLOCK_OPEN);applied=4;}
    if(!applied)og_put8(p,OG_PLAN_MARK,0x80u|on);       /* a foreign record: lowered at the next re-plan */
    else if(!((snap>>24)&0xffu))z63g_cas32((u64)(uintptr_t)og_st(p),snap,snap|0x01000000u);   /* plan mode on, only over the snapshot */
    z63g_dmb();
    u32 now=og_snap(p);
    if(((now^snap)&0xffffu)||((snap>>24)&&!(now>>24))) {   /* +0/+1 changed, or the plan mode went off (+2 is og_sync's own) */
        /* another writer changed the latch or the plan mode since the snapshot: follow it */
        u32 k=og_snap_kind(now),keep=k==on&&og_hold(now);
        og_sync(p,k&&og_hold(now)?k:0u);
        if(!keep&&applied==6u){og_rec6_rows(3432);og_put_block(OG_REC6+0x202,OG_BLOCK_STOCK);}
        else if(!keep&&applied==4u)og_put_block(OG_REC4+0x202,OG_BLOCK_STOCK);
    }
    og_fix_leave(p);
    return result;
}

/* ---- consumers of the shared record (emulator: 3 in ~7,450 per-frame calls
 * the dims query read rows 3432 at 0x4105B38C, 6-17 us after the OTHER core's stock store at
 * 0x4105D158 and before that core's post-fix) ----
 * The record 0xA0298ECA0 is rewritten to stock by every plan call on either CPU, so a reader of the
 * record can see stock rows between another core's stock store and its post-fix, whatever this core's
 * post-fix did. The two static readers of the patched fields (scratch recscan.py) get their outputs
 * fixed AFTER they have read, under the same single-snapshot hold as g10_plan, so what they hand on no
 * longer depends on the record:
 *  - g10_dims, BL->BL at the five calls of the dims query 0x4105B32C (0x4105B2C4/B3C4/B44C/B4D4/B55C,
 *    its only callers; parents read only x19/x21 and w0 is rewritten after the call, NZCV not read):
 *    on the latched 6K path (spec, slots) the rows it wrote at [x1+2] (from +0x19E or +0x1F4) go
 *    3432 -> 4040; a value that is neither, or a width other than 6064 at [x1], is left. 4K: the rows
 *    it reads (REC4 +0x19E/+0x1F4) are og_sync table words that stock's plan never stores (0x4105D178
 *    branch: only the block), so there is nothing to race and nothing is changed.
 *  - g10_sens, BL->BL at the sensor setup's second memcpy 0x410AD7AC (stock BL 0x4163B5C0, dst
 *    0xA04AFCA5C, src record + 0x19C, 0x78 bytes; the first memcpy 0x410AD79C copied record +0..0x1B
 *    to 0xA04AFCA40 just before, nothing reads either copy in between; after the call only x23/x24/
 *    x20/x22 and fresh registers are read, NZCV not read): latched 6K and source 0xA0298ECA0 with
 *    width 6064 in the copy and every patched field stock-or-open in the COPY: rows (copy1 +2, copy2
 *    +2, +0x58) 4040 and the block (copy2 +0x66) open; latched 4K and source 0xA0298EEBC: the block.
 *    The record's path is not known here (no spec): the content test is enough because the only plan
 *    branches that write 6064 with rows 3432 or 4040 and blocks 0x8090/0x8070 are the 6K movie path
 *    0x4105D13C (stock 3432, block 0x8090) and the photo path 0x4105D218 (4040 and 0x8070, already
 *    the open values), so the fix-up maps the first to its open values and leaves the second as is.
 * Both run in the ACT[3] hold (the break callback and restore wait for it). */
static u32 og_hold_kind(u64 p,u32 *snap) {
    u32 s=og_snap(p),k=og_snap_kind(s);
    *snap=s;
    return k&&og_hold(s)&&og_plan_slots(k)?k:0u;
}
u64 g10_dims(OG_ARGS) {
    u64 p=z63g_page(),spec=a0,out=a1;
    u32 ph=p?z63g_phase(p,G10):0;
    u32 path=ph?og_plan_path(spec):0;                   /* read before the call (the spec is the caller's) */
    u64 result=stock_og_dims(OG_PASS);
    if(path!=6u||!(u32)result||!out||!og_fix_enter(p))return result;
    u32 s;
    if(og_hold_kind(p,&s)==6u&&og_ld16(out)==6064u&&og_ld16(out+2)==3432u)*(volatile u16 *)(uintptr_t)(out+2)=4040;
    og_fix_leave(p);
    return result;
}
#define OG_SENS_COPY1 0xa04afca40ull
#define OG_SENS_COPY2 0xa04afca5cull
u64 g10_sens(OG_ARGS) {
    u64 p=z63g_page(),dst=a0,src=a1;
    u32 ph=p?z63g_phase(p,G10):0;
    u64 result=stock_og_memcpy(OG_PASS);
    if(!ph||dst!=OG_SENS_COPY2||a2!=0x78u||!og_fix_enter(p))return result;
    u32 s,k=og_hold_kind(p,&s);
    if(k==6u&&src==OG_REC6+0x19cu) {
        if(og_ld16(OG_SENS_COPY1+6)==6064u&&og_row_mixed(OG_SENS_COPY1+2)&&og_row_mixed(OG_SENS_COPY2+2)&&
           og_row_mixed(OG_SENS_COPY2+0x58)&&og_block_mixed(OG_SENS_COPY2+0x66)) {
            *(volatile u16 *)(uintptr_t)(OG_SENS_COPY1+2)=4040;
            *(volatile u16 *)(uintptr_t)(OG_SENS_COPY2+2)=4040;
            *(volatile u16 *)(uintptr_t)(OG_SENS_COPY2+0x58)=4040;
            og_put_block(OG_SENS_COPY2+0x66,OG_BLOCK_OPEN);
        }
    } else if(k==4u&&src==OG_REC4+0x19cu&&og_block_mixed(OG_SENS_COPY2+0x66))og_put_block(OG_SENS_COPY2+0x66,OG_BLOCK_OPEN);
    og_fix_leave(p);
    return result;
}

/* 2.3 leaf 0x4105EB40 (old site 0x4105EB84, 4K reduced rows 2288 -> 2692): path [x0] byte 0,
 * +0x14 == 1, +0x24 in 3..4, +4 == 3, +0x20 == 4 writes (4040, 2288) at [x1]. Callers: 5 BL
 * (the B at 0x4105EC14 inside 0x4105EBC0 is taken only when +0x14 != 1 or +0x24 is outside 3..4,
 * where this path cannot run). */
static u32 og_leaf_base(u64 in){return !in8(in)&&in32(in,0x14)==1u&&in32(in,0x24)-3u<=1u&&in32(in,4)==3u;}
u64 g10_eb40(OG_ARGS) {
    u64 p=z63g_page(),in=a0,out=a1;
    u32 ph=p?z63g_phase(p,G10):0;
    u64 result=stock_og_leaf_eb40(OG_PASS);
    u32 k=ph?og_post_kind(p):0;
    if(k!=4u||!og_apart(in,out)||!og_leaf_base(in)||in32(in,0x20)!=4u)return result;
    if(og_ld16(out)==4040u&&og_ld16(out+2)==2288u)*(volatile u16 *)(uintptr_t)(out+2)=2692;
    return result;
}
/* Leaf 0x4105EBC0 (old sites 0x4105EC0C 4K coded 2276 -> 2692, 0x4105EC28 6K coded 3410 ->
 * 4040): same base path; +0x20 == 4 writes (4040, 2276), else (6060, 3410). Callers: 2 BL. */
u64 g10_ebc0(OG_ARGS) {
    u64 p=z63g_page(),in=a0,out=a1;
    u32 ph=p?z63g_phase(p,G10):0;
    u64 result=stock_og_leaf_ebc0(OG_PASS);
    u32 k=ph?og_post_kind(p):0;
    if((k!=4u&&k!=6u)||!og_apart(in,out)||!og_leaf_base(in))return result;
    u32 four=in32(in,0x20)==4u;
    if(k==4u&&four&&og_ld16(out)==4040u&&og_ld16(out+2)==2276u)*(volatile u16 *)(uintptr_t)(out+2)=2692;
    else if(k==6u&&!four&&og_ld16(out)==6060u&&og_ld16(out+2)==3410u)*(volatile u16 *)(uintptr_t)(out+2)=4040;
    return result;
}
/* Leaf 0x4105EC38 (old site 0x4105ECEC, 4K active 2268 -> 2688): [x0] byte 0, +0x14 == 1,
 * +0x24 in 3..4, +4 == 3, +0x20 == 4 writes (4032, 2268), then [x1] = 3466 when +0x1C == 1.
 * Callers: 3 BL. */
u64 g10_ec38(OG_ARGS) {
    u64 p=z63g_page(),in=a0,out=a1;
    u32 ph=p?z63g_phase(p,G10):0;
    u64 result=stock_og_leaf_ec38(OG_PASS);
    u32 k=ph?og_post_kind(p):0;
    if(k!=4u||!og_apart(in,out)||!og_leaf_base(in)||in32(in,0x20)!=4u)return result;
    if(og_ld16(out+2)==2268u&&og_ld16(out)==(in32(in,0x1c)==1u?3466u:4032u))*(volatile u16 *)(uintptr_t)(out+2)=2688;
    return result;
}
/* Function 0x4105EE00 (old site 0x4105EF20, size class 7 active rows 3402 -> 4032): [x0] byte 0,
 * +0x14 == 1, +0x24 outside 3..4, +0xC == 4 and the selector 7, where the selector is
 * 0x4105E878(+0x2C, +0x20) when +0x2C != 0 (2 -> 7, 1 -> 2, else the value), else 1 when byte
 * +0x28 != 0, else +0x20; writes (6048, 3402). Callers: 9 BL, 3 B (tail; same adapter). The
 * stock word at 0x4105EF20 is "mov w9,#0xd4a", not a branch, so the design's B-tail form is not
 * used (only B/BL words are replaced at these leaves). */
static u32 og_ee00_path(u64 in) {
    if(in8(in)||in32(in,0x14)!=1u||in32(in,0x24)-3u<=1u||in32(in,0xc)!=4u)return 0;
    u32 a=in32(in,0x2c),b=in32(in,0x20),sel;
    if(a)sel=b==2u?7u:b==1u?2u:b;
    else sel=*(volatile const u8 *)(uintptr_t)(in+0x28)?1u:b;
    return sel==7u;
}
u64 g10_ee00(OG_ARGS) {
    u64 p=z63g_page(),in=a0,out=a1;
    u32 ph=p?z63g_phase(p,G10):0;
    u64 result=stock_og_leaf_ee00(OG_PASS);
    u32 k=ph?og_post_kind(p):0;
    if(k!=6u||!og_apart(in,out)||!og_ee00_path(in))return result;
    if(og_ld16(out)==6048u&&og_ld16(out+2)==3402u)*(volatile u16 *)(uintptr_t)(out+2)=4032;
    return result;
}
/* 2.4 classifier 0x41175E5C (old site 0x41175F48 "mov w0,#0xa" -> 8 for either size): the jump
 * table 0xA02BC8FF4 sends type [x0] == 5 to 0x41175EF0, which returns 10 exactly when
 * ([x0+4] - 4) >= 3 unsigned (the only branch to 0x41175F48). 17 BL call sites; the result 10 of
 * that path becomes 8 when open gate is on. The stock word is "mov w0,#0xa" (not a branch), so
 * the design's preferred B-tail form is not used; this is its Option A. */
u64 g10_cls(OG_ARGS) {
    u64 p=z63g_page(),in=a0;
    u32 ph=p?z63g_phase(p,G10):0;
    u64 result=stock_og_classify(OG_PASS);
    u32 k=ph?og_post_kind(p):0;
    if((k!=4u&&k!=6u)||!in||(u32)result!=10u)return result;
    if(in32(in,0)!=5u||in32(in,4)-4u<3u)return result;
    return 8u;                      /* as "mov w0,#8": upper half zero */
}
