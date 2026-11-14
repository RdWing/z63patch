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
/* G10 open gate on the arm-time gate: the interface every part of the group codes against.
 * Owner: part 10a (foundation). Stable from 10a on; a change here is a 10a change.
 *
 * ONE group (id 10), ONE translation unit: native/arm_time/groups/open_gate.c, which defines the
 * helpers below and then #includes each part's source if it exists (`__has_include`), so parts
 * are added by adding their own files only:
 *
 *   part  python (z63patch/gate_groups/)  C (native/arm_time/groups/)   content
 *   10a   open_gate.py (GROUP, switches)  open_gate.h, open_gate.c      state, og_size/og_on, og_sync,
 *         open_gate_geom.py              open_gate_geom.c              geometry records (plan post-fix,
 *                                                                       size-function value sites, leaf
 *                                                                       and classifier call-site
 *                                                                       post-fixes), budget decision
 *                                                                       + cap (site nraw_budget), rate
 *                                                                       row (site nraw_copy), sanitise
 *                                                                       list, restore z63r_g10
 *   ENT   open_gate_entries.py           open_gate_entries.c           frame-size page [FX 3:2] entries
 *                                                                      : page record swap,
 *                                                                       list/selection/summary, matcher,
 *                                                                       selection save (z63s_commit),
 *                                                                       private tables (derived asset)
 *   10b   open_gate_rec.py               open_gate_rec.c               container sites,
 *                                                                       NCTG/proxy descriptors (2.6),
 *                                                                       crop tag, proxy SPS, writers
 *   10c   open_gate_af.py                open_gate_af.c (+ .S)         AF area alias (2.8), AF area,
 *                                                                       zoom, lit points (photo
 *                                                                       layout), display, lv_display post
 *   10d   open_gate_play.py              open_gate_play.c              playback (2.11)
 *   10e   (no group file)                                              site registration requests, G7
 *                                                                       peer contract, core changes
 *
 * A part module defines (all optional): RECORDS, SHARED, STOCK_CALLS, ENTRIES, SANITISE, ASSETS,
 * DERIVED, TEXT_SLOTS, SAVES (bool). open_gate.py concatenates them in the order 10a, ENT, 10b,
 * 10c, 10d. A part uses its own set names (prefix: geom., ent., rec., af., play.).
 *
 * What a part may rely on (10a guarantees):
 *  - G10, Z63_STATE_G10 state layout below; og_* field offsets never move.
 *  - THE RULE (re-plan build): every G10 reader decides from the
 *    PLAN LATCH, never from the live choice and never from a live mode word. og_plan_kind(p): the
 *    latched kind (state +1; rows +4, width +6, active +0 written with it) while G10 is
 *    LIVE/DRAINING and either the readiness word 0xA048BB160 reads 1 (the earlier rule) or the
 *    plan-mode byte OG_MPLAN (+3) is set while movie live view is running (og_running_movie), else
 *    0. Latch and plan mode are
 *    written only where stock re-plans: (a) g10_replan at stock's live-view applier call
 *    0x40F681FC (raise, switch or clear, tables first, plan mode set: movie view), from the STORED
 *    word, and its lowering form (readiness not 1 while the plan mode is set: latch and plan mode
 *    to 0, then the tables); (b) the plan post-fix, which never lowers on a call the readers hold
 *    (per-frame rule: the plan function runs every frame on two CPUs): under the hold it
 *    re-applies the latch to the record and may set the plan mode; with no hold and no open-gate
 *    path (a photo plan) it clears the plan mode and puts the tables to stock; a foreign record
 *    field only marks OG_PLAN_MARK for (a); (c) the photo-view pre-set z63p_g10 (latch from the
 *    stored word, plan mode clear); (d) the restore and break callbacks (both to 0). Earlier
 *    the rule read 0xA048BB160 == 1 alone; that word is movie-RECORDING READINESS, which
 *    drops to 0 in running movie live view on a card pull (or a full or failed card) with no stock
 *    re-plan, so readers went stock against a live open-gate plan (analysis
 *    z63-movie-word-20261112). The plan mode now holds them on the plan in force through that drop
 *    while the pipeline keeps running, also in the DRAINING phase a card pull starts, and the plan
 *    post-fix applies under the same hold. Once the pipeline stops (menu, playback, lever to photo),
 *    the readers follow the readiness word as before, so a photo plan (made with the pipeline
 *    stopped and the word 0) never sees an open-gate value.
 *    The published/movie-entry word decides nothing in G10 geometry (G6's N-RAW tier field is still
 *    read from it, og_nraw_tier).
 *  - og_size(p) / og_on(p): the CHOICE from the movie-entry word (inside an ACT hold) and the
 *    stock slots; 0 in PASS. Kept for diagnostics; no reader decides from it.
 *  - og_kind_latched(p): the raw latch (state +1) while G10 is LIVE/DRAINING, else 0.
 *  - og_dims_open(w,h): received-geometry test (consumers decide on the dims they receive;
 *    a recording keeps its geometry whatever the setting does).
 *  - og_sync(p, kind) is 10a's: only 10a writes state +0..+0xB and the og_sync tables.
 *  - Parts never write the settings word except through z63s_commit (ENT only).
 * Rules for parts: every adapter takes z63g_page() first, z63g_phase(p, G10) before acting, and
 * in PASS does exactly the stock word's work and stores nothing outside its frame (README 5). */
#ifndef Z63_OPEN_GATE_H
#define Z63_OPEN_GATE_H
#include <stdint.h>

#define G10 10u

/* ---- state, 64 B at Z63_STATE_G10 (0x8D0) ---- */
#define OG_ACTIVE      0x00u  /* u8  10a: 1 iff the last plan post-fix found open gate on (read by G7 with z63g_phase(p,10)) */
#define OG_KIND        0x01u  /* u8  10a: 0 none, 6 = 6K (6060x4040 coded), 4 = 4K (4040x2692 coded) */
#define OG_SYNC_STATE  0x02u  /* u8  10a: kind the og_sync tables were last put to; | OG_SYNC_FOREIGN when a word held neither known value */
#define OG_MPLAN       0x03u  /* u8  10a: 1 iff the latch is in force (stock's last plan decision was a movie plan); G7 reads it */
#define OG_ROWS        0x04u  /* u16 10a: 4040 / 2692 (0 when off) */
#define OG_WIDTH       0x06u  /* u16 10a: 6060 / 4040 (0 when off) */
#define OG_LAST_TIER   0x08u  /* u32 10a: tier of the last budget decision at open-gate dims (Z63_TIER_NONE none) */
#define OG_LAST_INDEX  0x0Cu  /* u16 10a: budget factor index the last decision left (diagnostic) */
#define OG_REPLAN_LAST 0x0Eu  /* u8  re-plan: 0x80 | kind after the last g10_replan that acted, 0xC0 its lowering form, 0xA0 a lowering by the plan post-fix leaving a movie plan (diagnostic) */
#define OG_TWIN        0x0Fu  /* u8  re-plan, Option C: 0x80 | want | latch << 4 once forced (cleared at the applier) */
#define OG_PLAY_LATCH  0x10u  /* u8  10d */
#define OG_AF_ALIAS    0x11u  /* u8  10c: in-flight alias plans (diagnostic, 0 at disarm) */
#define OG_ENT_BASE    0x12u  /* 0x12..0x1F ENT and i-menu (14 B): +0x12 ENT's last save result */
#define OG_STALE       0x13u  /* u8  binding term seen stale while LIVE (open gate off for it), 0 none */
#define OG_SEEN        0x14u  /* u8  binding term last seen on while LIVE, 0 none */
#define OG_IM_START    0x15u  /* u8  i-menu: session start choice (bit 1 valid, bit 0 open gate) */
#define OG_IM_RESULT   0x16u  /* u8  i-menu: last save result (diagnostic) */
#define OG_IM_TERM     0x17u  /* u8  i-menu: the start record's binding term (rollback re-binds to it) */
#define OG_PLAN_MARK   0x18u  /* u8  per-frame rule: 0x80 | kind when a plan post-fix found a foreign record field; g10_replan lowers and clears it */
#define OG_DIAL        0x19u  /* u8  i-menu dial: 0x80 | position while one dial call is extended (| 0x40 previous), 0 none */
#define OG_DIAL_NEW    0x1Au  /* u8  i-menu dial: 0x80 | the position the search chose (0 none); cleared by the call's matcher */
#define OG_REC_BASE    0x20u  /* 0x20..0x2F 10b (16 B) */
#define OG_AF_BASE     0x30u  /* 0x30..0x37 10c (8 B) */
#define OG_PLAY_BASE   0x38u  /* 0x38..0x3F 10d (8 B) */
#define OG_SYNC_FOREIGN 0x80u

/* ---- stock words the decision reads (verified against the stock image by the 10a tests) ---- */
#define OG_SLOT_FILE_TYPE 0xa03f0eee0ull   /* = Z63_SLOT_FILE_TYPE; 6 is N-RAW */
/* Keys 0x05000F4E (N-RAW size) and 0x05000F50 (N-RAW rate): their SETTING_GET slots, proven with
 * tools/gen_setting_slots.py's equivalence method (67 cases each, 0 mismatches);
 * requested for setting_slots.h (G10-REQUESTS R-G10-3). Defined here until that lands. */
#ifndef Z63_SLOT_KEY_0F4E
#define Z63_SLOT_KEY_0F4E 0xa03f0f110ull
#endif
#ifndef Z63_SLOT_KEY_0F50
#define Z63_SLOT_KEY_0F50 0xa03f0f118ull
#endif
#define OG_MOVIE_WORD 0xa048bb160ull       /* movie-RECORDING readiness (1 can record): decision points; readers: word 1, or OG_MPLAN while running */
#define OG_PIPE_STATE 0xa03d42100ull       /* live-view pipeline state: 2 running */
#define OG_PIPE_CLASS 0xa03d42108ull       /* pipeline class (0x410538FC): movie path 6..0xB; no card input */
#define OG_MOVIE_CLASSES 0xfc0u            /* bits 6..11 */
/* Movie live view RUNNING: pipeline state 2 and a movie-path class. A card pull leaves both as they
 * are (emulator: 2/7 with the readiness word 0); a menu, playback or the
 * lever to photo stops the pipeline (state 0 or 3) before stock re-plans. */
static inline uint32_t og_running_movie(void) {
    uint32_t c=*(volatile const uint32_t *)(uintptr_t)OG_PIPE_CLASS;
    return *(volatile const uint32_t *)(uintptr_t)OG_PIPE_STATE==2u&&c<32u&&((OG_MOVIE_CLASSES>>c)&1u);
}
#define OG_STORED_WORD 0xa03f0eef4ull      /* = Z63S_WORD (save_api.h): the stored settings word */
#define OG_MOVIE_WORK  0xa03324ce0ull      /* = the core's MOVIE_WORK (recording) */
#define OG_MOVIE_OUT   0xa03324dc8ull      /* = the core's MOVIE_OUTSTANDING */

/* ---- budget facts (verified from stock code by the 10a tests) ----
 * 0x40E67D0C: bytes = factor[idx] * w * h / 80 (idx > 8: factor 0), rounded up to 512 for
 * request type 10 (down otherwise); factor table 0xA02B1453C = 10,20,30,40,50,60,70,15,25.
 * N-RAW ring (memory plan entry 0xA02B5FFD8.. : total 1,350,841,344 = 174 x 7,763,456). */
#define OG_FACTORS     0xa02b1453cull
#define OG_RING_SLOT   7763456u
#define OG_HIGH_INDEX_6K 8u                /* 2.5 bit/px: 7,650,816 B at 6060x4040 (fits) */
#define OG_NRAW_TYPE 10u                  /* request type of a movie N-RAW frame: the only type the cap changes */
#define OG_STILL_HE_TYPE 12u               /* request type of the C15/C30 still HE strip (6064x4040, format 5,
                                            * index 2 at 0x40E67A4C from 0x40E79BC0); not an N-RAW ring request */

/* Received-geometry test: the coded dims an open-gate recording carries. No stock N-RAW
 * dimension satisfies it (tests enumerate the stock N-RAW rate table and the size tables). */
static inline uint32_t og_dims_open(uint32_t w,uint32_t h) {
    return (h==4040u&&w>=4000u&&w<=6064u)||(w>=4032u&&w<=4040u&&h>=2688u&&h<=2692u);
}
static inline uint32_t og_dims_kind(uint32_t w,uint32_t h) {
    return og_dims_open(w,h)?(h==4040u?6u:4u):0u;
}

/* ---- the wanted-kind rule, shared: G10's own decision (og_chosen, og_want_rule,
 * og_stored_kind, z63p_g10) and G7's menu-close prediction (desqueeze.c g7_og_pass) use these, so
 * the prediction is the rule g10_replan then applies, not a copy. Pure: loads only (the binding's stale
 * marks are written by og_chosen in G10 alone). Needs settings_record.c (z63_settings_decode,
 * settings_crc) included before this header. `g` is G10's state (Z63_STATE_G10) or 0.
 * og_rule_chosen: 0 unless the record is v2/v3 with a good CRC and bit 20 is bound to its pair;
 * then bits 8..15 = the binding term, bit 1 = the record says open gate off (og_chosen's stale-mark
 * input), bit 0 = open gate chosen (on, and the term not marked stale). Packed in the return value
 * (no out-pointer, one CRC). g = 0 skips the stale test: G10's og_chosen passes 0 and applies
 * og_rule_stale itself after its own mark logic (keeps g10_replan's stack within the verifier's
 * 256 B bound: og_chosen's frame as before). */
static inline __attribute__((always_inline)) uint32_t og_rule_stale(const volatile uint8_t *g,uint32_t d) {
    return g&&g[0x13u]==d;                              /* OG_STALE */
}
static inline __attribute__((always_inline)) uint32_t og_rule_chosen(const volatile uint8_t *g,uint32_t word) {
    struct z63_feature_settings v;
    int kind=z63_settings_decode(word,&v);
    if(!(kind==Z63_RECORD_V2||kind==Z63_RECORD_V3))return 0;
    uint32_t d=((word>>24)^settings_crc((word>>8)&65535u))&255u;
    if(!d)return 0;                                     /* bit 20 clear, or an older plain bit 20 (before the binding) */
    if(!v.open_gate)return d<<8|2u;
    return d<<8|(uint32_t)!og_rule_stale(g,d);
}
/* The slots part: file type N-RAW (6), native N-RAW size 6 or 4, rate 6..8 -> the size, else 0. */
static inline __attribute__((always_inline)) uint32_t og_rule_slots(void) {
    if(*(volatile const uint32_t *)(uintptr_t)OG_SLOT_FILE_TYPE!=6u)return 0;
    uint32_t size=*(volatile const uint32_t *)(uintptr_t)Z63_SLOT_KEY_0F4E,rate=*(volatile const uint32_t *)(uintptr_t)Z63_SLOT_KEY_0F50;
    if(size!=6u&&size!=4u)return 0;
    return rate>=6u&&rate<=8u?size:0u;
}
/* The kind g10_replan latches in movie view (LIVE, readiness 1) for this stored word: 0 while a plan
 * post-fix marked a foreign record field (OG_PLAN_MARK), else the rule above. Pure (no marks). */
static inline __attribute__((always_inline)) uint32_t og_rule_replan_kind(const volatile uint8_t *g,uint32_t word) {
    if(g&&g[OG_PLAN_MARK])return 0;
    return og_rule_chosen(g,word)&1u?og_rule_slots():0u;
}

/* Defined in open_gate.c (one copy for every part). */
uint32_t og_size_word(uint64_t p,uint32_t published); /* 0, 4 or 6 from a record (binding, stale marks of p) */
uint32_t og_size(uint64_t p);                /* 0, 4 or 6; 0 in PASS */
uint32_t og_on(uint64_t p);                  /* og_size(p) != 0 */
uint32_t og_kind_latched(uint64_t p);        /* state +1 while G10 is LIVE/DRAINING, else 0 */
uint32_t og_plan_kind(uint64_t p);           /* THE reader rule: latch, LIVE/DRAINING, readiness 1 or (OG_MPLAN and running movie LV) */
uint32_t og_stored_kind(uint64_t p);         /* the wanted kind from the STORED word and the slots (g10_replan) */
void og_sync(uint64_t p,uint32_t kind);      /* 10a only: tables to kind (CAS between known values) */
uint32_t og_budget_cap(void *request);       /* the PASS-safe cap; returns the index it left */
#endif
