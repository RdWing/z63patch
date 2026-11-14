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
/* G10, open gate on the arm-time gate. Declared by
 * z63patch/gate_groups/open_gate.py; one translation unit linked alone at 0x401DC000..0x401EC000,
 * so its bytes do not depend on the composition. The interface for every part is open_gate.h.
 *
 * This file (part 10a): the open-gate decision (old layout native/open_gate.c og_size, now from
 * the published word inside an ACT hold), the og_sync tables (old og_sync, CAS between the two
 * known values only), the budget decision and cap at the shared site nraw_budget (G10 first),
 * the open-gate rate row at the shared site nraw_copy (after G6, with the tier G6 applied), and
 * the restore callback z63r_g10. The geometry records are in open_gate_geom.c; later parts are
 * included at the end when their files exist.
 *
 * Rules: an integrity break -> pass through at once (z63g_phase is read on every
 * call; nothing is cached across calls except the plan latch in state, which every reader ANDs
 * with the phase); open gate is chosen on the stock frame-size page (part ENT); the budget
 * cap may act in pass-through (og_budget_cap, made reachable in PASS only by the core change
 * G10-REQUESTS R-G10-2; until then it acts in LIVE/DRAINING); for the re-plan see open_gate.py. */
#include <stdint.h>
#define Z63_SETTINGS_SHARED_V3
#include "../gate_api.h"
#include "../setting_slots.h"
#include "../sites/nraw_copy.h"
#include "../sites/nraw_budget.h"
#include "../../nraw_quality.c"
#include "../settings_record.c"
#include "../../nv_quality.c"
#include "open_gate.h"
typedef uint32_t u32;typedef uint64_t u64;typedef uint16_t u16;typedef uint8_t u8;

#define G6 6u
#define NRAW_TABLE 0x42b723b0ull           /* 46 rows of 24 B: 23 High, then the 23 Normal pairs */
#define NRAW_PAIRS 23u
#define GLOBAL_ROW 0xa03325c10ull          /* the global N-RAW rate row (written through site nraw_copy) */

static inline volatile u8 *og_st(u64 p){return z63g_state(p,Z63_STATE_G10);}
static inline void og_put8(u64 p,u32 at,u32 v){og_st(p)[at]=(u8)v;}
static inline void og_put16(u64 p,u32 at,u32 v){*(volatile u16 *)(og_st(p)+at)=(u16)v;}
static inline void og_put32(u64 p,u32 at,u32 v){*(volatile u32 *)(og_st(p)+at)=v;}
static inline u32 og_ld16(u64 a){return *(volatile const u16 *)(uintptr_t)a;}
static inline __attribute__((always_inline)) u32 rd32(const void *b,u32 at) {
    const volatile u8 *q=(const volatile u8 *)b+at;
    return (u32)q[0]|(u32)q[1]<<8|(u32)q[2]<<16|(u32)q[3]<<24;
}

/* Bounded CAS of a u16 (64 attempts), the verifier's loop shape: 1 when stored. */
static u32 og_cas16(u64 a,u32 old,u32 next) {
    u32 v,s,t,r;
    __asm__ volatile(
        "mov %w[t],#64\n"
        "1: ldaxrh %w[v],[%[a]]\n"
        "cmp %w[v],%w[o]\n"
        "b.ne 3f\n"
        "stlxrh %w[s],%w[n],[%[a]]\n"
        "cbz %w[s],2f\n"
        "subs %w[t],%w[t],#1\n"
        "b.ne 1b\n"
        "3: clrex\n"
        "mov %w[r],#0\n"
        "b 4f\n"
        "2: mov %w[r],#1\n"
        "4:\n"
        :[v]"=&r"(v),[s]"=&r"(s),[t]"=&r"(t),[r]"=&r"(r):[a]"r"(a),[o]"r"(old&0xffffu),[n]"r"(next&0xffffu):"memory","cc");
    return r;
}
static u32 og_cas64(u64 a,u64 old,u64 next) {
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

/* ---- the decision ---- */
/* The choice in a record. The gate units' record reader (
 * native/arm_time/settings_record.c, settings_bind.h) reports open gate only when bit 20 is bound, inside the record's
 * check, to the CURRENT N-RAW size and rate; any later change of either, by any route, reads as
 * open gate off, and an older plain bit 20 (before the binding) reads off (conservative).
 *
 * One more rule while armed (og_chosen): one exception is accepted to "a regular size
 * means off": a RETURN to exactly the same size and rate while the patch is not
 * armed. While G10 is LIVE, a binding term that was seen on (OG_SEEN) and is then seen with another
 * size or rate is marked stale (OG_STALE) and stays off even if the same pair comes back (remote
 * command, U1-U3 recall, reset, ... then back). The marks are RAM (this state page): a power cycle
 * or a standby wake forgets them, which is the accepted residual. They are cleared only by an
 * open-gate pick of G10 itself (ENT or i-menu, saved or already the same: og_pick_marks), and are
 * never set from a term not yet seen on, so the window between a pick's save (bound to the new
 * pair) and stock's own slot write cannot mark the new choice stale. Every other group's save
 * re-encodes a stale record (decoded off) without bit 20, which makes the off durable. */
static inline __attribute__((always_inline)) u32 og_term_of(u32 word) {
    return ((word>>24)^settings_crc((word>>8)&65535u))&255u;
}
__attribute__((noinline)) static u32 og_chosen(u64 p,u32 word) {
    u32 r=og_rule_chosen(0,word),d=r>>8;                /* the shared rule (open_gate.h), stale test below */
    if(!r)return 0;
    u32 live=p&&z63g_phase(p,G10)==Z63_LIVE;
    if(r&2u) {
        if(live&&og_st(p)[OG_SEEN]==d&&og_st(p)[OG_STALE]!=d)og_put8(p,OG_STALE,d);
        return 0;
    }
    if(p&&og_rule_stale(og_st(p),d))return 0;
    if(live&&og_st(p)[OG_SEEN]!=d)og_put8(p,OG_SEEN,d);
    return 1;
}
/* A G10 open-gate pick (saved, or the record already said so): the stale marks are dropped, so the
 * pick takes effect once stock writes its size and rate. */
static inline __attribute__((always_inline)) void og_pick_marks(u64 p) {
    if(og_st(p)[OG_STALE])og_put8(p,OG_STALE,0);
    if(og_st(p)[OG_SEEN])og_put8(p,OG_SEEN,0);
}
/* Old-layout predicate (native/open_gate.c og_size) on a record: open gate chosen (og_chosen),
 * file type N-RAW (6), native N-RAW size 6 or 4 and rate 6..8 (30/25/24p); og_size_word adds
 * movie view (0xA048BB160 == 1). The wanted-kind rule without the movie-view term is shared by
 * og_size_word (movie view) and the photo-view pre-set z63p_g10 (settled photo view). */
static u32 og_want_rule(u64 p,u32 word) {
    return og_chosen(p,word)?og_rule_slots():0u;
}
u32 og_size_word(u64 p,u32 published) {
    if(z63g_ld32(OG_MOVIE_WORD)!=1u)return 0;
    return og_want_rule(p,published);
}
static inline __attribute__((always_inline)) u32 og_size_i(u64 p) {
    if(!p||!z63g_act_enter(p,G10))return 0;
    u64 e=z63g_effective(p);
    u32 k=e?og_size_word(p,(u32)e):0u;
    z63g_act_leave(p,G10);
    return k;
}
__attribute__((noinline)) u32 og_size(u64 p){return og_size_i(p);}
u32 og_on(u64 p){return og_size(p)!=0;}
u32 og_kind_latched(u64 p){return p&&z63g_phase(p,G10)?og_st(p)[OG_KIND]:0u;}
/* THE reader rule (re-plan build): the plan latch, while G10 is
 * LIVE/DRAINING and either
 *  - the movie-recording readiness word 0xA048BB160 reads 1 (the rule earlier: movie view
 *    with a ready card; it is 0 in photo view, so a photo plan never sees an open-gate value), or
 *  - the plan mode OG_MPLAN is set and movie live view is running (og_running_movie: pipeline state
 *    2, a movie-path class). The plan mode is written only where stock re-plans (open_gate.h): set
 *    when a movie plan took the latch, cleared at a photo plan. This keeps the readers on the plan
 *    in force when the readiness word drops in running movie live view with no re-plan (a card
 *    pull, a full or failed card), including the DRAINING phase a card pull starts (the core
 *    restores only under photo Q). Earlier the readers went stock there against a live
 *    open-gate plan. Once the pipeline stops (a menu, playback, the lever to photo) the first term
 *    alone decides again, as before: with the word 0 the readers are stock for the plan stock makes
 *    next; the photo-plan branch of the post-fix (no hold, no open-gate path) or g10_replan's
 *    lowering form (readiness not 1 with the plan mode set) then lowers. The plan post-fix uses this
 *    same hold and never lowers while it holds.
 * Pure (loads only). */
static inline __attribute__((always_inline)) u32 og_plan_kind_i(u64 p) {
    if(!p||!z63g_phase(p,G10))return 0;
    if(z63g_ld32(OG_MOVIE_WORD)!=1u&&!(og_st(p)[OG_MPLAN]&&og_running_movie()))return 0;
    u32 k=og_st(p)[OG_KIND];
    return k==6u||k==4u?k:0u;
}
__attribute__((noinline)) u32 og_plan_kind(u64 p){return og_plan_kind_i(p);}
/* The wanted kind: the old-layout predicate (og_size_word) on the STORED word. og_size_word decodes
 * with z63_settings_decode, the shared settings reader's validity check (v2/v3 marker, CRC, field
 * ranges): an empty, legacy, foreign or corrupt stored record gives 0, never open gate. */
__attribute__((noinline)) u32 og_stored_kind(u64 p){return og_size_word(p,z63g_acq32(OG_STORED_WORD));}

/* The latch (state +1, +4, +6, +0) to kind k and the plan mode (+3) to m. Only g10_replan (movie
 * view, m 1), z63p_g10 (settled photo view, m 0), the plan post-fix (m 1 when it applies the latch,
 * 0 at a photo plan or when lowering) and the restore and break callbacks (0, 0) write them. A
 * plan mode that goes off goes first and one that goes on goes last, so a reader never sees the
 * mode on with a half-written latch; kind first, active after it (G7 reads +0 and +3). */
static void og_mode(u64 p,u32 m){if(og_st(p)[OG_MPLAN]!=m)og_put8(p,OG_MPLAN,m);}
static void og_latch(u64 p,u32 k,u32 m) {
    if(!m)og_mode(p,0);
    og_put8(p,OG_KIND,k);
    og_put16(p,OG_ROWS,k==6u?4040u:k==4u?2692u:0u);
    og_put16(p,OG_WIDTH,k==6u?6060u:k==4u?4040u:0u);
    og_put8(p,OG_ACTIVE,k!=0u);
    if(m)og_mode(p,1);
}

/* ---- og_sync tables (old og_sync; disarm sanitiser in open_gate.py SANITISE) ----
 * Each word is exchanged only between its stock and open-gate value; any other content is left
 * and reported in state +2 (OG_SYNC_FOREIGN): the plan post-fix then keeps the record stock. */
static u32 og_swap(u64 a,u32 stock,u32 open,u32 on) {
    u32 want=on?open:stock,other=on?stock:open,v=og_ld16(a);
    if(v==want)return 0;
    if(v==other&&og_cas16(a,other,want))return 0;
    return og_ld16(a)==want?0u:1u;
}
__attribute__((noinline)) void og_sync(u64 p,u32 k) {
    u32 bad=0;
    bad|=og_swap(0xa02bc7944ull,3432,4040,k!=0);   /* RAW stream source rows, area 3 */
    bad|=og_swap(0xa02bc7970ull,3432,4040,k!=0);   /* readout rows, area 3 */
    bad|=og_swap(0xa0298eebeull,3432,4038,k==4);   /* 4K reduction record: input rows (+2) */
    bad|=og_swap(0xa0298f05aull,3432,4038,k==4);   /* 4K vertical block input rows (+0x19E) */
    bad|=og_swap(0xa0298f0b0ull,2288,2692,k==4);   /* 4K vertical block output rows (+0x1F4) */
    bad|=og_swap(0xa02bc820eull,3432,4040,k!=0);   /* size function rectangle rows (+0x4A) */
    bad|=og_swap(0xa02bc80f2ull,3402,4032,k==6);   /* 6K active rows */
    bad|=og_swap(0xa02bc80faull,14,4,k==6);        /* 6K active top */
    bad|=og_swap(0xa02bc8112ull,2268,2688,k==4);   /* 4K active rows */
    bad|=og_swap(0xa02bc811aull,10,2,k==4);        /* 4K active top */
    u32 s=k|(bad?OG_SYNC_FOREIGN:0u);
    if(og_st(p)[OG_SYNC_STATE]!=s)og_put8(p,OG_SYNC_STATE,s);
}

/* ---- site nraw_budget, role decide (G10 first) ----
 * The request the stock budget 0x40E67D0C reads: primary buffer (u64 +0x30 nonzero) dims at
 * +0x1A (u16 w, u16 h), else secondary (u64 +0x80 nonzero) dims at +0x78; factor index +0xB8.
 * Decided on the RECEIVED dims only: a recording keeps the geometry it was planned
 * with whatever the setting does. */
/* The received dims packed (w | h << 16), 0 when no buffer is present. Returned in a register,
 * never stored through a pointer: og_budget_cap must store nothing outside its frame but +0xB8
 * (the verifier's pass-cap rule, verify_gate._pass_cap). */
static inline __attribute__((always_inline)) u32 og_budget_dims(const void *request) {
    u32 primary=rd32(request,0x30)||rd32(request,0x34);
    u32 present=primary||rd32(request,0x80)||rd32(request,0x84);
    return present?rd32(request,primary?0x1a:0x78):0u;
}
static inline __attribute__((always_inline)) u32 og_budget_kind(u32 d){return d?og_dims_kind(d&0xffffu,d>>16):0u;}
/* Bytes the stock budget gives for factor index `index` at w x h, without the crop margins (an
 * upper bound of the stock value: margins only shrink it), rounded up to 512 as for type 10.
 * The factor is the live table word, i.e. what stock will multiply by. */
static inline __attribute__((always_inline)) u64 og_budget_bytes(u32 index,u32 d) {
    if(index>8u)return 0;
    u64 f=z63g_ld32(OG_FACTORS+4u*index);
    return (f*(d&0xffffu)*(d>>16)/80u+511u)&~(u64)511u;
}
/* Cap (PASS-safe: no gate access, no published word, one store of +0xB8 only when lowering).
 * The core's nraw_budget adapter calls it after the decide hooks in every phase, also with no
 * page (Group.pass_cap, R-G10-2): at open-gate dims, a factor index whose budget would exceed
 * the 7,763,456-byte ring slot becomes 8 (2.5 bit/px, the rule "in open gate, High uses index
 * 8"). Fine (1), Normal (7), Basic (0) and 8 always fit and stay; at stock dims nothing
 * happens. Only a request of type OG_NRAW_TYPE (movie N-RAW, the only kind that goes to the
 * ring) is ever changed. Every other type is left as stock: stills share this site, and the
 * high-speed-capture HE strip (OG_STILL_HE_TYPE, stock 6064x4040) passes og_dims_open; lowering
 * it starves the C15/C30 strips and stalls the burst. Returns the index left. */
u32 og_budget_cap(void *request) {
    if(!request)return Z63_TIER_NONE;
    u32 index=rd32(request,0xb8),d=og_budget_dims(request);
    if(rd32(request,0)!=OG_NRAW_TYPE)return index;
    if(!og_budget_kind(d)||og_budget_bytes(index,d)<=OG_RING_SLOT)return index;
    *(volatile u32 *)((u8 *)request+0xb8)=OG_HIGH_INDEX_6K;
    return OG_HIGH_INDEX_6K;
}
/* The stored N-RAW tier (G6's field, bits 18-19 of a v3 record) while G6 is live, else
 * Z63_TIER_NONE (Nikon's two classes only). Read once inside ACT[G10 & 3]. */
static u32 og_nraw_tier(u64 p) {
    if(!z63g_phase(p,G6)||!z63g_act_enter(p,G10))return Z63_TIER_NONE;
    u64 e=z63g_effective(p);
    u32 t=Z63_TIER_NONE;
    if(e) {
        struct z63_feature_settings v;
        int kind=z63_settings_decode((u32)e,&v);
        if(kind==Z63_RECORD_V2||kind==Z63_RECORD_V3)t=v.nraw;
    }
    z63g_act_leave(p,G10);
    return t;
}
/* Factor index at open-gate dims (old layout z63_og_budget_tier, with's class
 * rule as G6 applies it): type 10, Nikon High (2): Fine (1) for a stored Fine, else 6K 8 / 4K 2;
 * Nikon Normal (7): Basic (0) for a stored Basic, else 7. Then the cap. Returns the tier decided
 * (0..3) so G6 leaves the request alone (G6's own predicate excludes these dims as well). */
u32 z63h_nraw_budget_g10(u64 page,u32 phase,void *request,u32 decided) {
    if(!phase||!request||decided!=Z63_TIER_NONE)return decided;
    u32 kind=og_budget_kind(og_budget_dims(request));
    if(!kind)return decided;
    u32 index=rd32(request,0xb8),next=index,tier=Z63_TIER_NONE;
    if(rd32(request,0)==10u&&(index==2u||index==7u)) {
        u32 t=og_nraw_tier(page);
        if(index==2u) {tier=t==1u?1u:0u;next=tier==1u?1u:(kind==6u?OG_HIGH_INDEX_6K:2u);}
        else {tier=t==3u?3u:2u;next=tier==3u?0u:7u;}
        if(next!=index)*(volatile u32 *)((u8 *)request+0xb8)=next;
    }
    next=og_budget_cap(request);     /* the core adapter runs the cap again after every decide hook */
    og_put32(page,OG_LAST_TIER,tier);
    og_put16(page,OG_LAST_INDEX,next);
    return tier==Z63_TIER_NONE?0x10u|next:tier;
}

/* ---- site nraw_copy, role copy (after G6) ----
 * Old layout z63_og_copy_tier: the 6K or 4K record gets the full height and a rate of
 * bits-per-pixel x the new pixel rate plus the stock record's overhead: 6K 2.5/2.0/1.5/1.0,
 * 4K the stock 3.0/2.0/1.5/1.0 bit/px for High/Fine/Normal/Basic; rounded up to 1 Mbit/s.
 * `tier` is the tier whose rate the row holds now (G6's applied tier, else Nikon's class).
 * Returns 1 and fills *out when the row is an open-gate candidate for `kind`. */
static u32 og_row(u32 kind,const struct z63_nraw_row *in,u32 tier,struct z63_nraw_row *out) {
    if(tier>3u||!in->denominator||!in->bits_per_second)return 0;
    u32 stock_tenths=tier==0u?30u:tier==1u?20u:tier==2u?15u:10u;   /* no rodata table (README 5) */
    u32 open_6k_tenths=tier==0u?25u:stock_tenths;
    u64 width,rows,open_rows;u32 new_tenths;
    if(kind==6u&&in->dimensions==(6060u|3410u<<16)){width=6060;rows=3410;open_rows=4040;new_tenths=open_6k_tenths;}
    else if(kind==4u&&in->dimensions==(4040u|2276u<<16)){width=4040;rows=2276;open_rows=2692;new_tenths=stock_tenths;}
    else return 0;
    u64 old_rate=width*rows*in->numerator/in->denominator;
    u64 new_rate=width*open_rows*in->numerator/in->denominator;
    u64 old_payload=old_rate*stock_tenths/10u;
    if(in->bits_per_second<=old_payload)return 0;
    u64 bits=new_rate*new_tenths/10u+(in->bits_per_second-old_payload);
    out->dimensions=(u32)width|(u32)open_rows<<16;
    out->numerator=in->numerator;out->denominator=in->denominator;out->flags=in->flags;
    out->bits_per_second=(bits+999999u)/1000000u*1000000u;
    return 1;
}
u32 z63h_nraw_copy_g10(u64 page,u32 phase,void *request,void *out,u32 applied) {
    const u32 *req=(const u32 *)request;
    struct z63_nraw_row *row=(struct z63_nraw_row *)out;
    if(!phase||!req||!row||req[3]>1u)return applied;
    u32 k=og_plan_kind(page);          /* the plan latch (re-plan build), not the choice */
    if(!k)return applied;
    u32 tier=applied!=Z63_TIER_NONE?applied:(req[3]?2u:0u);
    struct z63_nraw_row in,next;
    in.dimensions=row->dimensions;in.numerator=row->numerator;in.denominator=row->denominator;
    in.flags=row->flags;in.bits_per_second=row->bits_per_second;
    if(!og_row(k,&in,tier,&next))return applied;
    row->dimensions=next.dimensions;row->bits_per_second=next.bits_per_second;
    return tier;
}

/* ---- restore callback (Group.restore) ----
 * Runs once per restore pass under Q (after the core's sanitisers; G10 declares none).
 * 0. The og_sync tables: each open-gate value back to its stock value by one bounded CAS.
 * 1. The global N-RAW row 0xA03325C10: if it holds an open-gate row this group makes (for a
 *    6K/4K pair i of the stock table and a tier t, og_row of the pair's stock High/Normal row or
 *    of the Fine/Basic row z63_nraw_budget makes), its bits/s (u64 +16) goes back to the stock
 *    row of the class (High i for t < 2, Normal i otherwise; what G6's own restore would end at),
 *    then its dims (u32 +0) back to the stock dims, each by one bounded CAS. A row with open-gate
 *    dims and the stock bits/s of that class (a pass interrupted between the two CAS) gets its
 *    dims back. Open-gate dims with any other rate: left alone, reported (2).
 * 2. Own state: the latch fields cleared (page given only for an own session).
 * Returns 1 if any CAS lost (next pass), else 2 if anything was left, else 0. No stock call. */
enum {FIX_NONE=0u,FIX_DONE=1u,FIX_PENDING=2u,FIX_MATCH=4u};
static int og_same_rate(const struct z63_nraw_row *a,const struct z63_nraw_row *b) {
    return a->numerator==b->numerator&&a->denominator==b->denominator&&a->flags==b->flags;
}
__attribute__((noinline)) static u32 og_fix_tier(u32 kind,const struct z63_nraw_row *high,const struct z63_nraw_row *normal,
                                                 u32 t,const struct z63_nraw_row *live) {
    struct z63_nraw_row src,og;
    const struct z63_nraw_row *to=t<2u?high:normal;
    if(t==0u||t==2u)src=*to;
    else if(!z63_nraw_budget(high,normal,t,&src))return FIX_NONE;
    if(!og_row(kind,&src,t,&og)||og.dimensions!=live->dimensions||!og_same_rate(&og,live))return FIX_NONE;
    if(live->bits_per_second==og.bits_per_second) {
        if(!og_cas64(GLOBAL_ROW+16u,og.bits_per_second,to->bits_per_second))return FIX_MATCH|FIX_PENDING;
    } else if(live->bits_per_second!=to->bits_per_second)return FIX_NONE;
    return z63g_cas32(GLOBAL_ROW,live->dimensions,to->dimensions)?FIX_MATCH|FIX_DONE:FIX_MATCH|FIX_PENDING;
}
__attribute__((noinline)) static u32 og_fix_pair(u32 i,const struct z63_nraw_row *live) {
    const struct z63_nraw_row *table=(const struct z63_nraw_row *)(uintptr_t)NRAW_TABLE;
    const struct z63_nraw_row *high=table+i,*normal=table+i+NRAW_PAIRS;
    u32 kind=high->dimensions==(6060u|3410u<<16)?6u:high->dimensions==(4040u|2276u<<16)?4u:0u;
    if(!kind||!og_same_rate(high,live))return FIX_NONE;
    u32 r=og_fix_tier(kind,high,normal,0u,live);
    if(!(r&FIX_MATCH))r=og_fix_tier(kind,high,normal,1u,live);
    if(!(r&FIX_MATCH))r=og_fix_tier(kind,high,normal,2u,live);
    if(!(r&FIX_MATCH))r=og_fix_tier(kind,high,normal,3u,live);
    return r;
}
/* The og_sync tables back to stock (the design's sanitise list, run here rather than as core
 * Sanitise entries: ten entries would cost 400 B of core table and leave the composed core below
 * its 1,024 B margin). 0 stock, 1 a CAS lost, 2 a value that is neither known one (left). */
static u32 og_unsync_word(u64 a,u32 stock,u32 open) {
    u32 v=og_ld16(a);
    if(v==stock)return 0;
    if(v!=open)return 2;
    return og_cas16(a,open,stock)?0u:1u;
}
__attribute__((noinline)) static u32 og_unsync(void) {
    u32 r=0;
    r|=og_unsync_word(0xa02bc7944ull,3432,4040);
    r|=og_unsync_word(0xa02bc7970ull,3432,4040);
    r|=og_unsync_word(0xa0298eebeull,3432,4038);
    r|=og_unsync_word(0xa0298f05aull,3432,4038);
    r|=og_unsync_word(0xa0298f0b0ull,2288,2692);
    r|=og_unsync_word(0xa02bc820eull,3432,4040);
    r|=og_unsync_word(0xa02bc80f2ull,3402,4032);
    r|=og_unsync_word(0xa02bc80faull,14,4);
    r|=og_unsync_word(0xa02bc8112ull,2268,2688);
    r|=og_unsync_word(0xa02bc811aull,10,2);
    return r;
}
/* The run-time words to stock (tables, then the global row): 0 done, 1 a CAS lost (again
 * later), 2 something left (a value neither known one). No stock call, no state write. */
__attribute__((noinline)) static u32 og_words_to_stock(void) {
    u32 tables=og_unsync();
    const volatile struct z63_nraw_row *g=(const volatile struct z63_nraw_row *)(uintptr_t)GLOBAL_ROW;
    struct z63_nraw_row live;
    live.dimensions=g->dimensions;live.numerator=g->numerator;live.denominator=g->denominator;
    live.flags=g->flags;live.bits_per_second=g->bits_per_second;
    u32 seen=0,out=0;
    if(og_dims_open(live.dimensions&0xffffu,live.dimensions>>16)) {
        #pragma clang loop unroll(full)
        for(u32 i=0;i<NRAW_PAIRS;i++)if(!(seen&FIX_MATCH))seen|=og_fix_pair(i,&live);
        out=(seen&FIX_PENDING)?1u:(seen&FIX_MATCH)?0u:2u;
    }
    if(tables&1u)out=1u;
    else if((tables&2u)&&!out)out=2u;
    return out;
}
u32 z63r_g10(u64 page) {
    u32 out=og_words_to_stock();
    if(page) {
        og_latch(page,0,0);og_put8(page,OG_REPLAN_LAST,0);og_put8(page,OG_TWIN,0);og_put8(page,OG_PLAN_MARK,0);
        og_put32(page,OG_LAST_TIER,Z63_TIER_NONE);
    }
    return out;
}
/* Break callback (Group.restore_now, R-G10-1): the core's armed poll runs it once G10 is broken
 * (an integrity break, a raised break) in LIVE or DRAINING, as soon as nothing is recording and
 * no TX or ACT is counted; so a stock re-plan in movie view after the break reads stock tables
 * and the stock rate row at once, not only after the restore pass under Q. Same work as the
 * restore callback's words, and (re-plan build) the latch cleared with them: every reader ANDs
 * it with the phase anyway (PASS for a broken group), and g10_replan does nothing in PASS, so
 * tables and latch both read "stock" from here on. 0 = done (a value left alone is reported by
 * the restore pass later), 1 = a CAS lost: run again at the next poll. */
u32 z63n_g10(u64 page) {
    u32 r=og_words_to_stock();
    if(page)og_latch(page,0,0);
    return r==1u?1u:0u;
}

/* ---- parts ---- */
#include "open_gate_geom.c"
#if __has_include("open_gate_entries.c")
#include "open_gate_entries.c"
#endif
#if __has_include("open_gate_imenu.c")
#include "open_gate_imenu.c"
#endif
#if __has_include("open_gate_rec.c")
#include "open_gate_rec.c"
#endif
#if __has_include("open_gate_af.c")
#include "open_gate_af.c"
#endif
#if __has_include("open_gate_play.c")
#include "open_gate_play.c"
#endif

/* ---- re-plan (an open-gate choice applies when the menu closes in movie view) ----
 *
 * g10_replan, BL->BL at 0x40F681FC (stock 94001B86 `bl 0x40F6F014`): the live-view applier's only
 * direct call. Both routes to it go through this one word: the differ path (b 0x40F681F8 from
 * 0x40F68094) and the mode-8 route 0x40F681C4..0x40F681F4 (straight line from the b.ne at
 * 0x40F680C8 on the equal path, falling into 0x40F681F8). The applier reads only x0 (the new live-
 * view state) and runs the classifier; the sensor setup that plans (0x410AD3EC -> 0x4105D07C) runs
 * after it on a later step. So moving tables and latch here, before the stock call, puts every
 * reader on the new kind before stock reads any geometry for the new plan.
 *
 * Guard (og_replan_ok): G10 LIVE, G2 LIVE (the save path and the stored record are ours), nothing
 * recording (movie work 0xA03324CE0 and outstanding 0xA03324DC8 both 0), and no ACT[G10 & 3] and no
 * TX of any class counted (no aliased AF call deciding, no save in progress); then the step holds
 * ACT[G10 & 3] itself (og_replan_step) so a break callback cannot interleave. Under the guard:
 * want = og_stored_kind() (stored bit 20 valid only in a v2/v3 record with a good CRC; file type 6,
 * size slot 6 or 4, rate slot 6..8, movie word 1); og_sync to want (bounded CAS from the latch rows
 * to the wanted rows); a word left neither known value (FOREIGN) puts the tables back to stock and
 * want to 0; then the latch to want and the plan mode on (this is movie view: the guard's movie
 * word; the plan post-fix that follows confirms or lowers it). In every other case nothing is read
 * or stored. Then the stock
 * applier call with the caller's x0-x7 (the record's C wrapper; the caller reads no caller-saved
 * register after it, static guard in the tests).
 *
 * Stored-word exception to the publication rule (README 6): this is the one G10 decision that reads
 * the STORED word. Allowed because (1) it is read once, on the live-view task, at the instant stock
 * itself re-plans, with nothing recording and no G10 ACT or any TX counted; (2) the value it
 * produces is used only through the latch, which every reader reads; (3) the record must decode as
 * v2/v3 with a good CRC (z63_settings_decode), so a foreign or corrupt word gives 0.
 */
u64 stock_og_applier(u64,u64,u64,u64,u64,u64,u64,u64);   /* 0x40F6F014 live-view applier */
u64 stock_og_compare(u64,u64,u64,u64,u64,u64,u64,u64);   /* 0x40F6E8F4 live-view state compare */
static inline __attribute__((always_inline)) u32 og_replan_ok(u64 p,u32 lower) {
    if(!p)return 0;
    u32 ph=z63g_phase(p,G10);
    if(z63g_ld32(OG_MOVIE_WORD)==1u) {
        if(ph!=Z63_LIVE||z63g_phase(p,2u)!=Z63_LIVE)return 0;
    } else if(!lower||!ph||!og_st(p)[OG_MPLAN])return 0;   /* photo view (plan mode off): the latch is z63p_g10's */
    if(z63g_ld32(OG_MOVIE_WORK)||z63g_ld32(OG_MOVIE_OUT))return 0;
    u64 g=p+Z63_GATE_OFF;
    if(z63g_acq32(g+Z63_G_ACT+4u*(G10&3u)))return 0;
    return !(z63g_acq32(g+Z63_G_TX)|z63g_acq32(g+Z63_G_TX+4u)|z63g_acq32(g+Z63_G_TX+8u)|z63g_acq32(g+Z63_G_TX+12u));
}
/* The re-plan step: the kind latched after it, or 0x100 when the guard refused (nothing done).
 * The work runs inside an ACT[G10 & 3] hold (taken after the guard saw the count at 0): the break
 * callback and the restore pass run only when every ACT count is 0, and act_enter re-reads the
 * phase after counting, so a break either comes before (nothing is done here) or its callback
 * runs after this step (and then puts tables and latch back to stock).
 * Readiness 1 (LIVE): the stored kind, 0 when a plan post-fix marked a foreign record (OG_PLAN_MARK);
 * tables first, then latch and plan mode. Readiness not 1 while the plan mode holds (a card pulled,
 * full or failed, then a menu close or any other applier call; LIVE or DRAINING): the lowering
 * form, latch and plan mode to 0 first, a barrier, then the tables to stock, before the stock
 * applier and the plan it starts (per-frame rule: the plan post-fix never lowers, so this applier
 * call is where a held plan ends in movie view). */
__attribute__((noinline)) u32 og_replan_step(u64 p) {
    if(!og_replan_ok(p,1))return 0x100u;
    u32 ph=z63g_act_enter(p,G10);
    if(!ph)return 0x100u;
    u32 want=0x100u;
    if(z63g_ld32(OG_MOVIE_WORD)==1u) {
        if(ph==Z63_LIVE) {
            want=og_st(p)[OG_PLAN_MARK]?0u:og_stored_kind(p);   /* = og_rule_replan_kind (G7's prediction) plus the stale marks */
            og_sync(p,want);
            if(og_st(p)[OG_SYNC_STATE]&OG_SYNC_FOREIGN) {og_sync(p,0);want=0;}
            if(og_st(p)[OG_KIND]!=want||og_st(p)[OG_ACTIVE]!=(want!=0u)||!og_st(p)[OG_MPLAN])og_latch(p,want,1);
            og_put8(p,OG_REPLAN_LAST,0x80u|want);
            if(og_st(p)[OG_TWIN])og_put8(p,OG_TWIN,0);
            if(og_st(p)[OG_PLAN_MARK])og_put8(p,OG_PLAN_MARK,0);
        }
    } else if(og_st(p)[OG_MPLAN]) {
        want=0;
        og_latch(p,0,0);
        z63g_dmb();
        og_sync(p,0);
        og_put8(p,OG_REPLAN_LAST,0xC0u);
        if(og_st(p)[OG_PLAN_MARK])og_put8(p,OG_PLAN_MARK,0);
    }
    z63g_act_leave(p,G10);
    return want;
}
/* The record's adapter: x0-x8 kept on the stack around the C step, then a tail branch to the stock
 * applier (the verifier's allowed tail: the replaced call's target), so the applier runs with the
 * caller's x0-x8, x30 (= 0x40F68200) and sp exactly as from the stock BL. */
__attribute__((used)) void g10_replan_c(void) {
    u64 p=z63g_page();
    if(p)og_replan_step(p);
}
__asm__(
".text\n"
".p2align 2\n"
".global g10_replan\n"
".type g10_replan,%function\n"
"g10_replan:\n"
"  stp x29,x30,[sp,#-0x60]!\n"
"  mov x29,sp\n"
"  stp x0,x1,[sp,#0x10]\n"
"  stp x2,x3,[sp,#0x20]\n"
"  stp x4,x5,[sp,#0x30]\n"
"  stp x6,x7,[sp,#0x40]\n"
"  str x8,[sp,#0x50]\n"
OG_LOAD_P
"  bl g10_replan_c\n"
"  ldp x0,x1,[sp,#0x10]\n"
"  ldp x2,x3,[sp,#0x20]\n"
"  ldp x4,x5,[sp,#0x30]\n"
"  ldp x6,x7,[sp,#0x40]\n"
"  ldr x8,[sp,#0x50]\n"
"  ldp x29,x30,[sp],#0x60\n"
"  b stock_og_applier\n"
".size g10_replan,.-g10_replan\n"
);
/* Option C (built, OFF by default: open_gate.TWIN_COMPARE, variant open_gate_twin): BL->BL post-fix
 * on the SECOND compare call only, 0x40F67FC4 (stock 94001A4C `bl 0x40F6E8F4`), whose result
 * decides differ (applier) or equal. The first call 0x40F67EC4 (its result selects a state copy) and
 * the compare's epilogue are not touched. When stock says equal (bit 0 clear), og_replan_ok, none of
 * stock's own no-difference overrides holds (mode 0xA03D804AC == 7, phase 0xA03D804CC == 3 or 5,
 * hold byte 0xA03D814F4 == 1, override 0xA03D8086C == 1 with new state +0 == 1 and +4 == 1: the old
 * layout's list), and og_stored_kind() differs from the latch: return 1 ("differ") so stock itself
 * re-plans and g10_replan then latches. Once per (want, latch) pair until the applier runs
 * (OG_TWIN), so a differ that stock routes to its in-place path (0x40F68218 -> 0x40F69BB4) cannot
 * be forced again on every step. Every other case: stock's x0 unchanged. Stores nothing but
 * OG_TWIN (no reason bit: the old layout's bit 31 of 0xA03D814E8 is not written). */
#define OG_LV_MODE     0xa03d804acull
#define OG_LV_PHASE    0xa03d804ccull
#define OG_LV_OVERRIDE 0xa03d8086cull
#define OG_LV_HOLD     0xa03d814f4ull
u64 g10_twin_cmp(OG_ARGS) {
    u64 p=z63g_page();
    u64 r=stock_og_compare(OG_PASS);
    if((r&1u)||!og_replan_ok(p,0))return r;
    if(z63g_ld32(OG_LV_MODE)==7u||z63g_ld32(OG_LV_PHASE)==3u||z63g_ld32(OG_LV_PHASE)==5u)return r;
    if(*(volatile const u8 *)(uintptr_t)OG_LV_HOLD==1u)return r;
    if(z63g_ld32(OG_LV_OVERRIDE)==1u&&(!a1||(z63g_ld32(a1)==1u&&z63g_ld32(a1+4u)==1u)))return r;
    u32 want=og_stored_kind(p),latch=og_st(p)[OG_KIND],mark=0x80u|want|latch<<4;
    if(want==latch||og_st(p)[OG_TWIN]==mark)return r;
    og_put8(p,OG_TWIN,mark);
    return 1;
}

/* ---- photo-view pre-set: the second way the latch is RAISED ----
 *
 * z63p_g10, G10's publication callback (Group.publish): the core runs it on the armed poll (card-
 * detect task) after a publication, and on every later poll while its bit in page +0xC40 is set,
 * always with every TX and ACT count 0, the fence holding and the hold free. It returns 1 every
 * time, so it stays pending and is re-run on each such poll: the movie slots (size, rate, file
 * type) can change in photo view without the settings word changing.
 *
 * It acts only in settled photo view, Q, checked here (the fence alone also passes in idle movie
 * view): pipeline 0xA03D42100 == 2, class 0xA03D42108 == 5, movie word != 1, capture work, movie
 * work and movie outstanding all 0; and only with G10 and G2 LIVE (the stored record is ours).
 * Then the latch goes to og_want_rule(word): the movie-view rule (og_size_word) without its
 * movie-word term, on the word just published, which under Q is the stored word, and the plan
 * mode goes off (photo view: the photo plan post-fix normally cleared it already; this covers a
 * photo entry whose plan chose no record). No reader acts on the latch while the plan mode is off,
 * so at the next movie entry every reader sees the kind planned here once stock re-plans for movie
 * (g10_replan or the movie plan post-fix sets the plan mode).
 *
 * Tables are not raised here (they are put to stock when the latch goes to 0: the plan post-fix
 * no longer lowers, so a latch lowered here must not leave open tables behind). Their only static readers are the area-indexed geometry leaves
 * 0x4105EAD0 / 0x4105EB14 / 0x4105EB40 (tables 0xA02BC790A, 0xA02BC7936, 0xA02BC7962 + 4 x area;
 * +0x0E and +0x3A of area 3 are the two og_sync words), reached from the sensor setup's helpers
 * after its plan call; whether photo live view reads area 3 is not established, so the tables
 * follow the latch at the first movie plan (g10_plan: og_sync to the latch kind before the record
 * edit, inside the same post-fix) or at g10_replan, whichever comes first, and the plan post-fix
 * puts them back to stock at a photo plan.
 *
 * Photo Q (pipeline class 5 among it) is read again just before the latch store; a switch to movie
 * view between that read and the store is the same window the core's own entry-word store has
 * under Q. */
#define OG_PIPE_STATE 0xa03d42100ull
#define OG_PIPE_CLASS 0xa03d42108ull
#define OG_CAPTURE    0xa048bb044ull
static inline __attribute__((always_inline)) u32 og_photo_q(void) {
    return z63g_ld32(OG_PIPE_STATE)==2u&&z63g_ld32(OG_PIPE_CLASS)==5u&&z63g_ld32(OG_MOVIE_WORD)!=1u&&
           !z63g_ld32(OG_CAPTURE)&&!z63g_ld32(OG_MOVIE_WORK)&&!z63g_ld32(OG_MOVIE_OUT);
}
u32 z63p_g10(u64 page,u32 word) {
    if(!page||z63g_phase(page,G10)!=Z63_LIVE||z63g_phase(page,2u)!=Z63_LIVE||!og_photo_q())return 1u;
    u32 want=og_want_rule(page,word);
    if(og_st(page)[OG_KIND]==want&&og_st(page)[OG_ACTIVE]==(want!=0u)&&!og_st(page)[OG_MPLAN])return 1u;
    if(!og_photo_q())return 1u;
    og_latch(page,want,0);
    if(!want&&og_st(page)[OG_SYNC_STATE])og_sync(page,0);   /* lowered: tables with it (the post-fix never lowers them) */
    return 1u;
}
