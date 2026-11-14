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
/* The one persistence path of the arm-time gate: the stock settings-save request 0x41003BC0, issued the way stock issues it, inside a TX0
 * (SAVE) transaction, with the result observed at the save worker's call 0x41003A7C (G2's
 * record) and the RAM word rolled back on any failure. Shared by G2, G6 and G10; header only, so
 * every group compiles its own copy and links nothing of another group.
 *
 * A group that includes this header:
 *   - includes gate_api.h first, and native/feature_settings.c (with Z63_SETTINGS_SHARED_V3) and
 *     native/nv_quality.c (the rollback tells a well-formed foreign record from ours);
 *   - declares stock_calls={'stock_save_request': 0x41003BC0} and the same in save_calls
 *     (gate_groups.SAVE_ENTRIES), requires=(2,) unless it is G2, install_needs_q=True and
 *     gate_groups.SAVE_TERM in its terms;
 *   - never calls the worker 0x41003538 (it reaches the flash writer); only G2's observer record
 *     replaces that one call and calls it in place of the stock BL.
 *
 * The save block lives in G2's feature state at fixed offsets (Z63_STATE_G2 + 0x30..0x44) and is
 * written only through these functions. No publication here: the core publishes the settings
 * word from the armed poll when Q holds and every TX and ACT count is 0. */
#ifndef Z63_SAVE_API_H
#define Z63_SAVE_API_H
#include <stdint.h>
#include "gate_api.h"

#define Z63S_WORD      0xa03f0eef4ull           /* settings word (item 0x0F0B storage) */
#define Z63S_INFLIGHT  (Z63_STATE_G2+0x30u)     /* generation of the save in flight, 0 = none */
#define Z63S_SEQUENCE  (Z63_STATE_G2+0x34u)     /* +1 for every observed worker result */
#define Z63S_RESULT    (Z63_STATE_G2+0x38u)     /* the worker's last observed result (1 = saved) */
#define Z63S_ERROR     (Z63_STATE_G2+0x3cu)     /* 0 none, 1 CFG error, 2 save failed */
#define Z63S_FATAL     (Z63_STATE_G2+0x40u)     /* 1: a rollback failed; every later save refuses */
#define Z63S_G2        2u
enum {Z63S_REFUSED=0,Z63S_SAVED=1,Z63S_FAILED=2,Z63S_SUPERSEDED=3,Z63S_FATAL_SET=4};

void stock_save_request(void);

/* Undo our RAM change after a failed save (native_rollback):
 * 1 restored; 2 superseded (a stock writer replaced our high bytes with a well-formed record);
 * 0 ownership uncertain (bytes still ours, or a foreign record), which is fatal. */
static inline uint32_t z63s_rollback(uint32_t ours,uint32_t prior) {
    if(z63g_replace_high(Z63S_WORD,ours,prior))return 1;
    uint32_t word=z63g_acq32(Z63S_WORD);
    if((word>>8)==(ours>>8))return 0;
    struct z63_feature_settings value;
    return z63_settings_decode(word,&value)!=Z63_RECORD_FOREIGN?2:0;
}

/* One save of `next` over `old` (both full words; the caller encoded `next` from `old` with the
 * shared decoder, and only bits 8-31 are replaced). Refused (nothing written) unless `group` and
 * G2 are LIVE and admitted to TX0 and no earlier rollback failed. */
static inline uint32_t z63s_commit(uint64_t p,uint32_t group,uint32_t old,uint32_t next) {
    if(!p||z63g_phase(p,Z63S_G2)!=Z63_LIVE||!z63g_tx_enter(p,0,group))return Z63S_REFUSED;
    uint32_t out=Z63S_REFUSED;
    volatile uint32_t *block=(volatile uint32_t *)(uintptr_t)(p+Z63S_INFLIGHT);
    if(!z63g_acq32(p+Z63S_FATAL)&&z63g_replace_high(Z63S_WORD,old,next)) {
        uint32_t sequence=z63g_acq32(p+Z63S_SEQUENCE),ok=0;
        if(sequence!=0xffffffffu) {
            __atomic_store_n(&block[0],sequence+1,__ATOMIC_RELEASE);
            stock_save_request();
            __atomic_store_n(&block[0],0u,__ATOMIC_RELEASE);
            ok=z63g_acq32(p+Z63S_SEQUENCE)==sequence+1&&z63g_acq32(p+Z63S_RESULT)==1&&
               (z63g_acq32(Z63S_WORD)>>8)==(next>>8);
        }
        if(ok) {block[3]=0;out=Z63S_SAVED;}
        else {
            uint32_t r=z63s_rollback(next,old);
            block[3]=2;
            if(!r) {__atomic_store_n(&block[4],1u,__ATOMIC_RELEASE);out=Z63S_FATAL_SET;}
            else out=r==2?Z63S_SUPERSEDED:Z63S_FAILED;
        }
    }
    z63g_tx_leave(p,0);
    return out;
}

/* G2's observer (record 0x41003A7C, replacing "bl 0x41003538"): the generation before the
 * worker call, then the result after it. Any other phase or a stale generation stores nothing. */
static inline uint32_t z63s_observe_begin(uint64_t p) {return p?z63g_acq32(p+Z63S_INFLIGHT):0;}
static inline void z63s_observe_end(uint64_t p,uint32_t generation,uint32_t result) {
    if(!p||!generation||z63g_acq32(p+Z63S_INFLIGHT)!=generation)return;
    volatile uint32_t *block=(volatile uint32_t *)(uintptr_t)(p+Z63S_INFLIGHT);
    block[2]=result;
    __atomic_store_n(&block[1],block[1]+1u,__ATOMIC_RELEASE);
}
#endif
