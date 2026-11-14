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
/* G6, N-RAW quality tiers on the arm-time gate.
 * Declared by z63patch/gate_groups/nraw.py; one translation unit linked alone at the group's
 * fixed range, so its bytes do not depend on the composition. Reuses the reviewed budget method
 * (native/nraw_quality.c: z63_nraw_budget) and the shared v2/v3 settings reader
 * (native/feature_settings.c with Z63_SETTINGS_SHARED_V3, native/nv_quality.c).
 *
 * Nothing is written at arming or install. The tier t is bits 18-19 of the
 * published settings word (0 High, 1 Fine, 2 Normal, 3 Basic; native/nraw_quality.h). It applies
 * only when (t < 2) equals Nikon's class in the request at the site; otherwise stock. Only
 * t = 1 (Fine) with Nikon High and t = 3 (Basic) with Nikon Normal change anything.
 *
 * Every entry: P acquire-loaded first (the site adapter for the hooks, z63g_page() for the two
 * menu records); PASS (stock call only, nothing stored outside the frame) unless G6 is LIVE or
 * DRAINING, selected and not broken; the published word is read once inside ACT[6 & 3]. In
 * DRAINING the word is frozen. */
#include <stdint.h>
#define Z63_SETTINGS_SHARED_V3
#include "../gate_api.h"
#include "../setting_slots.h"
#include "../sites/nraw_copy.h"
#include "../sites/nraw_budget.h"
#include "../../nraw_quality.c"
#include "../settings_record.c"
#include "../../nv_quality.c"
typedef uint32_t u32;typedef uint64_t u64;

#define G6 6u
#define NRAW_TABLE 0x42b723b0ull           /* 46 rows of 24 B: 23 High, then the 23 Normal pairs */
#define NRAW_PAIRS 23u
#define NRAW_TYPE 2u                       /* rate-copy request word 0: N-RAW */
#define NRAW_KEY 0x05000f55u               /* Nikon N-RAW quality: 1 High, 0 Normal */
#define BUDGET_HIGH 2u                     /* budget request +0xB8 factor index (indices {2,1,7,0}) */
#define BUDGET_FINE 1u
#define BUDGET_NORMAL 7u
#define BUDGET_BASIC 0u
#define GLOBAL_ROW 0xa03325c10ull          /* the rate row task 0x40D2518C copies at movie-subsystem start */
#define DESCRIPTOR 0xa03d2b2e0ull          /* the TENC descriptor setup 0x40E6C048 fills (+0xB8 at 0x40E6C078) */
#define HOLD (Z63_STATE_G6+0u)             /* Anchor value word (gate_api.h) */

u32 stock_setting_get(u32 key);
void stock_text_store(const char *text,u32 slot,u32 style);

/* The stored tier from the published word e (1 << 32 | word), or 0 (High: never changes a site)
 * for an empty, legacy or foreign record. A v2 record keeps a nonzero tier; its zero is 0. */
static u32 g6_tier(u64 e) {
    struct z63_feature_settings v;
    int kind=z63_settings_decode((u32)e,&v);
    return (kind==Z63_RECORD_V2||kind==Z63_RECORD_V3)?v.nraw:0u;
}

/* The tier with ACT[6 & 3] held (the caller leaves it), or Z63_TIER_NONE with nothing held. */
static u32 g6_enter(u64 p) {
    if(!z63g_act_enter(p,G6))return Z63_TIER_NONE;
    u64 e=z63g_effective(p);
    if(e)return g6_tier(e);
    z63g_act_leave(p,G6);
    return Z63_TIER_NONE;
}

static u32 rd32(const void *b,u32 at) {
    const volatile uint8_t *q=(const volatile uint8_t *)b+at;
    return (u32)q[0]|(u32)q[1]<<8|(u32)q[2]<<16|(u32)q[3]<<24;
}
static int same_row(const struct z63_nraw_row *a,const struct z63_nraw_row *b) {
    return a->dimensions==b->dimensions&&a->numerator==b->numerator&&a->denominator==b->denominator&&
        a->flags==b->flags&&a->bits_per_second==b->bits_per_second;
}

/* Site nraw_copy, role copy (first). The stock copy 0x40D22220 takes x0 = the request
 * (u32 type, size, rate, quality: 0 High, 1 Normal) and x1 = the 24-byte output row
 * (sites/nraw_copy.h: request, row). The row is replaced only when the stock copy produced exactly the
 * stock row of the class the request names, for the pair index of the old layout
 * (native/nraw_target.c), and the budget method accepts the pair. */
u32 z63h_nraw_copy_g6(u64 page,u32 phase,void *request,void *out,u32 applied) {
    const u32 *req=(const u32 *)request;
    struct z63_nraw_row *row=(struct z63_nraw_row *)out;
    if(!phase||applied!=Z63_TIER_NONE||!req||!row)return applied;
    u32 type=req[0],size=req[1],rate=req[2],quality=req[3],index;
    if(type!=NRAW_TYPE||quality>1)return applied;
    if(size==0&&rate>=2&&rate<7)index=rate-2;
    else if(size==1&&rate>=2&&rate<7)index=5+rate;
    else if(size==2&&rate<7)index=12+rate;
    else return applied;
    const struct z63_nraw_row *table=(const struct z63_nraw_row *)(uintptr_t)NRAW_TABLE;
    if(!same_row(row,table+index+(quality?NRAW_PAIRS:0u)))return applied;
    u32 t=g6_enter(page);
    if(t==Z63_TIER_NONE)return applied;
    /* The copy into the global row (movie-subsystem start) is an anchor read. */
    if(out==(void *)(uintptr_t)GLOBAL_ROW)t=z63g_hold_anchor(page,HOLD,t);
    struct z63_nraw_row next;
    if(((t==1u&&quality==0)||(t==3u&&quality==1))&&     /* else inconsistent, or the stock row */
       z63_nraw_budget(table+index,table+index+NRAW_PAIRS,t,&next)) {
        row->dimensions=next.dimensions;row->numerator=next.numerator;row->denominator=next.denominator;
        row->flags=next.flags;row->bits_per_second=next.bits_per_second;
        applied=t;
    }
    z63g_act_leave(page,G6);
    return applied;
}

/* Site nraw_budget, role decide (after G10). The old-layout predicate (native/nraw_target.c
 * z63_nraw_target_budget): type 10, factor index 2 (High) or 7 (Normal), N-RAW format 3, 5 or 6
 * and stock N-RAW coded dimensions, from the primary or (native N-RAW) the secondary buffer. Publication:
 * also index 1 (Fine) or 0 (Basic), which only this hook writes, taken back to 2 or 7 when the
 * tier no longer names them. */
u32 z63h_nraw_budget_g6(u64 page,u32 phase,void *request,u32 decided) {
    if(!phase||decided!=Z63_TIER_NONE||!request)return decided;
    u32 primary=rd32(request,0x30)||rd32(request,0x34);
    u32 present=primary||rd32(request,0x80)||rd32(request,0x84);
    u32 dimensions=rd32(request,primary?0x1a:0x78),index=rd32(request,0xb8);
    u32 width=dimensions&65535u,height=dimensions>>16,format=rd32(request,primary?0x10:0x74);
    if(!present||rd32(request,0)!=10||index>BUDGET_NORMAL||!((1u<<index)&0x87u)||    /* 0, 1, 2 or 7 */
       (format!=3&&format!=5&&format!=6)||
       !((width==6060&&height==3410)||(width==4040&&height==2276)||(width==3996&&height==2248)))
        return decided;
    u32 t=g6_enter(page);
    if(t==Z63_TIER_NONE)return decided;
    t=z63g_hold_anchor(page,HOLD,t);       /* Encoder setup is an anchor read */
    volatile u32 *at=(volatile u32 *)((uint8_t *)request+0xb8);
    if(t==1u&&index==BUDGET_HIGH) {*at=BUDGET_FINE;decided=t;}
    else if(t==3u&&index==BUDGET_NORMAL) {*at=BUDGET_BASIC;decided=t;}
    /* Fine (1) and Basic (0) are only ever this hook's (stock writes 2 or 7); one left by an
     * earlier tier (a request built at movie entry, before a publication whose callback has not
     * run yet) goes back to the stock index here, at the read. */
    else if(index==BUDGET_FINE) {if(t==1u)decided=t;else *at=BUDGET_HIGH;}
    else if(index==BUDGET_BASIC) {if(t==3u)decided=t;else *at=BUDGET_NORMAL;}
    z63g_act_leave(page,G6);
    return decided;
}

/* Record 0x40596308 (bl SETTING_GET, key 0x05000F55 in the stock refresh 0x40596140): the
 * stock value v (1 High, 0 Normal); with a tier consistent with v, High 1, Fine 2, Normal 0,
 * Basic 3 (the old-layout value map, experimental/nraw/nraw_native_menu.inc). The stock refresh
 * shows HIGH for 1, NORM for 0 and an empty label for any other value. */
u32 z63_g6_menu_value(u32 key) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G6):0;
    u32 v=stock_setting_get(key);
    if(!ph||key!=NRAW_KEY||v>1)return v;
    u32 t=g6_enter(p);
    if(t==Z63_TIER_NONE)return v;
    if(t==1u&&v==1)v=2;
    else if(t==3u&&v==0)v=3;
    z63g_act_leave(p,G6);
    return v;
}

/* Record 0x40596350 (bl TEXT_STORE(text, slot 0x4C, style 4), the N-RAW quality label): the
 * stock refresh passes an empty label exactly when the value was neither 0 nor 1, which only the
 * value record makes it see (Fine 2, Basic 3). For an empty label, a consistent Fine or Basic
 * tier (checked against Nikon's quality slot, Z63_SLOT_KEY_0F55) gives FINE or BASIC. Anything
 * else is the stock call with the caller's arguments; the slot is passed through, never set. */
void z63_g6_menu_text(const char *text,u32 slot,u32 style) {
    static const char fine[]="FINE";
    static const char basic[]="BASIC";
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G6):0;
    if(ph&&text&&!*(const volatile char *)text) {
        u32 v=z63g_ld32(Z63_SLOT_KEY_0F55);
        u32 t=g6_enter(p);
        if(t!=Z63_TIER_NONE) {
            if(t==1u&&v==1)text=fine;
            else if(t==3u&&v==0)text=basic;
            z63g_act_leave(p,G6);
        }
    }
    stock_text_store(text,slot,style);
}

/* ---- restore callback (Group.restore, G6-REQUESTS R-G6-2) ----
 * The global N-RAW rate row 0xA03325C10 is written only by the stock task 0x40D2518C through the
 * site nraw_copy (0x40D253CC), so a Fine or Basic row the hook left there can outlive the session
 * until the next movie-subsystem start. The core calls z63r_g6 once per restore pass on the
 * card-detect task, under Q and only for a session that selected G6 (or, page lost, for every
 * build with G6). For each of the 23 table pairs i (0x42B723B0, High i, Normal i + 23; the
 * budget method rejects the pairs it does not support): if the live row has High i's dimensions,
 * frame rate and flags and the bits/s of pair i's Fine (or Basic) row as z63_nraw_budget makes it,
 * the u64 bits/s at +16 is put back to High i's (or Normal i's) by one bounded CAS. That is the
 * only store, and only of that word. The row is left alone when it holds a stock value, a row of
 * another codec or anything that is not a known patch value. Returns 0 nothing left to do, 1 a
 * CAS lost to a concurrent write (try again next pass), 2 the row has a pair's dimensions, rate
 * and flags but a bits/s that is none of that pair's High, Normal, Fine or Basic (left alone,
 * reported: the session ends PARTIAL), or a patch value that one CAS cannot make stock (the
 * restoring row's first 16 bytes differ from the live row's; not the case in C2.01). */
enum {FIX_NONE=0u,FIX_KNOWN=1u,FIX_PAIR=2u,FIX_PENDING=4u,FIX_LEFT=8u};
static u32 g6_cas64(u64 a,u64 old,u64 next) {
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
static int same_prefix(const struct z63_nraw_row *a,u32 d,u32 n,u32 q,u32 f) {
    return a->dimensions==d&&a->numerator==n&&a->denominator==q&&a->flags==f;
}
__attribute__((noinline)) static u32 g6_fix_tier(const struct z63_nraw_row *high,const struct z63_nraw_row *normal,u32 tier,
                                                  const struct z63_nraw_row *live) {
    struct z63_nraw_row x;
    if(!z63_nraw_budget(high,normal,tier,&x)||x.bits_per_second!=live->bits_per_second||
       !same_prefix(&x,live->dimensions,live->numerator,live->denominator,live->flags))return FIX_NONE;
    const struct z63_nraw_row *to=tier==1u?high:normal;
    if(!same_prefix(to,live->dimensions,live->numerator,live->denominator,live->flags))return FIX_KNOWN|FIX_LEFT;
    return g6_cas64(GLOBAL_ROW+16u,x.bits_per_second,to->bits_per_second)?FIX_KNOWN:FIX_KNOWN|FIX_PENDING;
}
__attribute__((noinline)) static u32 g6_fix_pair(u32 i,const struct z63_nraw_row *live) {
    const struct z63_nraw_row *table=(const struct z63_nraw_row *)(uintptr_t)NRAW_TABLE;
    const struct z63_nraw_row *high=table+i,*normal=table+i+NRAW_PAIRS;
    if(!same_prefix(high,live->dimensions,live->numerator,live->denominator,live->flags))return FIX_NONE;
    if(live->bits_per_second==high->bits_per_second||live->bits_per_second==normal->bits_per_second)return FIX_PAIR|FIX_KNOWN;
    return FIX_PAIR|g6_fix_tier(high,normal,1u,live)|g6_fix_tier(high,normal,3u,live);
}
/* The TENC descriptor's budget index (+0xB8) for tier t, under the budget hook's predicate (the
 * descriptor is an N-RAW class-10 request with a supported raster). Stock writes 2 (High) or 7
 * (Normal) there; 1 (Fine) and 0 (Basic) are only ever the hook's. The index of the class the
 * descriptor holds becomes the one the budget hook would give for t (High 2 / Fine 1 with t = 1;
 * Normal 7 / Basic 0 with t = 3); stock for t = 0. One bounded CAS; 1 when it lost (pending). */
__attribute__((noinline)) static u32 g6_fix_descriptor(u32 t) {
    const void *request=(const void *)(uintptr_t)DESCRIPTOR;
    u32 primary=rd32(request,0x30)||rd32(request,0x34);
    u32 present=primary||rd32(request,0x80)||rd32(request,0x84);
    u32 dimensions=rd32(request,primary?0x1a:0x78),index=rd32(request,0xb8);
    u32 width=dimensions&65535u,height=dimensions>>16,format=rd32(request,primary?0x10:0x74);
    if(!present||rd32(request,0)!=10||(format!=3&&format!=5&&format!=6)||
       !((width==6060&&height==3410)||(width==4040&&height==2276)||(width==3996&&height==2248)))return 0;
    u32 want;
    if(index==BUDGET_HIGH||index==BUDGET_FINE)want=t==1u?BUDGET_FINE:BUDGET_HIGH;
    else if(index==BUDGET_NORMAL||index==BUDGET_BASIC)want=t==3u?BUDGET_BASIC:BUDGET_NORMAL;
    else return 0;
    if(want==index)return 0;
    return z63g_cas32(DESCRIPTOR+0xb8u,index,want)?0u:1u;
}
/* The global row for tier t: a supported pair's High or Fine row becomes Fine when t = 1, else
 * High; its Normal or Basic row becomes Basic when t = 3, else Normal (the rows the copy hook
 * gives for t). Only the u64 bits/s at +16, by one bounded CAS. Anything else is left. */
/* The bits/s of pair (high, normal)'s row for tier 1 or 3 as z63_nraw_budget makes it, when that
 * row has the live row's dimensions, rate and flags; else 0. */
__attribute__((noinline)) static u64 g6_tier_bits(const struct z63_nraw_row *high,const struct z63_nraw_row *normal,u32 tier,
                                                  const struct z63_nraw_row *live) {
    struct z63_nraw_row x;
    if(!z63_nraw_budget(high,normal,tier,&x)||!same_prefix(&x,live->dimensions,live->numerator,live->denominator,live->flags))return 0;
    return x.bits_per_second;
}
__attribute__((noinline)) static u32 g6_align_pair(u32 i,const struct z63_nraw_row *live,u32 t) {
    const struct z63_nraw_row *table=(const struct z63_nraw_row *)(uintptr_t)NRAW_TABLE;
    const struct z63_nraw_row *high=table+i,*normal=table+i+NRAW_PAIRS;
    if(!same_prefix(high,live->dimensions,live->numerator,live->denominator,live->flags))return 0;
    u64 fine=g6_tier_bits(high,normal,1u,live),basic=g6_tier_bits(high,normal,3u,live);
    u64 now=live->bits_per_second,want;
    if(now==high->bits_per_second||(fine&&now==fine))want=t==1u&&fine?fine:high->bits_per_second;
    else if(now==normal->bits_per_second||(basic&&now==basic))want=t==3u&&basic?basic:normal->bits_per_second;
    else return 0;
    if(want==now)return 0;
    return g6_cas64(GLOBAL_ROW+16u,now,want)?0u:1u;
}
__attribute__((noinline)) static u32 g6_align(u32 t) {
    const volatile struct z63_nraw_row *g=(const volatile struct z63_nraw_row *)(uintptr_t)GLOBAL_ROW;
    struct z63_nraw_row live;
    live.dimensions=g->dimensions;live.numerator=g->numerator;live.denominator=g->denominator;
    live.flags=g->flags;live.bits_per_second=g->bits_per_second;
    u32 pending=0;
    #pragma clang loop unroll(full)
    for(u32 i=0;i<NRAW_PAIRS;i++)pending|=g6_align_pair(i,&live,t);
    return pending|g6_fix_descriptor(t);
}
/* ---- publication callback (Group.publish) ----
 * Called by the armed poll after a publication changed the word, only while nothing is recording,
 * no TX or ACT count is held and the anchor hold is free. The global row (copied at movie-subsystem
 * start, possibly long before the next recording) and the TENC descriptor's index are put in line
 * with the new tier, exactly as the hooks would have made them had it been in force. 0 done, 1 a
 * CAS lost (pending). */
u32 z63p_g6(u64 page,u32 word) {
    (void)page;
    return g6_align(g6_tier(1ull<<32|word));
}
u32 z63r_g6(u64 page) {
    (void)page;
    if(g6_fix_descriptor(0u))return 1u;      /* A Fine/Basic index back to High/Normal */
    const volatile struct z63_nraw_row *g=(const volatile struct z63_nraw_row *)(uintptr_t)GLOBAL_ROW;
    struct z63_nraw_row live;
    live.dimensions=g->dimensions;live.numerator=g->numerator;live.denominator=g->denominator;
    live.flags=g->flags;live.bits_per_second=g->bits_per_second;
    u32 seen=0;
    #pragma clang loop unroll(full)
    for(u32 i=0;i<NRAW_PAIRS;i++)seen|=g6_fix_pair(i,&live);
    if(seen&FIX_PENDING)return 1u;
    if(seen&FIX_LEFT)return 2u;
    return (seen&FIX_PAIR)&&!(seen&FIX_KNOWN)?2u:0u;
}
