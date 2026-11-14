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
/* The arm-time gate as wrappers see it: the lookup, the
 * phase test of one group, the ACT count that fences publication, the published settings word
 * and the one-way fault bits. Used by the gate core (native/arm_time/gate.c) and by every
 * feature group (native/arm_time/groups/<name>.c).
 *
 * Header only (static inline): a group's code never calls into the gate core, so its bytes do
 * not depend on where the core's functions are linked (the core is rebuilt for every
 * composition). Every exclusive load/store loop has the one shape the verifier accepts:
 * "mov wT,#N" (N <= 64) just before the loop head, the exclusive load at the head, the
 * exclusive store followed by "cbz wS", then "subs wT,wT,#1; b.ne head".
 *
 * Rules for a wrapper (spec 3.5): call z63g_page() before any other load or store outside the
 * stack frame; on every path where z63g_phase() (or z63g_act_enter()) gives 0, do exactly what
 * the stock code at the site does and store nothing outside the frame; never spin, sleep, take
 * a lock or mask interrupts; no allocation. Asm value-site adapters use the same addresses
 * (Z63_P, Z63_GATE_OFF, ...) with the register budget their site allows. */
#ifndef Z63_GATE_API_H
#define Z63_GATE_API_H
#include <stdint.h>

#define Z63_P            0xa028cae20ull  /* publication word: video menu page +0x10 (record 0) */
#define Z63_RAM_LO       0xa00000000ull
#define Z63_PAGE_HI      0xa3ffff000ull  /* p >= this is "no page" (1.2) */
#define Z63_GATE_OFF     0xf80u          /* gate block in the state page */
#define Z63_GMAGIC       0x5447365au     /* "Z6GT" */
#define Z63_G_MAGIC      0x00u
#define Z63_G_WORD       0x04u           /* bits 0-3 phase, 8-15 reason, 16-31 broken groups */
#define Z63_G_SELF       0x08u
#define Z63_G_FAULT      0x2cu
#define Z63_G_PUBBUSY    0x38u
#define Z63_G_EFFECT     0x3cu
#define Z63_G_TX         0x40u           /* u32[4] */
#define Z63_G_ACT        0x50u           /* u32[4] */
#define Z63_STATE_OFF    0x9b0u          /* counters; +28 = the groups selected at arming */
#define Z63_SELECTED     (Z63_STATE_OFF+28u)
enum {Z63_CLOSED=0,Z63_LIVE=1,Z63_DRAINING=2,Z63_RESTORING=3,Z63_STOCK=4};
enum {Z63_F_OVERFLOW=1u,Z63_F_UNDERFLOW=2u,Z63_F_LEAK=4u,Z63_F_RACE=8u};

static inline uint32_t z63g_ld32(uint64_t a){return *(volatile const uint32_t *)(uintptr_t)a;}
static inline uint64_t z63g_ld64(uint64_t a){return *(volatile const uint64_t *)(uintptr_t)a;}
static inline uint32_t z63g_acq32(uint64_t a){return __atomic_load_n((const uint32_t *)(uintptr_t)a,__ATOMIC_ACQUIRE);}

/* 1.2: the page, or 0 (every wrapper passes through). The acquire load of P is the first
 * access outside the stack. */
static inline uint64_t z63g_page(void) {
    uint64_t p=__atomic_load_n((const uint64_t *)(uintptr_t)Z63_P,__ATOMIC_ACQUIRE);
    if(p<Z63_RAM_LO||p>=Z63_PAGE_HI||(p&0xfffu))return 0;
    if(z63g_ld32(p+Z63_GATE_OFF+Z63_G_MAGIC)!=Z63_GMAGIC||z63g_ld64(p+Z63_GATE_OFF+Z63_G_SELF)!=p)return 0;
    return p;
}
static inline uint32_t z63g_word(uint64_t p){return z63g_acq32(p+Z63_GATE_OFF+Z63_G_WORD);}

/* The phase a C-class contributor of group `group` runs in: LIVE or DRAINING when the group was
 * selected at arming and is not broken, else 0 (PASS). Loads only. */
static inline uint32_t z63g_phase(uint64_t p,uint32_t group) {
    uint32_t g=z63g_word(p),ph=g&15u;
    if((ph!=Z63_LIVE&&ph!=Z63_DRAINING)||((g>>16>>group)&1u))return 0;
    if(!((z63g_ld32(p+Z63_SELECTED)>>group)&1u))return 0;
    return ph;
}

/* ---- bounded exclusive load/store (1.6g) ---- 0 stored, 1 refused by the limit, 2 exhausted. */
static inline uint32_t z63g_inc8(volatile uint32_t *p) {   /* +1, refused at >= 0x8000, 8 attempts */
    uint32_t v,n,s,t,r;
    __asm__ volatile(
        "mov %w[t],#8\n"
        "1: ldaxr %w[v],[%[p]]\n"
        "cmp %w[v],#8,lsl #12\n"
        "b.hs 3f\n"
        "add %w[n],%w[v],#1\n"
        "stlxr %w[s],%w[n],[%[p]]\n"
        "cbz %w[s],2f\n"
        "subs %w[t],%w[t],#1\n"
        "b.ne 1b\n"
        "mov %w[r],#2\n"
        "b 4f\n"
        "3: clrex\n"
        "mov %w[r],#1\n"
        "b 4f\n"
        "2: mov %w[r],#0\n"
        "4:\n"
        :[v]"=&r"(v),[n]"=&r"(n),[s]"=&r"(s),[t]"=&r"(t),[r]"=&r"(r):[p]"r"(p):"memory","cc");
    return r;
}
static inline uint32_t z63g_dec64(volatile uint32_t *p) {  /* -1, refused at 0, 64 attempts */
    uint32_t v,n,s,t,r;
    __asm__ volatile(
        "mov %w[t],#64\n"
        "1: ldaxr %w[v],[%[p]]\n"
        "cbz %w[v],3f\n"
        "sub %w[n],%w[v],#1\n"
        "stlxr %w[s],%w[n],[%[p]]\n"
        "cbz %w[s],2f\n"
        "subs %w[t],%w[t],#1\n"
        "b.ne 1b\n"
        "mov %w[r],#2\n"
        "b 4f\n"
        "3: clrex\n"
        "mov %w[r],#1\n"
        "b 4f\n"
        "2: mov %w[r],#0\n"
        "4:\n"
        :[v]"=&r"(v),[n]"=&r"(n),[s]"=&r"(s),[t]"=&r"(t),[r]"=&r"(r):[p]"r"(p):"memory","cc");
    return r;
}
static inline void z63g_or64(volatile uint32_t *p,uint32_t bits) {   /* one-way bit set, 64 attempts */
    uint32_t v,s,t;
    __asm__ volatile(
        "mov %w[t],#64\n"
        "1: ldaxr %w[v],[%[p]]\n"
        "orr %w[v],%w[v],%w[b]\n"
        "stlxr %w[s],%w[v],[%[p]]\n"
        "cbz %w[s],2f\n"
        "subs %w[t],%w[t],#1\n"
        "b.ne 1b\n"
        "2:\n"
        :[v]"=&r"(v),[s]"=&r"(s),[t]"=&r"(t):[p]"r"(p),[b]"r"(bits):"memory","cc");
}
static inline void z63g_dmb(void){__asm__ volatile("dmb ish":::"memory");}
static inline void z63g_fault(uint64_t p,uint32_t bits){z63g_or64((volatile uint32_t *)(uintptr_t)(p+Z63_GATE_OFF+Z63_G_FAULT),bits);}

/* C class with a use of the published word (1.3, 1.6): count ACT[group & 3] (store, barrier,
 * then the phase again), so a publication either waits for this call or happened before it.
 * Returns the phase (LIVE or DRAINING) with the count held, or 0 with nothing counted (PASS).
 * Every nonzero return is followed by exactly one z63g_act_leave(). */
static inline uint32_t z63g_act_enter(uint64_t p,uint32_t group) {
    if(!z63g_phase(p,group))return 0;
    volatile uint32_t *act=(volatile uint32_t *)(uintptr_t)(p+Z63_GATE_OFF+Z63_G_ACT+4u*(group&3u));
    uint32_t r=z63g_inc8(act);
    if(r) {if(r==1)z63g_fault(p,Z63_F_OVERFLOW);return 0;}
    z63g_dmb();
    uint32_t ph=z63g_phase(p,group);
    if(!ph) {r=z63g_dec64(act);if(r)z63g_fault(p,r==1?Z63_F_UNDERFLOW:Z63_F_LEAK);return 0;}
    return ph;
}
static inline void z63g_act_leave(uint64_t p,uint32_t group) {
    uint32_t r=z63g_dec64((volatile uint32_t *)(uintptr_t)(p+Z63_GATE_OFF+Z63_G_ACT+4u*(group&3u)));
    if(r)z63g_fault(p,r==1?Z63_F_UNDERFLOW:Z63_F_LEAK);
}
/* The published settings word for a caller holding an ACT (or TX) count: 1 << 32 | word, or 0
 * (pass through) only when a publication stays in progress through the bounded wait below. Its
 * only store is the exclusive write-back of the busy value. In DRAINING the word is frozen (the
 * armed poll publishes only in LIVE).
 * A publication's busy window is a few instructions of the armed poll (no call). A reader
 * that finds it busy waits for it to close (64 bounded tries; the exclusive store writes back the
 * busy value it read and fails if the poll stored meanwhile), so every read inside one hold gives
 * the same word: a hold the poll saw makes it abort (old word throughout), a hold it did not see
 * reads only after the store (new word throughout). Only if the poll stays inside the window for
 * all 64 tries (preempted there) does the reader pass through. */
/* Core words at page +0xC40 (16 B): pending publication callbacks, the anchor hold,
 * the movie-entry word (R-G10-0), group-raised breaks (R-G10-4: bits 0-15, group CAS) and break
 * callbacks done (R-G10-1: bits 16-31, core CAS). */
#define Z63_PUBCB_OFF  0xc40u
#define Z63_ENTRY_OFF  0xc48u
#define Z63_RAISE_OFF  0xc4cu
static inline uint64_t z63g_effective(uint64_t p) {
    z63g_dmb();
    uint64_t a=p+Z63_GATE_OFF+Z63_G_PUBBUSY;
    uint32_t v,s,t;
    __asm__ volatile(
        "mov %w[t],#64\n"
        "1: ldaxr %w[v],[%[a]]\n"
        "cbz %w[v],2f\n"
        "stxr %w[s],%w[v],[%[a]]\n"
        "cbz %w[s],3f\n"                 /* either way: the next try (the verifier's loop shape) */
        "3: subs %w[t],%w[t],#1\n"
        "b.ne 1b\n"
        "2: clrex\n"
        :[v]"=&r"(v),[s]"=&r"(s),[t]"=&r"(t):[a]"r"(a):"memory","cc");
    (void)s;
    if(v)return 0;
#ifdef Z63_EFFECTIVE_ENTRY
    /* R-G10-0: a group without movie_publish (the builder defines this for its translation unit)
     * reads the movie-entry word: the published word as of the last publication under Q, so its
     * field never changes in movie view while the other groups' fields do. */
    return 1ull<<32|z63g_acq32(p+Z63_ENTRY_OFF);
#else
    return 1ull<<32|z63g_acq32(p+Z63_GATE_OFF+Z63_G_EFFECT);
#endif
}

/* ---- transactions (A class, spec 1.6a) ----
 * TX class c (0 SAVE, 1 IMENU, 2-3 reserved): admission only in LIVE with `group` selected and
 * not broken; +1 on TX[c] (8 attempts, refused at 0x8000), barrier, then the phase again. Returns
 * the page with the count held, or 0 (refused, nothing counted). Every nonzero return is followed
 * by exactly one z63g_tx_leave(page, c), on every path. While TX[c] is held the armed poll does
 * not publish and the drain waits (a record pinned to c is not restored while c is stuck). */
static inline uint64_t z63g_tx_enter(uint64_t p,uint32_t c,uint32_t group) {
    if(!p||z63g_phase(p,group)!=Z63_LIVE)return 0;
    volatile uint32_t *tx=(volatile uint32_t *)(uintptr_t)(p+Z63_GATE_OFF+Z63_G_TX+4u*(c&3u));
    uint32_t r=z63g_inc8(tx);
    if(r) {if(r==1)z63g_fault(p,Z63_F_OVERFLOW);return 0;}
    z63g_dmb();
    if(z63g_phase(p,group)!=Z63_LIVE) {
        r=z63g_dec64(tx);if(r)z63g_fault(p,r==1?Z63_F_UNDERFLOW:Z63_F_LEAK);return 0;
    }
    return p;
}
static inline void z63g_tx_leave(uint64_t p,uint32_t c) {
    uint32_t r=z63g_dec64((volatile uint32_t *)(uintptr_t)(p+Z63_GATE_OFF+Z63_G_TX+4u*(c&3u)));
    if(r)z63g_fault(p,r==1?Z63_F_UNDERFLOW:Z63_F_LEAK);
}
/* Bounded compare-and-swap of a u32 (64 attempts): 1 when `next` was stored over `old`. */
static inline uint32_t z63g_cas32(uint64_t a,uint32_t old,uint32_t next) {
    uint32_t v,s,t,r;
    __asm__ volatile(
        "mov %w[t],#64\n"
        "1: ldaxr %w[v],[%[a]]\n"
        "cmp %w[v],%w[o]\n"
        "b.ne 3f\n"
        "stlxr %w[s],%w[n],[%[a]]\n"
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
/* Replace bits 8-31 of a u32 while they still equal those of `from`, keeping bits 0-7 as they
 * are (16 attempts; the native_replace rule of native/native_settings.c in one bounded
 * exclusive loop): 1 stored, 0 the high bits are not `from`'s or the attempts ran out. */
static inline uint32_t z63g_replace_high(uint64_t a,uint32_t from,uint32_t to) {
    uint32_t v,n,s,t,r;
    __asm__ volatile(
        "mov %w[t],#16\n"
        "1: ldaxr %w[v],[%[a]]\n"
        "eor %w[n],%w[v],%w[f]\n"
        "lsr %w[n],%w[n],#8\n"
        "cbnz %w[n],3f\n"
        "and %w[n],%w[v],#0xff\n"
        "orr %w[n],%w[n],%w[h]\n"
        "stlxr %w[s],%w[n],[%[a]]\n"
        "cbz %w[s],2f\n"
        "subs %w[t],%w[t],#1\n"
        "b.ne 1b\n"
        "3: clrex\n"
        "mov %w[r],#0\n"
        "b 4f\n"
        "2: mov %w[r],#1\n"
        "4:\n"
        :[v]"=&r"(v),[n]"=&r"(n),[s]"=&r"(s),[t]"=&r"(t),[r]"=&r"(r):[a]"r"(a),[f]"r"(from),[h]"r"(to&0xffffff00u):"memory","cc");
    return r;
}

/* ---- feature state (fixed per-group offsets, z63patch/gate_groups PLAN_STATE; the builder
 * checks these against it) ---- */
#define Z63_STATE_G2   0x840u   /* 112 B: menu state, save block at +0x30 (save_api.h), font record; no menu row */
#define Z63_STATE_G4   0x8b0u   /* 8 B */
#define Z63_STATE_G7   0x8b8u   /* 16 B */
#define Z63_STATE_G9   0x8c8u   /* 8 B */
#define Z63_STATE_G10  0x8d0u   /* 64 B; +0 og_active (u8), read by G7 */
#define Z63_STATE_G6   0x9a0u   /* 8 B: +0 anchor value word */
#define Z63_STATE_G8   0x9a8u   /* 8 B: +0 anchor value word */
#define Z63_STATE_G11  0x920u   /* 128 B: RAW pre-capture frame table */
#define Z63_STATE_BYTES_G2  112u
#define Z63_STATE_BYTES_G4  8u
#define Z63_STATE_BYTES_G6  8u
#define Z63_STATE_BYTES_G7  16u
#define Z63_STATE_BYTES_G8  8u
#define Z63_STATE_BYTES_G9  8u
#define Z63_STATE_BYTES_G10 64u
#define Z63_STATE_BYTES_G11 128u
/* Own state: z63g_state(p, Z63_STATE_Gn). Another group's state, read only:
 * z63g_peer_state(p, Z63_STATE_Gn) (const). Valid only with a page from z63g_page(); a group
 * writes only its own bytes, and only on the paths its declaration allows (stores=True). */
static inline volatile uint8_t *z63g_state(uint64_t p,uint32_t off){return (volatile uint8_t *)(uintptr_t)(p+off);}
static inline const volatile uint8_t *z63g_peer_state(uint64_t p,uint32_t off){return (const volatile uint8_t *)(uintptr_t)(p+off);}
#define Z63_TIER_NONE 0xffffffffu

/* ---- anchor holds (Group.hold) ----
 * One hold word per composition at page +Z63_HOLD_OFF (core), and one value word per group in
 * the group's own state (Group.hold). An anchor read (a read that starts what one recording will
 * use, or fills a stock cache that a recording reads) takes the hold: while HELD the armed poll
 * does not publish outside Q, so the published word, and so every later read, stays what the
 * anchor saw. The core releases the hold after Z63_HOLD_QUIET consecutive polls of native idle
 * with no anchor read in between (each anchor read sets TOUCH and clears the count), and at once
 * in settled photo live view (Q). Hold word: bit 31 HELD, 30 TOUCH, 16-23 quiet count (core).
 * Value word: VALID | the anchor's value; it stays after release, so a follower read that belongs
 * to the recording an anchor started (a file writer) uses it and agrees with the anchor even when
 * a publication lands after the release. Group writes: by CAS only, inside an ACT hold. */
#define Z63_HOLD_OFF   0xc44u   /* (with the core words above) */
#define Z63_HOLD_HELD  0x80000000u
#define Z63_HOLD_TOUCH 0x40000000u
#define Z63_HOLD_QUIET 0x00ff0000u
#define Z63_HOLD_VALID 0x00008000u
#define Z63_HOLD_VALUE 0x00007fffu
/* An anchor read, with the caller's ACT hold and `fresh` from its z63g_effective() (a publication
 * cannot land between that read and the hold: the ACT count refuses it; the core checks the hold
 * again after its busy store, so a hold taken before it is seen). Returns fresh. */
static inline uint32_t z63g_hold_anchor(uint64_t p,uint32_t off,uint32_t fresh) {
    uint64_t a=p+Z63_HOLD_OFF;
    fresh&=Z63_HOLD_VALUE;
#pragma clang loop unroll(full)
    for(uint32_t k=0;k<4u;k++) {
        uint32_t v=z63g_ld32(a);
        if(z63g_cas32(a,v,(v&~Z63_HOLD_QUIET)|Z63_HOLD_HELD|Z63_HOLD_TOUCH))break;
    }
    uint32_t w=z63g_ld32(p+off);
    if(w!=(Z63_HOLD_VALID|fresh))(void)z63g_cas32(p+off,w,Z63_HOLD_VALID|fresh);
    return fresh;
}
/* A follower read: the last anchor value when there is one, else `fresh`. */
static inline uint32_t z63g_hold_last(uint64_t p,uint32_t off,uint32_t fresh) {
    uint32_t v=z63g_ld32(p+off);
    return (v&Z63_HOLD_VALID)?(v&Z63_HOLD_VALUE):(fresh&Z63_HOLD_VALUE);
}

/* An anchor read made by a producer that also runs continuously in idle movie view (a per-frame
 * builder: z63g_hold_anchor there would keep the hold taken, so movie-view
 * publication would never land). While a capture or recording is in progress (a work word
 * nonzero, as the core's native idle) or the record button is held (switch snapshot bit 0x2000,
 * so a start whose first reads come before its work words still holds): as z63g_hold_anchor.
 * Otherwise no hold is taken or touched (the core's quiet count runs, the hold is released,
 * publication lands): the value word is refreshed to `fresh`, so a follower reads the value of
 * the last idle build. The caller's ACT hold and `fresh` as for z63g_hold_anchor. Returns fresh
 * (while HELD no publication lands outside Q, so fresh is the value the hold latched). */
#define Z63G_CAPTURE_WORK      0xa048bb044ull
#define Z63G_MOVIE_WORK        0xa03324ce0ull
#define Z63G_MOVIE_OUTSTANDING 0xa03324dc8ull
#define Z63G_SWITCHES          0xa0336bdb0ull
#define Z63G_RECORD_BUTTON     0x2000u
static inline uint32_t z63g_recording(void) {
    return (z63g_ld32(Z63G_CAPTURE_WORK)|z63g_ld32(Z63G_MOVIE_WORK)|z63g_ld32(Z63G_MOVIE_OUTSTANDING))!=0||
           (z63g_ld32(Z63G_SWITCHES)&Z63G_RECORD_BUTTON)!=0;
}
static inline uint32_t z63g_hold_recording(uint64_t p,uint32_t off,uint32_t fresh) {
    if(z63g_recording())return z63g_hold_anchor(p,off,fresh);
    fresh&=Z63_HOLD_VALUE;
    uint32_t w=z63g_ld32(p+off);
    if(w!=(Z63_HOLD_VALID|fresh))(void)z63g_cas32(p+off,w,Z63_HOLD_VALID|fresh);
    return fresh;
}

/* ---- group-raised integrity break (R-G10-4) ----
 * Sets bit `group` of the raise word (one bounded CAS loop, nothing else). The next armed poll
 * treats it as an integrity break of that group (the gate goes to DRAINING with the group's
 * broken bit, its break callback runs, restore as usual). Until that poll the group's own code
 * sees it through z63g_raised() and must pass through. Returns 1 when the bit is set. */
static inline uint32_t z63g_raise(uint64_t p,uint32_t group) {
    uint64_t a=p+Z63_RAISE_OFF;
    uint32_t bit=1u<<(group&15u);
#pragma clang loop unroll(full)
    for(uint32_t k=0;k<4u;k++) {
        uint32_t v=z63g_ld32(a);
        if((v&bit)||z63g_cas32(a,v,v|bit))return 1;
    }
    return 0;
}
static inline uint32_t z63g_raised(uint64_t p,uint32_t group) {
    return (z63g_ld32(p+Z63_RAISE_OFF)>>(group&15u))&1u;
}
#endif
