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
/* C2.01 arm-time safety gate, composed stages (S7 on): the gate core that feature groups are
 * added to. Derived from native/arm_time/s6.c, which stays frozen so that S6 keeps its bytes.
 *
 * Built only by z63patch/gate.py, which generates gate_build.h (the composition: group mask,
 * record count, dependencies, quiescence terms, shared-site contributors) and gate_sites.inc
 * (the merged adapters of the shared sites in the composition). Groups are declared in
 * z63patch/gate_groups. Lifecycle as S6; new against S6:
 *
 *  - records of any group in the build are accepted by generic shape rules (rec_ok); the
 *    independent verifier (z63patch/verify_gate.py) checks every record against its group's
 *    declaration and the stock image;
 *  - all-or-none sets (record flag bit 1): contiguous in the table, one group, restore only
 *    under Q (with the group's terms), at most Z63_BATCH members. Installed only after every
 *    member was checked; a write failure puts back what the set had written and fails the
 *    arming (everything stock). Restored only when every member is mapped and holds either its
 *    installed or its stock value, all in one poll; a member that does not take its stock value
 *    makes the pass put back what it restored. A set therefore ends fully stock, or as found
 *    with its wrappers passing through (RESTORING/STOCK, or no page);
 *  - the arm file may name any group of the build (bit 0 required); a selected group whose
 *    dependencies are not all selected, or whose install quiescence does not hold at the
 *    arming poll, is not installed (gate block +0x6C requested, +0x70 dropped);
 *  - per-group quiescence terms (data: address, width, mask, value, equal/differ) beyond Q,
 *    at installation and for every restore-under-Q record of that group;
 *  - wrappers use native/arm_time/gate_api.h (header only, shared with the groups);
 *  - update protection: the arming
 *    refuses (STOCK for this power-on, reason R_UPDATE) while an update has started (updating());
 *    two pinned deny records (record flag bit 2, class X, the last two of the table) put
 *    `b z63_update_denied` on the two tail branches every update start passes. They are
 *    installed first, checked by the armed integrity check, and never restored (not by a fail,
 *    a restore pass, the stale restore after a wake or at STOCK): STOCK means every record word
 *    is stock except these two. Only a power-switch start (text copied again) removes them.
 *  - standby wake: a standby wake restarts
 *    MAIN with the RAM text kept (hook, anchor, every installed code record and the two deny
 *    words) while the data image is copied again and the state page is gone. An ARMED session
 *    that finds start-up's copy-skip flag set, P stock and no data record at a patch value enters
 *    WAKE: it only reads (and writes its anchor) until stock has finished its own start-up (every
 *    data record stock, the card settled, photo live view quiet for WAKE_SETTLE polls), then in
 *    one poll re-reads the arm file (same mask), checks the kept code against the armed mask,
 *    takes a fresh page and installs the data records only ("warm arming"). Any doubt: the stale
 *    restore (STOCK except the deny words until the power switch). Card-detect code that only
 *    arming uses lives in its own section .text.z63card, linked at 0x401A5000..0x401A6000.
 *  - movie-view arming:
 *    cold and warm arming accept QM (qm(): idle movie view, pipeline 2 with a class of
 *    Z63_QM_CLASSES, the record button clear, with view()'s common terms, among them the GUI
 *    shooting screen with the i-menu closed: gui_state()) as
 *    well as settled photo live view, held for the whole settle (a view change restarts it) and
 *    re-checked last before any write. At a QM arming the groups that may not install there
 *    (Z63_QM_OK: a code record that replaces a non-branch word without evidence) and their
 *    dependants are deferred (page +0xC3C); stage 2 installs them from the armed LIVE poll once
 *    settled photo live view (Q) and their terms have held for SETTLE_POLLS polls (stage2()).
 *    The warm arming after a wake writes data records only and carries the deferred groups over.
 *    Publication callbacks run once at a QM arming when the anchor hold is free.
 *
 * Timing is in poll counts (no time floor) and the counter-time arming window inherited from S3-S6; no poll
 * period is assumed. Fixed state-page map, whatever the composition: +0x000 rows copy (room for
 * 48 x 44), +0x840 feature state (368 B, allocated per group by the builder), +0x9B0 counters,
 * +0x9F0..+0xC38 reserved (text-slot originals), +0xC38 stage-2 settle (groups << 16 | polls),
 * +0xC3C groups deferred to stage 2, +0xC40 core words (16 B, gate_api.h), +0xC60 record statuses
 * (800), +0xF80 gate block. No writable static data; no lock of our own; no interrupt masking
 * in our own code; every exclusive loop bounded; no flash call; no card or flash write; x18
 * never used (-ffixed-x18). Code ends below 0x401A5000, the card section below 0x401A6000
 * (checked by builder and verifier). */
#include <stdint.h>
#include "gate_api.h"
#include "gate_build.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;
#define R8(a) (*(volatile const u8 *)(uintptr_t)(a))
#define R16(a) (*(volatile const u16 *)(uintptr_t)(a))
#define R32(a) (*(volatile const u32 *)(uintptr_t)(a))
#define R64(a) (*(volatile const u64 *)(uintptr_t)(a))
#define W8(a,v) (*(volatile u8 *)(uintptr_t)(a)=(u8)(v))
#define W32(a,v) (*(volatile u32 *)(uintptr_t)(a)=(u32)(v))
#define W64(a,v) (*(volatile u64 *)(uintptr_t)(a)=(u64)(v))
#define NOINLINE __attribute__((noinline))   /* size: the core must end below 0x401A5000 */
/* Card-detect code used only by arming (cold and warm): linked in its own slot (z63patch/gate.py
 * CARD), never reached from a wrapper, a site adapter or the API (verifier). */
#define CARD __attribute__((section(".text.z63card"),noinline))
#define CARDI __attribute__((section(".text.z63card")))   /* the same section where not inlined */

#if !defined(Z63_CARD_PRESENT) || !defined(Z63_TABLE) || !defined(Z63_GROUPS) || !defined(Z63_RECORDS) || \
    !defined(Z63_DEPS) || !defined(Z63_QNEED) || !defined(Z63_QTERM_COUNT) || !defined(Z63_QM_OK) || \
    !defined(Z63_NODEFER) || !defined(Z63_QM_CLASSES) || !defined(Z63_QM_IMENU) || !defined(Z63_QM_SCREEN)
#error "gate.c is built only by z63patch/gate.py"
#endif
#if !(Z63_GROUPS&1u) || (Z63_GROUPS>>16) || (Z63_QNEED&~Z63_GROUPS) || !(Z63_QM_OK&1u) || (Z63_QM_OK&~Z63_GROUPS) || \
    (Z63_NODEFER&~Z63_GROUPS)
#error "group mask"
#endif
/* Stage 2 (movie-view arming): compiled when a composed group may be deferred at a QM arming. */
#define STAGE2 ((Z63_GROUPS&~Z63_QM_OK&0xfffeu)!=0)
#ifndef Z63_BATCH
#define Z63_BATCH 64u     /* Records per poll (a test composition may lower it) */
#endif
/* Optional parts, compiled only when the composition needs them (so a composition without them
 * keeps its core bytes): u16 value records, the video-menu row template, the sanitiser. */
#ifndef Z63_DATA16
#define Z63_DATA16 0
#endif
#ifndef Z63_ROWS_COUNT
#define Z63_ROWS_COUNT 0
#endif
#ifndef Z63_SAN_COUNT
#define Z63_SAN_COUNT 0
#endif
#ifndef Z63_MOVIE_HOLD
#define Z63_MOVIE_HOLD 0xfffeu
#endif
#ifndef Z63_RESTORE_COUNT
#define Z63_RESTORE_COUNT 0
#endif
#ifndef Z63_HOLD_COUNT
#define Z63_HOLD_COUNT 0
#endif
#ifndef Z63_ENTRY
#define Z63_ENTRY 0
#endif
#ifndef Z63_BREAKCB_COUNT
#define Z63_BREAKCB_COUNT 0
#endif
#ifndef Z63_RAISE
#define Z63_RAISE 0
#endif
#define Z63_PUBLISHING (Z63_HOLD_COUNT||Z63_PUBCB_COUNT||Z63_ENTRY)
#ifndef Z63_PUBCB_COUNT
#define Z63_PUBCB_COUNT 0
#endif

/* Stock C2.01 addresses (each checked against the image by the builder). */
#define POLL              0x403277c0ull
#define HOOK              0x4032768cull
#define GUARD             0x40194f00ull
#define ANCHOR            0x40194ff0ull
#define RANGE_LO          0x401a0000ull
#define HEADER            0x401fff00ull
#define DETECT            0xa0305daa0ull /* +0 enable; slot record 0 at +4: +1 sample, +8 event sent */
#define CAPTURE_WORK      0xa048bb044ull
#define MOVIE_WORK        0xa03324ce0ull
#define MOVIE_OUTSTANDING 0xa03324dc8ull
#define PIPELINE_STATE    0xa03d42100ull
#define PIPELINE_CLASS    0xa03d42108ull
/* Stills work the movie-only CAPTURE_WORK does not show. Both
 * BSS, 0 from power-on; 0 in the emulator in idle photo view in every release mode the dial reaches
 * (S, CL, CH, CH+, C15, C30, C60, C120, self-timer), in idle movie view (class 7) and across a
 * single shot; static: the absolute-address sweep (a lower bound) finds no writer but the sites
 * below. busy = nonzero: a positive form, so a
 * value we have not seen can only delay arming, publication and disarm, never allow them.
 *  STILLS_HISTORY: pre-release history count; +1 at 0x40F5B42C per frame entering history (frame
 *   state +0x44 == 1), -1 at 0x40F5BA18 per frame submitted by the drain and at 0x40F67314 per frame
 *   discarded. Nonzero = frames held for a pre-release (C15..C120) capture.
 *  STILLS_WRITER: the still writer's handoff slot (u64 job pointer); stored at 0x40E92A80 with event
 *   0xA03D43D30 when a job is handed to the writer step 0x40E83A28, cleared only at 0x40E92B20 once
 *   the writer step has signalled completion (every other path of 0x40E92AA4 returns "retry" with
 *   the slot kept). Nonzero = a stills card write in progress. */
#define STILLS_HISTORY    0xa03d8109cull
#define STILLS_WRITER     0xa03d47ac8ull
/* Movie-recording READINESS (0 cannot record, 1 can; the result word of 0x4075969C): 0 in photo
 * view, and also in movie view with no card, a full or failed card, or N-RAW / ProRes RAW on a card
 * that is not exFAT (analysis z63-movie-word-20261112). Not a movie-view signal: only quiet() reads
 * it, where it is redundant (photo view always reads 0). */
#define MOVIE_VIEW        0xa048bb160ull
/* QM (movie-view arming): z63patch/gate_groups
 * QM_CLASSES (Z63_QM_CLASSES), SWITCHES / RECORD_BUTTON. */
/* The GUI app controller (z63patch/gate_groups GUI_*; switches IMENU_CLOSED -> Z63_QM_IMENU and
 * SHOOTING_SCREEN -> Z63_QM_SCREEN). Stock reaches the object only by the literal of getter
 * 0x40D59224; its init 0x40D5A610 stores the self pointer (+0x660, 0x40D5A694) and registers the
 * handler 0x40D5A6B0 that writes the current app id (+0x6C8, 0x40D5A950: 9 shooting screen, 0xD
 * i-menu, 5 menu, 6 playback); hold bit 2 of +0x680 is set while the posted id is 0xD or 0x11
 * (0x40D5D444, cleared 0x40D5B370). The builder and the verifier pin those stock words. */
#define GUI_OBJECT        0xa0282e8c8ull
#define GUI_SELF          0xa0282ef28ull /* u64 +0x660: GUI_OBJECT once initialised */
#define GUI_HOLD          0xa0282ef48ull /* u32 +0x680: bit 2 i-menu (0xD / 0x11) hold */
#define GUI_APP           0xa0282ef90ull /* u32 +0x6C8: the current app id */
#define GUI_SHOOTING      9u
#define GUI_IMENU         0xdu
#define SWITCHES          0xa0336bdb0ull /* switch snapshot; bit 0x2000 the record button (SW1) */
#define RECORD_BUTTON     0x2000u
/* Update signals (stock C2.01, all BSS, 0 at idle):
 *  UPDATE_CLIENT, PINS_CLOSED: nonzero once an update has started; never cleared.
 *  UPDATER_BUSY bit 0x400: also set and cleared by ordinary update checks, so it is treated as
 *    activity in settled() (arming waits), not as an update. */
#define UPDATE_CLIENT     0xa04073c70ull
#define PINS_CLOSED       0xa02fb47f8ull
#define UPDATER_BUSY      0xa0305521cull
#define UPDATE_DENIED     2              /* z63_update_denied's status: see z63_update_denied */
#define SETTINGS_WORD     0xa03f0eef4ull /* item 0x0F0B storage: read only, never written */
/* Start-up's copy-skip decision (u32, .bss, stored once per start by 0x40D70FAC from 0xA035D9F00):
 * 1 when the W1-W3 text copies were skipped (standby wake, read 1 on a camera), 0 after a
 * power-switch start (read 0). Only read. */
#define WAKE_FLAG         0xa035d9fb8ull
#define ITEMS_POINTER     Z63_P
#define ITEMS_STOCK       0x43cb3868ull
#define ITEMS_COUNT       0xa028cae30ull
#define ROWS              45u
#define ROW_BYTES         44u
#define FEATURE_OFF       0x840u
#define STATE_OFF         Z63_STATE_OFF
#define STATE_MAGIC       0x3650365au    /* "Z6P6" */
#define HEAP_LOCK         0xa04ee7240ull
#define HEAP_FREE_HEAD    0xa04ee71e8ull
#define HEAP_USED_HEAD    0xa04ee71f0ull
#define HEAP_GUARD_FLAG   0xa04ee7200ull
#define HEAP_NODES        0xa04ee7258ull
#define TEXT_LO           0x40200000ull
#define TEXT_HI           0x41800000ull
#define DATA_LO           0xa02808000ull
#define DATA_HI           0xa05000000ull
#define ROOT              0xa04f10000ull
#define TABLES_END        0xa04fd0000ull
#define RAM_LO            0xa00000000ull
#define RAM_HI            0xa40000000ull
#define PAGE_HI           Z63_PAGE_HI
#define ALIAS_DELTA       0x400000000ull
#define ALIAS_L1          56u
#define OA_MASK           0x0000fffffffff000ull

/* Cold arming: no time floor (guard v2 has no counter test, step() none).
 * The arming file is read after COLD_SETTLE consecutive polls of cold readiness (cold_ready():
 * stock's start-up registrations done, every data record and P stock, settled photo live view or QM,
 * card present, no update signal); an arm-file read that fails is retried after a new settle,
 * COLD_FILE_TRIES reads in all (the volume may still be mounting: mount is not ordered against
 * these predicates), then STOCK. A never-armed session puts the hook back to the stock call when
 * the arming window closes (window_closed()). */
#define SETTLE_POLLS      16u
#define COLD_FILE_TRIES   3u
/* The arming window (window_closed()). */
#define WINDOW_END_SECONDS 136u
#define WINDOW_HARD_SECONDS 7800u        /* 130 min: the window ends whatever delays it */

/* Lifecycle constants, in polls. */
#define QUIET_POLLS       20u
#define STUCK_POLLS       200u
#define CARD_OUT_POLLS    4u
#define RETRY_EVERY       20u
#define RETRY_MAX         600u
#define BATCH             Z63_BATCH
/* Standby wake, in polls: ready polls before the warm arming poll; polls of Q without
 * readiness before giving up (as STUCK_POLLS); file-read or page retries. */
#define WAKE_SETTLE       8u
#define WAKE_GIVEUP       200u
#define WAKE_RETRIES      3u
#define WAKE_TAG          (1ull<<63)     /* anchor +8 in WAKE: counters, never a page (page_like fails) */
#define WAKE_REFUSED      0x57410000u    /* anchor +8 of a wake refused before a page: "WA" | reason */

/* Gate block at page + 0xF80 (1.1c). */
#define GATE_OFF   Z63_GATE_OFF
#define GMAGIC     Z63_GMAGIC
#define G_MAGIC    0x00u
#define G_WORD     0x04u
#define G_SELF     0x08u
#define G_BUILD    0x10u
#define G_TFLIP    0x18u
#define G_QUIET    0x20u
#define G_CURSOR   0x24u
#define G_RETRY    0x28u
#define G_FAULT    0x2cu
#define G_CARDOUT  0x30u
#define G_OUTCOME  0x34u
#define G_PUBBUSY  0x38u
#define G_EFFECT   0x3cu
#define G_TX       0x40u
#define G_ACT      0x50u
#define G_CONT     0x60u
#define G_PUBS     0x64u
#define G_STUCK    0x68u
#define G_REQUESTED 0x6cu /* the arm file's group mask */
#define G_DROPPED  0x70u  /* requested groups not installed (dependency or quiescence) */
#define G_PUBIDLE  0x74u  /* Consecutive polls of native idle (saturates at PUB_SETTLE) */
#define G_RESTORECB 0x78u /* restore callbacks of the last own pass: bit g pending, bit 16+g left */
#define G_ACTWAIT  0x7cu  /* RESTORING: polls waited for the ACT counts to drain (to STUCK_POLLS) */
/* Movie-view arming, in the page's reserved area: the stage-2 settle (bits 0-15 the count, bits
 * 16-31 the groups it counts for) and the groups deferred to stage 2. */
#define S2_OFF     0xc38u
#define DEFER_OFF  0xc3cu
#define STATUS_OFF 0xc60u
#define STATUS_MAX 800u
#define REC_BYTES  40u
#define TABLE_END  0x401ffc00ull
_Static_assert(Z63_RECORDS<=STATUS_MAX,"status area");
_Static_assert(Z63_TABLE+REC_BYTES*Z63_RECORDS<=TABLE_END,"install table");
_Static_assert(FEATURE_OFF==48u*44u&&FEATURE_OFF+368u==STATE_OFF&&STATE_OFF+64u<=0x9f0u&&STATUS_OFF+STATUS_MAX<=GATE_OFF,"page map");
_Static_assert(Z63_BATCH>=1u&&Z63_BATCH<=64u,"batch");

#define P_CLOSED Z63_CLOSED
#define P_LIVE Z63_LIVE
#define P_DRAINING Z63_DRAINING
#define P_RESTORING Z63_RESTORING
#define P_STOCK Z63_STOCK
#define F_OVERFLOW Z63_F_OVERFLOW
#define F_UNDERFLOW Z63_F_UNDERFLOW
#define F_LEAK Z63_F_LEAK
#define F_RACE Z63_F_RACE
enum {ST_INSTALLED=0,ST_RESTORED=1,ST_ALREADY=2,ST_FOREIGN=3,ST_MAPFAIL=4,ST_PINNED=5,
      ST_UNSELECTED=6,ST_VERIFY=7};
enum {S_NONE=0,S_OBSERVE=1,S_READING=2,S_VERIFY=3,S_INSTALL=4,S_ARMED=5,S_STOCK=6,
      S_DRAINING=7,S_RESTORING=8,S_PARTIAL=9,S_WAKE=10};
enum {R_NONE=0,R_WINDOW=1,R_FILE=2,R_CONTENT=3,R_HEADER=4,R_CRC=5,R_TABLE=6,R_SITE=7,
      R_MAPPING=8,R_ACTIVITY=9,R_INSTALL=10,R_STALE=11,R_ANCHOR=12,R_PAGE=13,R_CARD=14,
      R_SELF=15,R_HOOK=16,R_FAULT=17,R_STATE=18,R_UPDATE=19};
enum {K_DATA=1,K_CODE=2,K_PAGE=3,K_DATA16=4};
enum {W_NONE=0,W_A=1,W_C=2,W_F=3,W_S=4,W_X=5};
#define RF_NEEDQ  1u
#define RF_ATOMIC 2u
#define RF_PIN    4u      /* Pinned deny record: installed first, never restored */
#define PINNED    2u      /* the last two records of every composed table */
#define ANCHOR_MAGIC 0x5a41u
#define A_FINISHED 0x80000000u

struct s5 {u32 magic,version;u64 self,build;u32 polls,groups;u32 enter[4],leave[4];};
_Static_assert(sizeof(struct s5)==64u,"counters");

u64 z63_entry(void);
u64 z63_armed(void);
u64 z63_enter(u32 index);
void z63_leave(u64 page,u32 index);
u64 z63_admit(u32 c,u32 group);
void z63_exit(u64 page,u32 c);
u64 z63_effective(u64 page);
u64 stock_poll(void);
int stock_fs_open(const char *path,u32 mode,u32 flags);
int stock_fs_read(int fd,void *buffer,u32 bytes,u32 *count);
int stock_fs_close(int fd);
int stock_heap_trylock(u64 lock);
int stock_heap_unlock(u64 lock);
int stock_heap_alloc(u32 bytes,void **out);
u64 stock_video_refresh(void *window);
void z63_capture_dispatch(void);
void z63_movie_dispatch(void);
void z63_pipeline_drain(void);
void z63_pipeline_configure(void);

/* ---- thunks: the S5 shape, word for word (F class, 1.3) ---- */
__asm__(
".text\n"
".macro Z63_THUNK name,index,lo,hi\n"
".p2align 2\n"
".global \\name\n"
".type \\name,%function\n"
"\\name:\n"
"  stp x29,x30,[sp,#-112]!\n"
"  mov x29,sp\n"
"  stp x0,x1,[sp,#16]\n"
"  stp x2,x3,[sp,#32]\n"
"  stp x4,x5,[sp,#48]\n"
"  stp x6,x7,[sp,#64]\n"
"  str x8,[sp,#80]\n"
"  mov w0,#\\index\n"
"  bl z63_enter\n"
"  str x0,[sp,#88]\n"
"  ldp x0,x1,[sp,#16]\n"
"  ldp x2,x3,[sp,#32]\n"
"  ldp x4,x5,[sp,#48]\n"
"  ldp x6,x7,[sp,#64]\n"
"  ldr x8,[sp,#80]\n"
"  movz x16,#\\lo\n"
"  movk x16,#\\hi,lsl #16\n"
"  blr x16\n"
"  ldr x9,[sp,#88]\n"
"  cbz x9,1f\n"
"  stp x0,x1,[sp,#16]\n"
"  mov x0,x9\n"
"  mov w1,#\\index\n"
"  bl z63_leave\n"
"  ldp x0,x1,[sp,#16]\n"
"1:ldp x29,x30,[sp],#112\n"
"  ret\n"
".size \\name,.-\\name\n"
".endm\n"
".macro Z63_STOCK name,lo,hi\n"
".p2align 2\n"
".global \\name\n"
".type \\name,%function\n"
"\\name:\n"
"  stp x29,x30,[sp,#-16]!\n"
"  mov x29,sp\n"
"  movz x16,#\\lo\n"
"  movk x16,#\\hi,lsl #16\n"
"  blr x16\n"
"  ldp x29,x30,[sp],#16\n"
"  ret\n"
".size \\name,.-\\name\n"
".endm\n"
"Z63_STOCK stock_poll,0x77c0,0x4032\n"
"Z63_STOCK stock_fs_open,0x3674,0x402e\n"
"Z63_STOCK stock_fs_read,0x3774,0x402e\n"
"Z63_STOCK stock_fs_close,0x36d4,0x402e\n"
"Z63_STOCK stock_heap_alloc,0x0d08,0x4017\n"
"Z63_STOCK stock_heap_trylock,0x2f0c,0x4017\n"
"Z63_STOCK stock_heap_unlock,0x2f10,0x4017\n"
"Z63_THUNK z63_capture_dispatch,0,0x5a54,0x4075\n"
"Z63_THUNK z63_movie_dispatch,1,0x2770,0x40d2\n"
"Z63_THUNK z63_pipeline_drain,2,0x0c74,0x40e8\n"
"Z63_THUNK z63_pipeline_configure,3,0xfd08,0x40e7\n"
);
/* The target of the two pinned deny words. Both are tail branches, so `ret` returns to the
 * caller with status UPDATE_DENIED (2), which stock firmware treats as a failed update start;
 * 0 and 1 must never be returned. Reads nothing (no page, no anchor, no stack): it works with
 * the patch STOCK, after a wake with the page gone, or with the rest of the core never reached.
 * Executes in place from the patch range (flash): only an update could change those bytes while
 * the RAM text holds the deny words, and the deny words refuse it. */
#define Z63_STR2(x) #x
#define Z63_STR(x) Z63_STR2(x)
__asm__(
".text\n.p2align 2\n.global z63_update_denied\n.type z63_update_denied,%function\n"
"z63_update_denied:\n  mov w0,#" Z63_STR(UPDATE_DENIED) "\n  ret\n.size z63_update_denied,.-z63_update_denied\n");
void z63_update_denied(void);
static void x_count8(volatile u32 *p) {            /* +1 without limit (diagnostic totals), 8 attempts */
    u32 v,s,t;
    __asm__ volatile(
        "mov %w[t],#8\n"
        "1: ldxr %w[v],[%[p]]\n"
        "add %w[v],%w[v],#1\n"
        "stxr %w[s],%w[v],[%[p]]\n"
        "cbz %w[s],2f\n"
        "subs %w[t],%w[t],#1\n"
        "b.ne 1b\n"
        "2:\n"
        :[v]"=&r"(v),[s]"=&r"(s),[t]"=&r"(t):[p]"r"(p):"memory","cc");
}
static u32 x_cas32(u64 a,u32 old,u32 new) {        /* 1 when stored */
    u32 v,s,t,r;
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
        :[v]"=&r"(v),[s]"=&r"(s),[t]"=&r"(t),[r]"=&r"(r):[a]"r"(a),[o]"r"(old),[n]"r"(new):"memory","cc");
    return r;
}
static u32 x_cas64(u64 a,u64 old,u64 new) {
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
        :[v]"=&r"(v),[s]"=&r"(s),[t]"=&r"(t),[r]"=&r"(r):[a]"r"(a),[o]"r"(old),[n]"r"(new):"memory","cc");
    return r;
}
#if Z63_DATA16
static u32 x_cas16(u64 a,u32 old,u32 new) {
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
        :[v]"=&r"(v),[s]"=&r"(s),[t]"=&r"(t),[r]"=&r"(r):[a]"r"(a),[o]"r"(old&0xffffu),[n]"r"(new&0xffffu):"memory","cc");
    return r;
}
#endif
static inline void dmb(void){__asm__ volatile("dmb ish":::"memory");}
static inline void dsb(void){__asm__ volatile("dsb ish":::"memory");}
static inline u32 ld_acq32(u64 a){return __atomic_load_n((const u32 *)(uintptr_t)a,__ATOMIC_ACQUIRE);}
static inline void st_rel32(u64 a,u32 v){__atomic_store_n((u32 *)(uintptr_t)a,v,__ATOMIC_RELEASE);}
#define fault z63g_fault
#define gate_page z63g_page
#define gate_word z63g_word
#define PHASE(g)  ((g)&15u)
#define REASON(g) (((g)>>8)&255u)
#define BROKEN(g) ((g)>>16)

/* F class (1.3): count ACT around the stock call in LIVE and DRAINING; anything else, or the
 * infrastructure group broken, passes through with nothing counted and nothing stored. */
u64 z63_enter(u32 index) {
    u64 p=gate_page();
    if(!p)return 0;
    u32 g=gate_word(p),ph=PHASE(g);
    if((ph!=P_LIVE&&ph!=P_DRAINING)||(BROKEN(g)&1u))return 0;
    u32 r=z63g_inc8((volatile u32 *)(uintptr_t)(p+GATE_OFF+G_ACT+4*(index&3u)));
    if(r) {if(r==1)fault(p,F_OVERFLOW);return 0;}
    dmb();
    x_count8((volatile u32 *)(uintptr_t)(p+STATE_OFF+32+4*(index&3u)));
    return p;
}
void z63_leave(u64 p,u32 index) {
    x_count8((volatile u32 *)(uintptr_t)(p+STATE_OFF+48+4*(index&3u)));
    u32 r=z63g_dec64((volatile u32 *)(uintptr_t)(p+GATE_OFF+G_ACT+4*(index&3u)));
    if(r)fault(p,r==1?F_UNDERFLOW:F_LEAK);
}

/* A class admission (1.6a): the TX API (no client in this build). */
u64 z63_admit(u32 c,u32 group) {
    u64 p=gate_page();
    if(!p)return 0;
    u32 g=gate_word(p);
    if(PHASE(g)!=P_LIVE||group>=16u||((BROKEN(g)>>group)&1u))return 0;
    u32 r=z63g_inc8((volatile u32 *)(uintptr_t)(p+GATE_OFF+G_TX+4*(c&3u)));
    if(r) {if(r==1)fault(p,F_OVERFLOW);return 0;}
    dmb();
    g=gate_word(p);
    if(PHASE(g)!=P_LIVE||((BROKEN(g)>>group)&1u)) {z63_exit(p,c);return 0;}
    return p;
}
void z63_exit(u64 p,u32 c) {
    u32 r=z63g_dec64((volatile u32 *)(uintptr_t)(p+GATE_OFF+G_TX+4*(c&3u)));
    if(r)fault(p,r==1?F_UNDERFLOW:F_LEAK);
}
u64 z63_effective(u64 p){return z63g_effective(p);}

/* The merged adapters of the shared sites in this composition (generated list). Every adapter
 * does its own acquire load of P first (inline z63g_page); the per-contributor phase and the
 * after-call recheck are these two out-of-line helpers, so the adapters stay small enough for all
 * six sites to fit below 0x401A5000. */
NOINLINE __attribute__((unused)) static u32 site_phase(u64 p,u32 g){return p?z63g_phase(p,g):0;}
NOINLINE __attribute__((unused)) static u32 site_same(u64 p,u32 g,u32 ph){return ph&&gate_page()==p&&z63g_phase(p,g)==ph;}
#include "gate_sites.inc"

/* ---- card-detect task only from here on ---- */
/* Size first for the card-detect code (minsize, as -Oz; the machine outliner stays off with the
 * core's -mno-outline): the wrappers, the API and the site adapters above keep -Os. */
#pragma clang attribute push(__attribute__((minsize)),apply_to=function)
static u64 counter(void){u64 v;__asm__ volatile("isb\n mrs %0,cntpct_el0":"=r"(v)::"memory");return v;}
static u64 frequency(void){u64 v;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(v));return v;}
static u32 anchor_state(void){
    u64 w=R64(ANCHOR);
    return (u16)w==ANCHOR_MAGIC?(u32)(w>>16)&255u:S_NONE;
}

/* ---- page tables (as S5) ---- */
struct walk {u64 desc;u32 level,count;u64 tables[3];};
struct map {u64 hook_alias,anchor_alias;};
static int walk(u64 va,struct walk *w) {
    u64 t=ROOT;w->count=0;w->desc=0;w->level=0;
    if(va>>39)return 0;
    for(u32 l=1;l<=3;l++) {
        w->tables[w->count++]=t;
        u64 d=R64(t+((va>>(39-9*l))&511u)*8);
        if(!(d&1))return 0;
        if(l==3) {if((d&3)!=3)return 0;w->desc=d;w->level=3;return 1;}
        if((d&3)==1) {w->desc=d;w->level=l;return 1;}
        if(d&(15ull<<59))return 0;
        t=d&OA_MASK;
        if(t<ROOT||t>=TABLES_END)return 0;
    }
    return 0;
}
static u64 leaf_pa(const struct walk *w,u64 va) {
    u32 bits=w->level==1?30u:w->level==2?21u:12u;u64 mask=(1ull<<bits)-1;
    return (w->desc&OA_MASK&~mask)|(va&mask);
}
#define ATTR(d) (((d)>>2)&7u)
#define AP(d)   (((d)>>6)&3u)
#define SH(d)   (((d)>>8)&3u)
#define AF(d)   (((d)>>10)&1u)
#define PXN(d)  (((d)>>53)&1u)
static int code_leaf(u64 d){return ATTR(d)==3&&SH(d)==3&&AF(d)&&(AP(d)&2u)&&!PXN(d);}
static int data_leaf(u64 d){return ATTR(d)==3&&SH(d)==3&&AF(d)&&!(AP(d)&2u)&&PXN(d);}
static int tables_identity(const struct walk *first) {
    u64 list[8];u32 n=0;
    for(u32 i=0;i<first->count;i++)list[n++]=first->tables[i];
    for(u32 i=0;i<n;i++) {
        struct walk w;
        if(!walk(list[i],&w)||!data_leaf(w.desc)||leaf_pa(&w,list[i])!=list[i])return 0;
        for(u32 k=0;k<w.count;k++) {
            u32 seen=0;for(u32 j=0;j<n;j++)seen|=list[j]==w.tables[k];
            if(!seen) {if(n==8)return 0;list[n++]=w.tables[k];}
        }
    }
    return 1;
}
static int sysregs_ok(void) {
    u64 el,tcr,ttbr,mair,sctlr;
    __asm__ volatile("mrs %0,CurrentEL":"=r"(el));
    __asm__ volatile("mrs %0,tcr_el1":"=r"(tcr));
    __asm__ volatile("mrs %0,ttbr0_el1":"=r"(ttbr));
    __asm__ volatile("mrs %0,mair_el1":"=r"(mair));
    __asm__ volatile("mrs %0,sctlr_el1":"=r"(sctlr));
    return el==4&&(tcr&63)==25&&((tcr>>14)&3)==0&&((mair>>24)&255)==255&&
           (sctlr&0x1005)==0x1005&&(ttbr&OA_MASK)==ROOT;
}
static u64 code_alias(u64 va) {
    struct walk w;
    if(!walk(va,&w)||!code_leaf(w.desc)||!tables_identity(&w))return 0;
    u64 pa=leaf_pa(&w,va);
    if(pa<RAM_LO||pa>=RAM_HI)return 0;
    return pa+ALIAS_DELTA;
}
static int data_identity(u64 va) {
    struct walk w;
    return walk(va,&w)&&data_leaf(w.desc)&&leaf_pa(&w,va)==va&&tables_identity(&w);
}
static int mapping(struct map *m) {
    if(!sysregs_ok())return 0;
    u64 a=R64(ROOT+ALIAS_L1*8);
    if((a&3)!=1||(a&OA_MASK)!=RAM_LO||!data_leaf(a)||AP(a)!=1)return 0;
    m->hook_alias=code_alias(HOOK);m->anchor_alias=code_alias(ANCHOR);
    if(!m->hook_alias||!m->anchor_alias)return 0;
    if(code_alias(GUARD)!=m->anchor_alias-(ANCHOR-GUARD))return 0;
    return R32(HOOK)==R32(m->hook_alias)&&R64(ANCHOR)==R64(m->anchor_alias)&&
           R64(ANCHOR+8)==R64(m->anchor_alias+8)&&R64(GUARD)==R64(m->anchor_alias-(ANCHOR-GUARD));
}

/* ---- writes ---- */
static int anchor_put(const struct map *m,u32 state,u32 reason,u32 streak,u64 second) {
    u64 w=(u64)ANCHOR_MAGIC|((u64)state<<16)|((u64)reason<<24)|((u64)streak<<32);
    *(volatile u64 *)(uintptr_t)(m->anchor_alias+8)=second;
    __atomic_store_n((u64 *)(uintptr_t)m->anchor_alias,w,__ATOMIC_RELEASE);
    dsb();
    return R64(ANCHOR)==w&&R64(ANCHOR+8)==second;
}
static u32 bl(u64 at,u64 to){return 0x94000000u|(u32)(((to-at)>>2)&0x3ffffffu);}
static u64 bl_target(u64 at,u32 word) {
    u64 d=word&0x3ffffffu;
    if(d&0x2000000u)d|=~0x3ffffffull;
    return at+(d<<2);
}
static int code_word(u64 va,u64 alias,u32 old,u32 new) {
    if(R32(va)!=old||R32(alias)!=old)return 0;
    if(!x_cas32(alias,old,new))return 0;
    __asm__ volatile("dc civac,%0\n dsb sy\n ic ivau,%1\n dsb sy\n isb"
                     ::"r"(alias),"r"(va):"memory");
    return R32(va)==new;
}
static int data_word(u64 address,u64 old,u64 new) {
    if(!x_cas64(address,old,new))return 0;
    dmb();
    return R64(address)==new;
}
/* The hook word back to the stock call from either value this build gives it: 1 done (or
 * already stock), 2 foreign (never written), 0 not done (write or read-back failed). */
static u32 hook_to_stock(const struct map *m) {
    u32 now=R32(HOOK),stock=bl(HOOK,POLL);
    if(now==stock)return 1;
    if(now!=bl(HOOK,GUARD)&&now!=bl(HOOK,(u64)(uintptr_t)z63_armed))return 2;
    return code_word(HOOK,m->hook_alias,now,stock)?1:0;
}

/* ---- install table ---- */
struct rec {u32 kind,cls,order,groups,set,rank,wclass,flags,tx,resv;u64 address,old,new;};
#define REC_AT(i) ((u64)Z63_TABLE+(u64)REC_BYTES*(i))
NOINLINE static void rec_get(u32 i,struct rec *r) {
    u64 t=(u64)Z63_TABLE+(u64)REC_BYTES*i;
    r->kind=R8(t);r->cls=R8(t+1);r->order=R16(t+2);r->groups=R16(t+4);r->set=R8(t+6);
    r->rank=R8(t+7);r->wclass=R8(t+8);r->flags=R8(t+9);r->tx=R8(t+10);r->resv=R8(t+11)|R32(t+12);
    r->address=R64(t+16);r->old=R64(t+24);r->new=R64(t+32);
}
static int in_patch(u64 a){return a>=RANGE_LO+4&&a<(u64)Z63_TABLE&&!(a&3);}
static int in_text(u64 a){return a>=TEXT_LO&&a<TEXT_HI&&!(a&3);}
static int is_branch(u64 w){return !(w>>32)&&((w>>26)==0x25u||(w>>26)==0x05u);}   /* B or BL */
/* Generic shape rules. The infrastructure records (group 0) keep the S6 rules; a feature
 * group's record is one aligned text word whose new value is a B or BL into the patch code
 * (an old value that is not a branch needs restore-under-Q), or one aligned 64-bit data word
 * whose new value points into the patch code. Which word and which adapter is the verifier's
 * check against the group's declaration. */
_Static_assert(Z63_RECORDS>PINNED,"pinned records");
static int rec_ok(u32 i,const struct rec *r) {
    u32 wc=r->wclass;
    /* The last two records, and only they, are pinned deny records: a B word replaced by
     * `b z63_update_denied`; rec_site() keeps it to RAM-backed code (which word: the verifier). */
    if(i+PINNED>=Z63_RECORDS)   /* kind code, class X, order i, group 0 only, no set or rank; wrapper X, flags pin, rest 0 */
        return R64(REC_AT(i))==(K_CODE|'X'<<8|(u64)i<<16|1ull<<32)&&R64(REC_AT(i)+8)==(W_X|RF_PIN<<8)&&
               r->new==(0x14000000u|(u32)((((u64)(uintptr_t)z63_update_denied-r->address)>>2)&0x3ffffffu));
    if(r->order!=i||r->resv||r->tx>15u||(r->flags&~(RF_NEEDQ|RF_ATOMIC)))return 0;
    if((r->flags&RF_ATOMIC)&&(!r->set||!(r->flags&RF_NEEDQ)))return 0;
    if(!r->groups||(r->groups&~(u32)Z63_GROUPS)||(r->set?!r->rank:r->rank!=0))return 0;
    if(r->kind==K_PAGE)
        return i==0&&r->groups==1u&&r->cls=='D'&&r->address==ITEMS_POINTER&&r->old==ITEMS_STOCK&&r->new==0&&
               wc==W_NONE&&r->flags==RF_NEEDQ&&r->set==1u&&r->rank==1u&&!r->tx;
    if(!i||r->address==ITEMS_POINTER)return 0;
    if(r->groups==1u) {
        if(r->set||r->flags||r->tx||wc!=W_F)return 0;
        if(r->kind==K_CODE)
            return r->cls=='S'&&in_text(r->address)&&!(r->old>>32)&&!(r->new>>32)&&
                   (r->old>>26)==0x25&&(r->new>>26)==0x25&&
                   in_text(bl_target(r->address,(u32)r->old))&&in_patch(bl_target(r->address,(u32)r->new));
        if(r->kind==K_DATA)
            return r->cls=='R'&&!(r->address&7)&&r->address>=DATA_LO&&r->address<DATA_HI&&
                   in_text(r->old)&&in_patch(r->new);
        return 0;
    }
    if((r->groups&1u)||wc==W_NONE||wc>W_X||r->cls<'A'||r->cls>'Z')return 0;
    if(r->kind==K_CODE)
        return in_text(r->address)&&r->address!=HOOK&&!(r->old>>32)&&r->old!=r->new&&is_branch(r->new)&&
               in_patch(bl_target(r->address,(u32)r->new))&&(is_branch(r->old)||(r->flags&RF_NEEDQ));
    if(r->kind==K_DATA)
        return !(r->address&7)&&r->address>=DATA_LO&&r->address<DATA_HI&&in_patch(r->new)&&r->old!=r->new;
#if Z63_DATA16
    if(r->kind==K_DATA16)
        return !(r->address&1)&&r->address>=DATA_LO&&r->address<DATA_HI&&r->address!=ITEMS_POINTER&&
               r->old<0x10000u&&r->new<0x10000u&&r->old!=r->new;
#endif
    return 0;
}
NOINLINE static u64 rec_site(const struct rec *r) {
    if(r->kind==K_CODE) {
        u64 alias=code_alias(r->address);
        return alias&&R32(alias)==R32(r->address)?alias:0;
    }
    return data_identity(r->address)?r->address:0;
}
#if Z63_DATA16
NOINLINE static u64 rec_live(const struct rec *r){return r->kind==K_CODE?R32(r->address):r->kind==K_DATA16?R16(r->address):R64(r->address);}
#else
NOINLINE static u64 rec_live(const struct rec *r){return r->kind==K_CODE?R32(r->address):R64(r->address);}
#endif
static int page_like(u64 p){return p>=RAM_LO&&p<PAGE_HI&&!(p&0xfffu);}
NOINLINE static u64 rec_new(const struct rec *r,u64 page){return r->kind==K_PAGE?page:r->new;}
NOINLINE static int rec_put(const struct rec *r,u64 site,u64 from,u64 to) {
    if(r->kind==K_CODE)return code_word(r->address,site,(u32)from,(u32)to);
#if Z63_DATA16
    if(r->kind==K_DATA16) {
        if(!x_cas16(site,(u32)from,(u32)to))return 0;
        dmb();
        return R16(site)==to;
    }
#endif
    return data_word(site,from,to);
}
static u32 table_count(void) {
    u32 n=R32(HEADER+36);
    return R32(HEADER+32)==(u32)Z63_TABLE&&n==Z63_RECORDS?n:0;
}
static int header_ok(void) {
    static const u8 magic[8]={'Z','6','3','G','A','T','E','1'};
    for(u32 i=0;i<8;i++)if(R8(HEADER+i)!=magic[i])return 0;
    return R32(HEADER+16)==1&&R64(HEADER+24)==(u64)(uintptr_t)z63_entry&&table_count()!=0;
}
/* Every record meets its rules; one record per word; ranks rise in table order inside a set; a
 * set is all-or-none for all its members or for none; an all-or-none set is contiguous, of one
 * group and at most BATCH records. */
static int table_rules(void) {
    if(!header_ok())return 0;
    u32 n=table_count(),run=0;
    for(u32 i=0;i<n;i++) {
        struct rec r;rec_get(i,&r);
        if(!rec_ok(i,&r))return 0;
        u32 contiguous=i&&r.set&&R8(REC_AT(i-1)+6)==r.set;
        if(r.flags&RF_ATOMIC) {run=contiguous?run+1:1;if(run>BATCH)return 0;}
        for(u32 j=0;j<i;j++) {
            u64 e=REC_AT(j);
            if(R64(e+16)==r.address)return 0;
            if(!r.set||R8(e+6)!=r.set)continue;
            if(R8(e+7)>=r.rank||((R8(e+9)^r.flags)&RF_ATOMIC))return 0;
            if((r.flags&RF_ATOMIC)&&(R16(e+4)!=r.groups||!contiguous))return 0;
        }
    }
    return 1;
}
CARD static u32 table_check(void) {
    if(!table_rules())return R_TABLE;
    u32 n=table_count();
    for(u32 i=0;i<n;i++) {
        struct rec r;rec_get(i,&r);
        if(!rec_site(&r))return R_MAPPING;
        if(rec_live(&r)!=r.old)return R_SITE;
    }
    return R_NONE;
}
static u32 groups_of(u64 page){return ((const struct s5 *)(uintptr_t)(page+STATE_OFF))->groups;}
/* A record an install for `mask` writes: one of its groups is in mask and none of its other
 * contributors is already selected (with mask the selected groups: any contributor selected; a shared site installed at the arming for a contributor
 * stays as it is when stage 2 adds another). For the arming, mask is the selected groups. */
static int want(const struct rec *r,u64 page,u32 mask){return (r->groups&mask)&&!(r->groups&~mask&groups_of(page));}
/* The all-or-none set that starts at first (contiguous by table_rules): its last index. */
static u32 set_last(u32 first,u32 n) {
    u32 set=R8(REC_AT(first)+6),last=first;
    while(last+1<n&&R8(REC_AT(last+1)+6)==set&&(R8(REC_AT(last+1)+9)&RF_ATOMIC))last++;
    return last;
}
static u32 set_first(u32 last) {
    u32 set=R8(REC_AT(last)+6),first=last;
    while(first&&R8(REC_AT(first-1)+6)==set&&(R8(REC_AT(first-1)+9)&RF_ATOMIC))first--;
    return first;
}
/* Before LIVE only (still CLOSED): undo records [0, count) in reverse; only a word that still
 * holds the value this build installed is written. An all-or-none set is undone only when every
 * member is mapped and holds its installed or its stock value; otherwise it is left as it is
 * (the gate is STOCK: its wrappers pass through). */
NOINLINE static void uninstall(u32 count,u64 page) {
    for(u32 i=count;i--;) {
        struct rec r;rec_get(i,&r);
        if(!rec_ok(i,&r)||!page||(r.flags&RF_PIN))continue;   /* The deny records stay */
        if(r.flags&RF_ATOMIC) {
            u32 first=set_first(i),whole=1;
            for(u32 k=first;k<=i;k++) {
                struct rec e;rec_get(k,&e);u64 live=rec_live(&e);
                if(!rec_ok(k,&e)||!rec_site(&e)||(live!=e.old&&live!=e.new))whole=0;
            }
            for(u32 k=i+1;whole&&k-->first;) {
                struct rec e;rec_get(k,&e);u64 site=rec_site(&e);
                if(site&&rec_live(&e)==e.new)rec_put(&e,site,e.new,e.old);
            }
            i=first;continue;
        }
        u64 site=rec_site(&r);
        if(!site||rec_live(&r)!=rec_new(&r,page))continue;
        rec_put(&r,site,rec_new(&r,page),r.old);
    }
}
/* warm (standby wake): the RAM text kept every installed code record, so a code record of a
 * selected group must already hold its installed value and is not written; data records are
 * written as in a cold arming. */
static int kept(u32 warm,const struct rec *r){return warm&&r->kind==K_CODE;}
/* An all-or-none set: every member checked before any write (warm: code members at their
 * installed value, data members stock); a failed write puts back what the set wrote. 1 when the
 * whole set is installed. A set whose code half survived a wake and whose data half is stock is
 * completed here, never left split. */
NOINLINE static int install_set(u32 first,u32 last,u64 page,u32 warm) {   /* warm only with the arming's mask */
    for(u32 i=first;i<=last;i++) {
        struct rec r;rec_get(i,&r);
        if(!rec_ok(i,&r)||!rec_site(&r)||rec_live(&r)!=(kept(warm,&r)?r.new:r.old))return 0;
    }
    for(u32 i=first;i<=last;i++) {
        struct rec r;rec_get(i,&r);u64 site=rec_site(&r);
        if(kept(warm,&r)||(site&&rec_put(&r,site,r.old,r.new)))continue;
        for(u32 k=i;k-->first;) {
            struct rec e;rec_get(k,&e);u64 s=rec_site(&e);
            if(!kept(warm,&e)&&s&&rec_live(&e)==e.new)rec_put(&e,s,e.new,e.old);
        }
        return 0;
    }
    for(u32 i=first;i<=last;i++)W8(page+STATUS_OFF+i,ST_INSTALLED);
    return 1;
}
/* Returns the count of records [0, count) that may hold installed values (n when complete).
 * The deny records (the last PINNED) are written first, before any other record; a failure
 * there returns 0 (a deny record already written stays, as it would after LIVE). Warm: the deny
 * records and the selected code records are checked, not written. mask: the groups installed
 * (want()): the selected groups at an arming, the stage-2 groups in LIVE (no deny record). */
static u32 install(u32 n,u64 page,u32 warm,u32 mask) {
    for(u32 k=0;k<n;k++) {
        u32 i=k<PINNED?n-PINNED+k:k-PINNED;     /* the deny records, then the table in order */
        struct rec r;rec_get(i,&r);
        if(r.flags&RF_ATOMIC) {
            u32 last=set_last(i,n);
            if(want(&r,page,mask)&&!install_set(i,last,page,warm))return i;
            k=last+PINNED;continue;
        }
        if(!want(&r,page,mask))continue;         /* never installed; status stays UNSELECTED */
        u64 site=rec_ok(i,&r)?rec_site(&r):0;
        if(!site||(kept(warm,&r)?rec_live(&r)!=r.new:!rec_put(&r,site,r.old,rec_new(&r,page))))return k<PINNED?0:i;
        W8(page+STATUS_OFF+i,ST_INSTALLED);
    }
    return n;
}
/* A4: every record install() writes for mask holds its new value. */
NOINLINE static int self_ok(u32 n,u64 page,u32 mask) {
    for(u32 i=0;i<n;i++) {
        struct rec r;rec_get(i,&r);
        if(want(&r,page,mask)&&rec_live(&r)!=rec_new(&r,page))return 0;
    }
    return 1;
}

/* ---- the state page ---- */
static int node_valid(u64 p){return p>=HEAP_NODES&&p<HEAP_NODES+1024*24&&(p-HEAP_NODES)%24==0;}
static int heap_can_grant(void) {
    u64 node=R64(HEAP_FREE_HEAD);
    if(!node_valid(node))return 0;
    return R64(node+16)>=(R8(HEAP_GUARD_FLAG)?4096u:0u)+4096u;
}
static int heap_owns_page(u64 base) {
    u64 guard=R8(HEAP_GUARD_FLAG)?4096:0;
    if(base<guard)return 0;
    base-=guard;
    u64 node=R64(HEAP_USED_HEAD);
    for(u32 i=0;i<1024&&node;i++) {
        if(!node_valid(node))return 0;
        if(R64(node+8)==base)return R64(node+16)>=guard+4096;
        node=R64(node);
    }
    return 0;
}
static u64 irq_state(void){u64 v;__asm__ volatile("mrs %0,daif":"=r"(v));return v;}
static u32 cpu(void){u64 v;__asm__ volatile("mrs %0,mpidr_el1":"=r"(v));return (u32)v&15u;}
static u64 heap_page(void) {
    u64 before=irq_state();void *out=0;int ok=0;
    if(stock_heap_trylock(HEAP_LOCK))return 0;
    if(R32(HEAP_LOCK)==cpu()&&R32(HEAP_LOCK+4)==1&&heap_can_grant())
        ok=stock_heap_alloc(4096,&out)==0&&out&&heap_owns_page((u64)(uintptr_t)out);
    u32 depth=R32(HEAP_LOCK+4);
    int status=stock_heap_unlock(HEAP_LOCK);
    return ok&&depth==1&&!status&&irq_state()==before?(u64)(uintptr_t)out:0;
}
/* A1 PREPARE: the page, the rows copy, the counters, every record UNSELECTED, the gate block
 * with GATE = CLOSED, the groups deferred to stage 2; gmagic last (release), then DSB. */
NOINLINE static u64 page_prepare(u32 requested,u32 groups,u32 deferred) {
    if(R16(ITEMS_COUNT)!=ROWS||R64(ITEMS_POINTER)!=ITEMS_STOCK)return 0;
    u64 p=heap_page();
    if(!p||!page_like(p)||!data_identity(p))return 0;
    volatile u8 *d=(volatile u8 *)(uintptr_t)p;
    for(u32 i=0;i<4096;i++)d[i]=0;
#if Z63_ROWS_COUNT
    /* The video-menu row template of group Z63_ROWS_GROUP (G2) when that group is selected:
     * Z63_ROWS_COUNT rows from its read-only template (rows past 48 lie in its own state). */
    if((groups>>Z63_ROWS_GROUP)&1u)for(u32 i=0;i<Z63_ROWS_COUNT*ROW_BYTES;i++)d[i]=R8(Z63_ROWS_TEMPLATE+i);
    else
#endif
    for(u32 i=0;i<ROWS*ROW_BYTES;i++)d[i]=R8(ITEMS_STOCK+i);
    for(u32 i=0;i<Z63_RECORDS;i++)d[STATUS_OFF+i]=ST_UNSELECTED;
    struct s5 *s=(void *)(uintptr_t)(p+STATE_OFF);
    s->version=7;s->self=p;s->build=R64(HEADER+8);s->groups=groups;
    __atomic_store_n(&s->magic,STATE_MAGIC,__ATOMIC_RELEASE);
    u64 g=p+GATE_OFF;
    W64(g+G_SELF,p);W64(g+G_BUILD,R64(HEADER+8));
    W32(g+G_REQUESTED,requested);W32(g+G_DROPPED,requested&~groups&~deferred);W32(p+DEFER_OFF,deferred);
    st_rel32(g+G_WORD,P_CLOSED);
    st_rel32(g+G_MAGIC,GMAGIC);
    dsb();
    return p;
}
static void gate_store(u64 page,u32 phase,u32 reason,u32 broken) {
    st_rel32(page+GATE_OFF+G_WORD,phase|(reason&255u)<<8|broken<<16);
    dsb();
}

/* ---- predicates ---- */
/* A firmware update has started in this power-on (UPDATE_CLIENT, PINS_CLOSED above). */
NOINLINE static int updating(void){return (R64(UPDATE_CLIENT)|R8(PINS_CLOSED))!=0;}
/* Arming (as S3): includes the card. No update card phase, no update started (the poll
 * refuses an update explicitly first, R_UPDATE; inside the arming poll it fails as activity). */
/* A capture or recording in progress: any of the three work words nonzero, or stills work in
 * flight (frames held in the pre-release history, a still handed to the card writer). Read by
 * arming (settled_common), publication (native_idle) and Q (disarm, stage 2, warm arming). */
NOINLINE static u32 working(void) {
    return R32(CAPTURE_WORK)|R32(MOVIE_WORK)|R32(MOVIE_OUTSTANDING)|R32(STILLS_HISTORY)|(R64(STILLS_WRITER)!=0);
}
static int settled_common(void) {
    return R8(DETECT)==1&&R8(DETECT+5)==Z63_CARD_PRESENT&&R8(DETECT+12)==1&&!working()&&
           !(R32(UPDATER_BUSY)&0x400u)&&!updating();
}
/* The GUI terms: 0 the arming views may proceed (both switches off, or a sane object showing the
 * shooting screen with the i-menu closed), 1 a sane object away from it (another app id under
 * SHOOTING_SCREEN, the i-menu under IMENU_CLOSED: arming is only delayed), 2 a self pointer that is
 * not GUI_OBJECT (an object other than the one the pinned stock code initialises: never ready,
 * delays nothing). */
NOINLINE static u32 gui_state(void) {
    if(!(Z63_QM_IMENU||Z63_QM_SCREEN))return 0;
    if(R64(GUI_SELF)!=GUI_OBJECT)return 2;
    u32 app=R32(GUI_APP);
    return (Z63_QM_SCREEN&&app!=GUI_SHOOTING)||(Z63_QM_IMENU&&(app==GUI_IMENU||(R32(GUI_HOLD)&4u)))?1u:0u;
}
/* QM's view part: the movie live-view
 * pipeline running (state 2, a class of QM_CLASSES, each a movie-path class of 0x410538FC: 7, 0x0A
 * for N-RAW / ProRes RAW; 3 with the main menu open, 0 on a sub-page and in playback), the record
 * button not held (single-bit test of the switch snapshot; design 3.3 option 1). Not the readiness
 * word: it is 0 in idle movie view without a ready card. The work, update, card and GUI terms are
 * view()'s common part. */
NOINLINE static int qm(void) {
    u32 c=R32(PIPELINE_CLASS);
    return R32(PIPELINE_STATE)==2&&c<32u&&((Z63_QM_CLASSES>>c)&1u)&&!(R32(SWITCHES)&RECORD_BUTTON);
}
/* The view an arming accepts, at every settle poll and as the last check before any write: 1
 * settled photo live view (as S3: pipeline 2/5), 2 QM (settled idle movie view), 0 neither. In
 * both views the GUI must show the shooting screen with the i-menu closed (gui_state): G10 part
 * IM's data and Value16 records are i-menu tables, so an open i-menu counts as a menu open, as the
 * pipeline state does for the main menu. Stage 2 and the warm arming use view() too. */
NOINLINE static u32 view(void) {
    if(!settled_common()||gui_state())return 0;      /* GUI: shooting screen, i-menu closed */
    return R32(PIPELINE_STATE)==2&&R32(PIPELINE_CLASS)==5?1u:qm()?2u:0u;
}
/* The publication poll's native idle (3.4): the pipeline running (state 2) and not playback
 * (class 12), nothing captured or recorded. */
static int native_idle(void){return R32(PIPELINE_STATE)==2&&R32(PIPELINE_CLASS)!=12&&!working();}
/* Q (D-4): settled photo live view, no card terms. */
static int quiet(void) {
    return R32(PIPELINE_STATE)==2&&R32(PIPELINE_CLASS)==5&&R32(MOVIE_VIEW)!=1&&!working();
}
/* The declared quiescence terms of the groups in `groups` (beyond Q): word & mask equal to
 * value (op 0) or different from it (op 1). */
struct qterm {u64 address;u32 mask,value;u8 group,width,op,pad;};
#if Z63_QTERM_COUNT
static const struct qterm qterms[Z63_QTERM_COUNT]=Z63_QTERMS;
#endif
static int terms_hold(u32 groups) {
#if Z63_QTERM_COUNT
    for(u32 i=0;i<Z63_QTERM_COUNT;i++) {
        const struct qterm *t=&qterms[i];
        if(!((groups>>t->group)&1u))continue;
        u32 v=t->width==1?R8(t->address):R32(t->address);
        u32 hit=(v&t->mask)==t->value;
        if(t->op?hit:!hit)return 0;
    }
#endif
    (void)groups;
    return 1;
}
static const u16 deps[16]=Z63_DEPS;
/* The largest part of m whose every group has its dependencies in base or in that part. */
static u32 closed(u32 base,u32 m) {
    for(u32 k=0;k<16;k++)
        for(u32 g=1;g<16;g++)
            if(((m>>g)&1u)&&(deps[g]&~(base|m)))m&=~(1u<<g);
    return m;
}
/* The groups installed for an arm-file mask in view `mode` (1 photo, 2 QM). A group needing
 * quiescence whose terms do not hold now (with Q, or QM in movie view) is dropped. In QM a group
 * that may not install there (Z63_QM_OK) is deferred to stage 2, unless it is never deferred
 * (Z63_NODEFER: dropped). Then a group with a dependency neither installed nor deferred is
 * dropped, and one with a dependency deferred is deferred. In photo view nothing is deferred and
 * the result is S7's. */
NOINLINE static u32 select_groups(u32 requested,u32 mode,u32 *deferred) {
    u32 now=0,later=0;
    for(u32 g=1;g<16;g++) {
        u32 b=1u<<g;
        if(!(requested&b))continue;
        if(mode==2u&&!(Z63_QM_OK&b)) {if(!(Z63_NODEFER&b))later|=b;}
        else if(!(Z63_QNEED&b)||((mode==2u||quiet())&&terms_hold(b)))now|=b;
    }
    u32 all=closed(0,now|later),sel=closed(0,now&all);
    *deferred=closed(sel,all&~sel&~(u32)Z63_NODEFER);
    return sel|1u;
}
CARD static u32 crc32(u64 at,u64 n) {
    u32 c=~0u;
    while(n--) {c^=R8(at++);for(u32 k=0;k<8;k++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}
    return ~c;
}
CARDI static u64 le64(const u8 *p){u64 v=0;for(u32 i=0;i<8;i++)v|=(u64)p[i]<<(8*i);return v;}
CARDI static u32 le32(const u8 *p){u32 v=0;for(u32 i=0;i<4;i++)v|=(u32)p[i]<<(8*i);return v;}
/* The mask may name any group of this build and must name the infrastructure (bit 0). */
CARD static int arm_file_valid(const u8 *b) {
    static const u8 magic[8]={'Z','6','3','A','R','M',0,1};
    for(u32 i=0;i<8;i++)if(b[i]!=magic[i])return 0;
    if(le64(b+8)!=R64(HEADER+8))return 0;
    u64 groups=le64(b+16);
    if(!groups||(groups&~(u64)Z63_GROUPS)||!(groups&1u))return 0;
    if(le32(b+24))return 0;
    for(u32 i=28;i<60;i++)if(b[i])return 0;
    return crc32((u64)(uintptr_t)b,60)==le32(b+60);       /* the same CRC-32 as over the range (one copy, for size) */
}
CARD static int read_arm_file(u8 b[72]) {
    static const char path[]="A:\\Z63ARM.BIN";
    int fd=stock_fs_open(path,0,0x20);
    if(fd<0)return 0;
    u32 count=0;
    int rc=stock_fs_read(fd,b,65,&count);
    int cl=stock_fs_close(fd);
    return rc==0&&cl==0&&count==64;
}

/* ---- restoration ---- */
/* gpend / gleft: the groups with a record deferred / left (stopped, foreign, mapping failed)
 * so far in this pass, for the cross-group restore order (Z63_AFTER). */
struct pass {u32 pending,mapfail,left,writes,gpend,gleft;};
/* Cross-group restore order (restore_after, "readers last"): the groups whose records must all be
 * finished before a record of `groups` is restored. The table is in group-id order and restored
 * in reverse, so the groups waited for have been classified earlier in the same pass. */
#ifdef Z63_AFTER
static u32 after_of(u32 groups) {
    static const u16 after[16]=Z63_AFTER;
    u32 a=0;
    for(u32 g=1;g<16;g++)if((groups>>g)&1u)a|=after[g];
    return a;
}
#else
static u32 after_of(u32 groups){(void)groups;return 0;}
#endif
/* One record: classify, then write only "installed -> stock". */
NOINLINE static u32 restore_one(const struct rec *r,u64 page) {
    u64 site=rec_site(r);
    if(!site)return ST_MAPFAIL;
    u64 live=rec_live(r);
    if(live==r->old)return ST_ALREADY;
    if(r->kind==K_PAGE&&!page_like(page))return ST_FOREIGN;
    u64 inst=rec_new(r,page);
    if(live!=inst)return ST_FOREIGN;
    rec_put(r,site,inst,r->old);
    live=rec_live(r);
    if(live==r->old)return ST_RESTORED;
    return live==inst?ST_MAPFAIL:ST_VERIFY;
}
/* An all-or-none set [first, last], restored in one poll or not at all. own: the statuses are
 * this session's (read and written). q: Q held when the batch started. */
NOINLINE static void restore_set(u64 page,int own,u32 q,u32 stuck,struct pass *o,u32 first,u32 last) {
    u32 n=last-first+1,finished=0,stopped=0,unselected=0,pinned=0;u64 status=page+STATUS_OFF;
    struct rec r;
    for(u32 i=first;i<=last;i++) {
        rec_get(i,&r);
        if(!rec_ok(i,&r)) {o->left+=n;return;}
        u32 st=own?R8(status+i):ST_INSTALLED;
        if(st==ST_UNSELECTED)unselected++;
        else if(st==ST_RESTORED||st==ST_ALREADY)finished++;
        else if(st==ST_FOREIGN||st==ST_VERIFY||st==ST_PINNED)stopped++;
        if(r.tx&stuck)pinned=1;
    }
    if(unselected==n||finished==n)return;
    if(pinned&&!stopped&&own)
        for(u32 i=first;i<=last;i++)if(R8(status+i)==ST_INSTALLED||R8(status+i)==ST_MAPFAIL)W8(status+i,ST_PINNED);
    if(stopped||pinned) {o->left+=n-finished;o->gleft|=r.groups;return;}
    rec_get(first,&r);
    u32 wait=after_of(r.groups);
    if(wait&o->gleft) {o->left+=n-finished;o->gleft|=r.groups;return;}
    if(!q||!terms_hold(r.groups)||o->writes+n>BATCH||(wait&o->gpend)) {o->pending+=n-finished;o->gpend|=r.groups;return;}
    /* Classify every member before any write. */
    u32 foreign=0,mapfail=0;
    for(u32 i=first;i<=last;i++) {
        rec_get(i,&r);
        if(!rec_site(&r)) {mapfail=1;continue;}
        u64 live=rec_live(&r);
        if(live!=r.old&&live!=r.new) {foreign=1;if(own)W8(status+i,ST_FOREIGN);}
    }
    if(foreign) {o->left+=n-finished;o->gleft|=r.groups;return;}           /* nothing written; wrappers pass */
    if(mapfail) {
        if(own)for(u32 i=first;i<=last;i++)if(R8(status+i)==ST_INSTALLED)W8(status+i,ST_MAPFAIL);
        o->mapfail++;o->gleft|=r.groups;return;
    }
    /* Descending rank; a member that does not take its stock value stops the pass. */
    u64 done=0;u32 failed=n;
    for(u32 k=n;k--;) {
        u32 i=first+k;rec_get(i,&r);
        u64 site=rec_site(&r);
        if(rec_live(&r)==r.old) {if(own)W8(status+i,ST_ALREADY);continue;}
        if(site)rec_put(&r,site,r.new,r.old);
        if(!site||rec_live(&r)!=r.old) {failed=k;break;}
        done|=1ull<<k;o->writes++;
        if(own) {W8(status+i,ST_RESTORED);W32(page+GATE_OFF+G_CURSOR,R32(page+GATE_OFF+G_CURSOR)+1);}
    }
    if(failed==n)return;
    /* Put back what this pass restored: the set is as found, its wrappers pass through. If the
     * member still holds its installed value the whole set is retried later. */
    rec_get(first+failed,&r);
    u32 retry=rec_live(&r)==r.new;
    for(u32 k=failed+1;k<n;k++) {
        if(!((done>>k)&1u))continue;
        u32 i=first+k;rec_get(i,&r);u64 site=rec_site(&r);
        if(!(site&&rec_put(&r,site,r.old,r.new))) {retry=0;if(own)W8(status+i,ST_VERIFY);}
    }
    o->gleft|=r.groups;
    if(retry) {
        if(own)for(u32 i=first;i<=last;i++)if(R8(status+i)!=ST_ALREADY)W8(status+i,ST_MAPFAIL);
        o->mapfail++;
    } else {
        if(own) {
            W8(status+first+failed,ST_VERIFY);
            for(u32 k=failed+1;k<n;k++)if(R8(status+first+k)==ST_RESTORED)W8(status+first+k,ST_INSTALLED);
        }
        o->left+=n-finished;
    }
}
#if Z63_SAN_COUNT
/* Sanitiser: run-time words a group's tables can leave behind. Every entry whose word
 * holds a value in [lo, hi) (and, with a `when` mask, only when one of those entries matched in
 * this pass) is put back to its stock value by one bounded CAS, under Q and the group's terms;
 * without them the pass stays pending. Matches are taken before any write. */
struct san {u64 address,lo,hi,stock;u32 when;u8 group,width,pad[2];};
static const struct san sans[Z63_SAN_COUNT]=Z63_SANS;
static u64 san_read(const struct san *e) {
    return e->width==2?R16(e->address):e->width==4?R32(e->address):R64(e->address);
}
NOINLINE static void sanitise(u32 q,struct pass *o) {
    u32 hit=0;
    for(u32 i=0;i<Z63_SAN_COUNT;i++) {
        const struct san *e=&sans[i];
        if(!data_identity(e->address)) {o->mapfail++;continue;}
        u64 v=san_read(e);
        if(v>=e->lo&&v<e->hi)hit|=1u<<i;
    }
    for(u32 i=0;i<Z63_SAN_COUNT;i++) {
        const struct san *e=&sans[i];
        if(!((hit>>i)&1u)||(e->when&&!(hit&e->when)))continue;
        if(!q||!terms_hold(1u<<e->group)) {o->pending++;continue;}
        u64 v=san_read(e);
        if(v<e->lo||v>=e->hi)continue;
        u32 done=e->width==8?x_cas64(e->address,v,e->stock):e->width==4?x_cas32(e->address,(u32)v,(u32)e->stock):
#if Z63_DATA16
                 x_cas16(e->address,(u32)v,(u32)e->stock);
#else
                 0;
#endif
        if(!done)o->left++;
    }
}
#endif
#if Z63_RESTORE_COUNT
/* Per-group restore callbacks (Group.restore): u32 z63r_g<id>(u64 page), run once per pass
 * before the records, under Q and the group's terms, for a group selected at arming (own) or for
 * every composed group (page lost; page passed as 0). 0 nothing to do, 1 pending, 2 left (a value
 * the group does not know: left alone, reported). The verifier allows them no stock call. */
#define Z63_X_DECLARE_CB(g,f) u32 f(u64 page);
Z63_RESTORE_CBS(Z63_X_DECLARE_CB)
NOINLINE static void restore_callbacks(u64 page,int own,u32 q,struct pass *o) {
    u32 sel=own?((const struct s5 *)(uintptr_t)(page+STATE_OFF))->groups:(u32)Z63_GROUPS,diag=0;
#define Z63_X_CALL_CB(g,f) if((sel>>(g))&1u) { \
        u32 res=q&&terms_hold(1u<<(g))?f(own?page:0):1u; \
        if(res==1u) {o->pending++;diag|=1u<<(g);} else if(res==2u) {o->left++;diag|=1u<<(16+(g));} }
    Z63_RESTORE_CBS(Z63_X_CALL_CB)
    if(own)W32(page+GATE_OFF+G_RESTORECB,diag);
}
#undef Z63_X_CALL_CB
#undef Z63_X_DECLARE_CB
#endif
/* All records in reverse table order. own: the page is this session's and holds the
 * statuses. Otherwise every record is classified by its live value only and nothing but the
 * record words is written. A restore-under-Q record needs Q at the start of the batch and its
 * groups' terms. */
static void restore_pass(u64 page,int own,u32 q,u32 stuck,struct pass *o) {
    u64 stopped[4]={0,0,0,0},deferred[4]={0,0,0,0};   /* per coupling set; set 0 is no set */
    o->pending=o->mapfail=o->left=o->writes=o->gpend=o->gleft=0;
#if Z63_SAN_COUNT
    sanitise(q,o);
#endif
#if Z63_RESTORE_COUNT
    restore_callbacks(page,own,q,o);
#endif
    u32 n=table_count();
    for(u32 i=n;i--;) {
        struct rec r;rec_get(i,&r);
        if(!rec_ok(i,&r)) {o->left++;continue;}
        if(r.flags&RF_PIN)continue;                 /* Never restored, never counted */
        if(r.flags&RF_ATOMIC) {u32 first=set_first(i);restore_set(page,own,q,stuck,o,first,i);i=first;continue;}
        u64 bit=r.set?1ull<<(r.set&63u):0,status=page+STATUS_OFF+i;u32 w=(r.set>>6)&3u;
        u32 st=own?R8(status):ST_INSTALLED;
        if(st==ST_UNSELECTED||st==ST_RESTORED||st==ST_ALREADY)continue;
        u32 wait=after_of(r.groups);
        if(st==ST_FOREIGN||st==ST_VERIFY||st==ST_PINNED||(wait&o->gleft)) {stopped[w]|=bit;o->left++;o->gleft|=r.groups;continue;}
        if(r.tx&stuck) {if(own)W8(status,ST_PINNED);stopped[w]|=bit;o->left++;o->gleft|=r.groups;continue;}
        if(stopped[w]&bit) {o->left++;o->gleft|=r.groups;continue;}
        if(deferred[w]&bit) {o->pending++;o->gpend|=r.groups;continue;}
        if(((r.flags&RF_NEEDQ)&&!(q&&terms_hold(r.groups)))||o->writes>=BATCH||(wait&o->gpend)) {deferred[w]|=bit;o->pending++;o->gpend|=r.groups;continue;}
        u32 res=restore_one(&r,page);
        if(own) {W8(status,res);W32(page+GATE_OFF+G_CURSOR,R32(page+GATE_OFF+G_CURSOR)+1);}
        if(res==ST_RESTORED)o->writes++;
        else if(res==ST_MAPFAIL) {o->mapfail++;stopped[w]|=bit;o->gleft|=r.groups;}
        else if(res!=ST_ALREADY) {o->left++;stopped[w]|=bit;o->gleft|=r.groups;}
    }
}

/* ---- arming (1.4) ---- */
CARD static void fail(const struct map *m,u32 reason,u32 streak,u64 second,u32 installed,u64 page) {
    uninstall(installed,page);
    if(page)gate_store(page,P_STOCK,reason,0);
    anchor_put(m,S_STOCK,reason,streak,second);
    hook_to_stock(m);
}
static void stale_poll(u32 st,u32 reason,u32 streak,u64 page);

#if Z63_PUBCB_COUNT
static u32 counted(u64 g);
static void pub_callbacks(u64 page,u32 word,u32 run);
#endif
/* A2-A7, shared by the cold arming poll and the warm arming poll (warm: the selected code records,
 * the deny records and the hook word were kept by the RAM text and are checked, not written).
 * aw: the anchor streak bits for INSTALL; masks: requested (bits 0-15) and selected (16-31)
 * groups, kept in the ARMED anchor for a later wake; mode: the view of the last check (2 QM).
 * R_NONE when LIVE (or DRAINING after a failed anchor write, as before); otherwise the reason,
 * with *count the records that may hold installed values. */
NOINLINE static u32 arm_tail(const struct map *m,u64 page,u32 aw,u32 masks,u32 warm,u32 *count,u32 mode) {
    struct map again;
    /* A2, A3 (the deny records first). */
    anchor_put(m,S_INSTALL,R_NONE,aw,page);
    u32 n=table_count(),done=install(n,page,warm,masks>>16);
    *count=done;
    if(done!=n)return R_INSTALL;
    /* A4. */
    if(!self_ok(n,page,masks>>16))return R_SELF;
    if(!mapping(&again)||again.hook_alias!=m->hook_alias||again.anchor_alias!=m->anchor_alias)return R_MAPPING;
    /* A start that passed a tail branch before the deny records were written and stored its
     * client meanwhile: everything but the deny records goes back, the session stays stock. */
    if(updating())return R_UPDATE;
    /* A5: first publication, while CLOSED. */
    u64 g=page+GATE_OFF;
    u32 v=R32(SETTINGS_WORD);
    st_rel32(g+G_EFFECT,v);
#if Z63_ENTRY
    st_rel32(page+Z63_ENTRY_OFF,v);
#endif
    W32(g+G_PUBS,1);
#if Z63_PUBCB_COUNT
    /* Every publication callback pending from the first LIVE poll: a fresh page (cold arming, or
     * the warm arming after a wake) holds no group state, and the first publication above makes
     * the word "the same", so without this no callback would run until the settings word changed
     * (G10: a 3:2 entry stored before arming would never raise the plan latch in photo view). */
    W32(page+Z63_PUBCB_OFF,~0u);
#endif
    /* A6: the kill switch before LIVE (warm: kept, checked). */
    u32 armed=bl(HOOK,(u64)(uintptr_t)z63_armed);
    if(warm?R32(HOOK)!=armed:!code_word(HOOK,m->hook_alias,bl(HOOK,GUARD),armed))return R_HOOK;
    /* A7. */
    gate_store(page,P_LIVE,R_NONE,0);
    if(!anchor_put(m,S_ARMED,R_NONE,masks,page)) {
        W64(g+G_TFLIP,counter());gate_store(page,P_DRAINING,R_ANCHOR,0);
    }
#if Z63_PUBCB_COUNT
    /* Publication at a QM arming (design 3.4): the poll publishes in movie view only when the
     * anchor hold is free, which a group's anchor reads in live view may keep from happening for
     * long; so with QM at the last check, nothing counted and the hold free, the callbacks run
     * once here, on the word just published (z63p_g4, z63p_g6 align the global rate row and the
     * TENC index before an anchor can take the hold; z63p_g10 refuses outside photo view and
     * stays pending, as designed). */
    else if(mode==2u&&!counted(g)
#if Z63_HOLD_COUNT
            &&!(ld_acq32(page+Z63_HOLD_OFF)>>31)
#endif
           )pub_callbacks(page,v,~0u);
#else
    (void)mode;(void)v;
#endif
    return R_NONE;
}

CARD static u32 wake_data(void);
/* Cold readiness: the wake path's readiness without the group terms (select_groups drops a
 * group whose terms do not hold at the arming poll): both dispatcher callbacks registered and
 * every other data record at its stock value (wake_data() == 1), P and the row count stock, and
 * the view: settled photo live view as the cold arming always required, or QM (movie-view
 * arming). Returns view() (1 photo, 2 QM) or 0. Q is not required here (unchanged): select_groups()
 * still drops a group that needs quiescence when neither Q nor QM holds at the arming poll. */
CARDI static u32 cold_ready(void) {
    return wake_data()==1&&
           __atomic_load_n((const u64 *)(uintptr_t)ITEMS_POINTER,__ATOMIC_ACQUIRE)==ITEMS_STOCK&&
           R16(ITEMS_COUNT)==ROWS?view():0;
}
/* The arming window (recommendation adopted provisionally): one rule. A session
 * that never armed is closed (STOCK, the hook back to the stock call) at the first poll after
 * WINDOW_END_SECONDS of counter time since power-on that is neither ready (no settle streak) nor
 * in a state that only delays arming: a recording or capture in progress, the live-view pipeline
 * not running (main menu, sub-page, start-up), playback, a GUI app other than the shooting screen
 * or the i-menu open (with a sane GUI object). Those extend the window
 * while they hold, and a ready poll may finish its settle. WINDOW_HARD_SECONDS closes it in any
 * case, so a session that never arms still ends at the stock call. */
static int delaying(void){return !native_idle()||gui_state()==1u;}
NOINLINE static int window_closed(u64 now,u64 freq,u32 streak) {
    u64 t=now/freq;
    return t>=WINDOW_HARD_SECONDS||(t>=WINDOW_END_SECONDS&&!streak&&!delaying());
}
/* The OBSERVE anchor's streak word: bits 0-5 the settle streak, bit 6 its view (QM). The streak
 * restarts when the view changes. */
#define OBS_QM   0x40u
CARD static void step(void) {
    u64 freq=frequency(),now=counter();
    if(!freq||freq>>40)return;
    u32 state=anchor_state();
    if(state==S_STOCK||state==S_PARTIAL)return;   /* no re-arm in this power-switch cycle */
    if(state>=S_INSTALL) {
        u64 w=R64(ANCHOR);
        stale_poll(state,(u32)(w>>24)&255u,(u32)(w>>32),R64(ANCHOR+8));
        return;
    }
    u64 first=R64(ANCHOR+8);u32 obs=(u32)(R64(ANCHOR)>>32),tries=(u32)(R64(ANCHOR)>>24)&255u;
    u32 streak=obs&63u;
    struct map m;
    if(!mapping(&m))return;
    if(state!=S_NONE&&first>now)state=S_NONE;
    if(state==S_NONE) {streak=0;tries=0;}
    else if(state!=S_OBSERVE) {fail(&m,R_STALE,obs,first,0,0);return;}
    /* Anchor +8 is the counter of the last OBSERVE poll. A gap of more than three poll
     * periods (150 ms) means polls the guard did not pass on (card out, header) or a stalled
     * loop: the settle starts again. Time only ever delays arming here, never allows it. */
    else if(now-first>(freq*3u)/20u)streak=0;
    first=now;
    /* Every NONE/OBSERVE poll, before this poll writes anything: an update has started,
     * so this power-on stays stock (STOCK anchor, hook back to the stock call). */
    if(updating()) {fail(&m,R_UPDATE,obs,first,0,0);return;}
    if(tries>=COLD_FILE_TRIES) {fail(&m,R_STALE,obs,first,0,0);return;}
    u32 mode=cold_ready(),qmode=mode==2u?OBS_QM:0;
    streak=!mode?0:streak&&(obs&OBS_QM)==qmode?streak+1:1;
    obs=streak|qmode;
    if(window_closed(now,freq,streak)) {fail(&m,R_WINDOW,obs,first,0,0);return;}
    if(streak<SETTLE_POLLS) {
        /* OBSERVE: the reason byte counts the failed arm-file reads of this session. */
        if(!anchor_put(&m,S_OBSERVE,tries,obs,first))fail(&m,R_ANCHOR,obs,first,0,0);
        return;
    }
    if(!anchor_put(&m,S_READING,R_NONE,obs,first)) {fail(&m,R_ANCHOR,obs,first,0,0);return;}
    u8 b[72];
    for(u32 i=0;i<72;i++)b[i]=0;
    if(!read_arm_file(b)) {
        if(++tries<COLD_FILE_TRIES) {
            if(!anchor_put(&m,S_OBSERVE,tries,0,first))fail(&m,R_ANCHOR,0,first,0,0);
            return;
        }
        fail(&m,R_FILE,obs,first,0,0);return;
    }
    if(!arm_file_valid(b)) {fail(&m,R_CONTENT,obs,first,0,0);return;}
    anchor_put(&m,S_VERIFY,R_NONE,obs,first);
    if(!header_ok()) {fail(&m,R_HEADER,obs,first,0,0);return;}
    if(crc32(RANGE_LO,HEADER-RANGE_LO)!=R32(HEADER+20)) {fail(&m,R_CRC,obs,first,0,0);return;}
    u32 reason=table_check();
    if(reason) {fail(&m,reason,obs,first,0,0);return;}
    struct map again;
    if(!mapping(&again)||again.hook_alias!=m.hook_alias||again.anchor_alias!=m.anchor_alias) {
        fail(&m,R_MAPPING,obs,first,0,0);return;
    }
    if(cold_ready()!=mode) {fail(&m,R_ACTIVITY,obs,first,0,0);return;}
    /* A1 PREPARE, with the groups this poll may install (and, in QM, those deferred to stage 2). */
    u32 requested=(u32)le64(b+16),deferred,sel=select_groups(requested,mode,&deferred);
    u64 page=page_prepare(requested,sel,deferred);
    if(!page) {fail(&m,R_PAGE,obs,first,0,0);return;}
    if(!mapping(&again)||again.hook_alias!=m.hook_alias||again.anchor_alias!=m.anchor_alias||
       table_check()||cold_ready()!=mode) {fail(&m,R_MAPPING,obs,first,0,page);return;}   /* Last check before any record write; the same view */
    u32 count;
    reason=arm_tail(&m,page,obs,requested|sel<<16,0,&count,mode);
    if(reason)fail(&m,reason,obs,page,count,page);
}

/* ---- standby wake ---- */
/* The data side of the table: 2 when a data record holds its patch value (the data image was not
 * copied again: not a wake of this session), 1 when every data record reads its stock value
 * (stock has finished its own start-up: the capture callback (FastbootPhase slot 3) and the
 * movie callback (ReleasePhase slot 1, the last registration the gate depends on) are
 * registered), 0 otherwise (a word still 0 or another value: wait). P is checked by the caller. */
CARD static u32 wake_data(void) {
    u32 n=table_count(),ready=n!=0;
    for(u32 i=0;i<n;i++) {
        struct rec r;rec_get(i,&r);
        if(r.kind==K_CODE||r.kind==K_PAGE)continue;
        u64 live=rec_live(&r);
        if(live==r.new)return 2;
        if(live!=r.old)ready=0;
    }
    return ready;
}
/* The kept RAM text against the armed mask: the hook calls z63_armed, the guard's build ID is the
 * flashed header's, every code record of a selected group and both deny words hold their
 * installed value and every other code record its stock value (sites: mapped through the alias
 * too). */
CARD static u32 wake_code(u32 sel,u32 sites) {
    u32 n=table_count();
    if(!n||R32(HOOK)!=bl(HOOK,(u64)(uintptr_t)z63_armed)||R64(GUARD+104)!=R64(HEADER+8))return 0;
    for(u32 i=0;i<n;i++) {
        struct rec r;rec_get(i,&r);
        if(r.kind!=K_CODE)continue;
        if(rec_live(&r)!=((r.groups&sel)?r.new:r.old)||(sites&&!rec_site(&r)))return 0;
    }
    return 1;
}
/* Ready for the warm arming: every data record stock, the card settled (no update, no update
 * activity), photo live view quiet (Q) or QM (movie-view arming: after a wake every code record
 * is kept, only data records are written, so nothing is deferred), the selected groups' terms,
 * the stock row count. Returns the view (1 photo, 2 QM) or 0. */
NOINLINE static u32 wake_ready(u32 sel) {
    u32 v=wake_data()==1&&terms_hold(sel)&&R16(ITEMS_COUNT)==ROWS?view():0;
    return v==2u||(v==1u&&quiet())?v:0;
}
#if STAGE2
/* After a wake: the requested groups that were not installed (deferred, or dropped at the cold
 * arming) whose dependencies are installed or with them, never a Z63_NODEFER group, wait for
 * stage 2 again on the fresh page (stage 2 installs them in the armed poll, as after a cold
 * arming in QM). */
static u32 carry(u32 req,u32 sel){return closed(sel,req&~sel&~(u32)Z63_NODEFER&0xfffeu);}
#else
static u32 carry(u32 req,u32 sel){(void)req;(void)sel;return 0;}
#endif
#define WAKE_RETRY 0x100u
#define WAKE_QM    0x8000u     /* anchor +8 in WAKE: the view the ready count is for (QM) */
/* The warm arming poll (2.4): 0 when handled (ARMED with a fresh page, DRAINING after a failed
 * anchor write, or a failure after the page was taken: the records this poll may have written and
 * the kept code put back, the page's gate STOCK, the anchor DRAINING/STALE with the page, so the
 * stale path finishes); WAKE_RETRY|reason for a file read or page refused; else the reason a wake
 * is refused before any write. */
CARD static u32 warm_arm(const struct map *m,u32 masks,u32 mode) {
    u32 req=masks&0xffffu,sel=masks>>16;
    u8 b[72];
    for(u32 i=0;i<72;i++)b[i]=0;
    if(!read_arm_file(b))return WAKE_RETRY|R_FILE;
    if(!arm_file_valid(b)||le64(b+16)!=req)return R_CONTENT;    /* deleted, other card, other mask or build */
    if(!header_ok())return R_HEADER;
    if(crc32(RANGE_LO,HEADER-RANGE_LO)!=R32(HEADER+20))return R_CRC;
    if(!table_rules())return R_TABLE;
    struct map again;
    if(!mapping(&again)||again.hook_alias!=m->hook_alias||again.anchor_alias!=m->anchor_alias)return R_MAPPING;
    if(!wake_code(sel,1))return R_SITE;
    if(wake_ready(sel)!=mode)return R_ACTIVITY;
    u64 page=page_prepare(req,sel,carry(req,sel));   /* a fresh page; the old one is never used */
    if(!page)return WAKE_RETRY|R_PAGE;
    u32 count=0,why=R_MAPPING;
    if(mapping(&again)&&again.hook_alias==m->hook_alias&&again.anchor_alias==m->anchor_alias&&
       wake_code(sel,1)&&wake_ready(sel)==mode)   /* last check before any record write; the same view */
        why=arm_tail(m,page,masks,masks,1,&count,mode);
    if(why) {
        uninstall(table_count(),page);
        gate_store(page,P_STOCK,why,0);
        anchor_put(m,S_DRAINING,R_STALE,0,page);
    }
    return 0;
}
/* ARMED or WAKE with no live page. 0: not a standby wake of this session (the stale path runs
 * as before). Before the warm arming poll nothing but the anchor is written (masks kept; +8:
 * WAKE_TAG, ready streak bits 0-15, Q polls without readiness 16-31, retries 32-39). A wake
 * refused: anchor DRAINING/STALE with WAKE_REFUSED|reason at +8 (never a page), so the stale
 * restore puts every kept word back once Q has held (STOCK except the deny words). */
CARD static u32 wake_poll(u32 st,u32 masks,u64 second) {
    if(R32(WAKE_FLAG)!=1||__atomic_load_n((const u64 *)(uintptr_t)ITEMS_POINTER,__ATOMIC_ACQUIRE)!=ITEMS_STOCK)return 0;
    u32 data=wake_data();
    if(data==2)return 0;
    struct map m;
    if(!mapping(&m))return 1;                     /* nothing written; every adapter passes */
    u32 req=masks&0xffffu,sel=masks>>16,why=R_NONE;
    u64 c=st==S_WAKE?second:WAKE_TAG;
    u32 ready=(u32)c&0xffffu,waits=(u32)(c>>16)&0xffffu,retries=(u32)(c>>32)&255u,v=0;
    if(updating())why=R_UPDATE;                   /* Never re-arm once an update has started */
    else if(!(sel&1u)||(sel&~req)||(req&~(u32)Z63_GROUPS))why=R_STATE;
    else if(!wake_code(sel,0))why=R_SITE;         /* text changed during standby, or deny words not as installed */
    else if(!(data&&(v=wake_ready(sel)))) {
        ready=0;
        if((quiet()||qm())&&++waits>=WAKE_GIVEUP)why=R_ACTIVITY;
    }
    else {
        /* The ready count (bits 0-14) restarts when the view changes (bit 15: QM). */
        u32 tag=v==2u?WAKE_QM:0;
        if((ready&WAKE_QM)!=tag)ready=tag;
        if((++ready&~WAKE_QM)>=WAKE_SETTLE) {
            why=warm_arm(&m,masks,v);
            if(!why)return 1;
            if(why&WAKE_RETRY) {ready=0;why=++retries>=WAKE_RETRIES?why&255u:R_NONE;}
        }
    }
    if(why)anchor_put(&m,S_DRAINING,R_STALE,0,WAKE_REFUSED|why);
    else anchor_put(&m,S_WAKE,R_NONE,masks,WAKE_TAG|(u64)retries<<32|(u64)waits<<16|ready);
    return 1;
}

__attribute__((section(".text.entry"))) u64 z63_entry(void) {
    u64 result=stock_poll();
    step();
    return result;
}

/* ---- armed operation (1.5, 1.7, 3.4) ---- */
NOINLINE static u32 integrity(u64 page) {
    u32 n=table_count(),broken=0;
    for(u32 i=0;i<n;i++) {
        if(R8(page+STATUS_OFF+i)!=ST_INSTALLED)continue;
        struct rec r;rec_get(i,&r);
        u64 live=rec_live(&r);
        if(live==rec_new(&r,page))continue;
        if(live==r.old&&r.kind==K_DATA&&r.cls=='R') {W8(page+STATUS_OFF+i,ST_ALREADY);continue;}
        broken|=r.groups;
    }
    return broken;
}
/* Publication fence, LIVE only, from the armed poll:
 *   F = Q (settled photo live view, as before), or
 *       native idle for PUB_SETTLE consecutive polls: pipeline state 2, class not 12, capture
 *       worker 0xA048BB044, movie state 0xA03324CE0 and movie outstanding 0xA03324DC8 all 0
 *       (the admitted-idle condition; recording keeps one of them nonzero), and no
 *       selected group of Z63_MOVIE_HOLD (groups not declaring movie_publish);
 *   and every TX and ACT count 0 (ACT[0..3] include the four dispatcher thunks, so a movie or
 *   capture dispatcher callback in progress refuses it), checked after PUBBUSY is set; F is
 *   checked again there. DRAINING never publishes (the word is frozen). */
#define PUB_SETTLE 4u
static int pub_fence(u64 page,u32 idle) {
    if(quiet())return 1;
#if Z63_ENTRY
    /* R-G10-0: groups without movie_publish read the movie-entry word, which moves only under Q
     * (publish below), so they no longer hold movie-view publication for the others. */
    (void)page;
    return idle>=PUB_SETTLE&&native_idle();
#else
    return idle>=PUB_SETTLE&&!(R32(page+STATE_OFF+28)&Z63_MOVIE_HOLD)&&native_idle();
#endif
}
#if Z63_HOLD_COUNT
/* Anchor hold (gate_api.h): the word at page +Z63_HOLD_OFF. Mode 1 (a native-idle LIVE poll
 * outside Q) or 3 (a LIVE poll outside Q that is not native idle): an anchor read since the last
 * poll (TOUCH) or mode 3 restarts the quiet count; HOLD_QUIET consecutive quiet idle polls release
 * it. Mode 2 (Q): release. Returns 1 while held. One bounded CAS; a lost CAS (an anchor read
 * meanwhile) counts as held. */
#define HOLD_QUIET 8u
NOINLINE static u32 holds(u64 page,u32 mode) {
    u64 a=page+Z63_HOLD_OFF;
    u32 v=R32(a),n=0;
    if(!(v&Z63_HOLD_HELD))return 0;
    if(mode==1u&&!(v&Z63_HOLD_TOUCH)) {u32 c=((v>>16)&255u)+1u;if(c<HOLD_QUIET)n=Z63_HOLD_HELD|c<<16;}
    else if(mode!=2u)n=Z63_HOLD_HELD;
    if(!x_cas32(a,v,n))return 1;
    return n>>31;
}
#endif
#if Z63_PUBLISHING||Z63_BREAKCB_COUNT
/* dmb, then the OR of every ACT and TX count. */
NOINLINE static u32 counted(u64 g) {
    u32 busy=0;
    dmb();
    for(u32 c=0;c<4;c++)busy|=ld_acq32(g+G_ACT+4*c)|ld_acq32(g+G_TX+4*c);
    return busy;
}
#endif
#if Z63_PUBCB_COUNT
/* Publication callbacks (Group.publish): u32 z63p_g<id>(u64 page, u32 word), for the selected
 * groups in `run`, with every TX and ACT count 0 and the fence holding; bit g of +0xC40 is set
 * while group g's callback is pending (it returned 1, or the counts were not 0 after the store). */
#define PUBCB_OFF 0xc40u
#define Z63_X_DECLARE_PCB(g,f) u32 f(u64 page,u32 word);
Z63_PUBCBS(Z63_X_DECLARE_PCB)
#undef Z63_X_DECLARE_PCB
NOINLINE static void pub_callbacks(u64 page,u32 word,u32 run) {
    u32 sel=R32(page+STATE_OFF+28)&run,pend=0;
#define Z63_X_PCB(g,f) if(((sel>>(g))&1u)&&f(page,word)==1u)pend|=1u<<(g);
    Z63_PUBCBS(Z63_X_PCB)
#undef Z63_X_PCB
    W32(page+PUBCB_OFF,pend);
}
#endif
static void publish(u64 page) {
    u64 g=page+GATE_OFF;
    u32 idle=R32(g+G_PUBIDLE);
#if Z63_HOLD_COUNT
    u32 ni=native_idle();
    idle=ni?(idle<PUB_SETTLE?idle+1:PUB_SETTLE):0;
    W32(g+G_PUBIDLE,idle);
    if(holds(page,quiet()?2u:ni?1u:3u))return;   /* A held anchor hold refuses (Q releases it) */
#else
    idle=native_idle()?(idle<PUB_SETTLE?idle+1:PUB_SETTLE):0;
    W32(g+G_PUBIDLE,idle);
#endif
    if(!pub_fence(page,idle))return;
#if Z63_PUBLISHING
    if(counted(g))return;                /* Any TX or ACT count refuses (stricter than TX only) */
#else
    for(u32 c=0;c<4;c++)if(ld_acq32(g+G_TX+4*c))return;
#endif
    u32 v=R32(SETTINGS_WORD);
#if Z63_ENTRY
    u32 same=v==R32(g+G_EFFECT)&&!(quiet()&&R32(page+Z63_ENTRY_OFF)!=v);   /* under Q the entry word follows */
#else
    u32 same=v==R32(g+G_EFFECT);
#endif
#if Z63_PUBCB_COUNT
    if(same) {
        u32 pend=R32(page+PUBCB_OFF);
        if(pend)pub_callbacks(page,v,pend);
        return;
    }
#else
    if(same)return;
#endif
    st_rel32(g+G_PUBBUSY,1);
#if Z63_PUBLISHING
    u32 busy=counted(g);                 /* dmb, then every ACT and TX count */
#else
    dmb();
    u32 busy=0;
    for(u32 c=0;c<4;c++)busy|=ld_acq32(g+G_ACT+4*c)|ld_acq32(g+G_TX+4*c);
#endif
#if Z63_HOLD_COUNT
    /* After the busy store: an anchor read that took a hold before it is seen here (or its ACT
     * count above); one admitted after it waits for the store and holds the new word. */
    busy|=ld_acq32(page+Z63_HOLD_OFF)>>31;
#endif
#if Z63_PUBCB_COUNT||Z63_ENTRY
    u32 done=0;
    if(!busy&&pub_fence(page,idle)) {
        st_rel32(g+G_EFFECT,v);W32(g+G_PUBS,R32(g+G_PUBS)+1);done=1;
#if Z63_ENTRY
        if(quiet())st_rel32(page+Z63_ENTRY_OFF,v);       /* R-G10-0, inside the busy window */
#endif
    }
    st_rel32(g+G_PUBBUSY,0);
#if Z63_PUBCB_COUNT
    if(done) {
        if(counted(g))W32(page+PUBCB_OFF,~0u);
        else pub_callbacks(page,v,~0u);
    }
#else
    (void)done;
#endif
#else
    if(!busy&&pub_fence(page,idle)) {st_rel32(g+G_EFFECT,v);W32(g+G_PUBS,R32(g+G_PUBS)+1);}
    st_rel32(g+G_PUBBUSY,0);
#endif
}
static void flip(u64 page,u32 reason,u32 broken) {
    W64(page+GATE_OFF+G_TFLIP,counter());W32(page+GATE_OFF+G_QUIET,0);
    gate_store(page,P_DRAINING,reason,broken);
    struct map m;
    if(mapping(&m))anchor_put(&m,S_DRAINING,reason,0,page);
}
#if STAGE2
/* Stage 2 (movie-view arming), LIVE only, from the armed
 * poll. The groups deferred at a QM arming (page +0xC3C; their records stock, their adapters
 * passing: z63g_phase() tests the selected mask) whose condition holds: settled photo live view
 * (view() 1 and Q; a deferred group is one QM may not install, so stage 2 never runs in QM) and
 * the group's terms, with their dependencies installed or in the same pass. 0 if none. */
static u32 eligible(u32 d,u32 sel) {
    if(view()!=1u||!quiet())return 0;
    u32 e=0;
    for(u32 g=1;g<16;g++)if(((d>>g)&1u)&&terms_hold(1u<<g))e|=1u<<g;
    return closed(sel,e);
}
/* Once the same groups have been eligible for SETTLE_POLLS polls, and again as
 * the last test: the existing installer for those groups only (want(): no deny record, no shared
 * site a selected contributor already has), the A4 self-check of what it wrote, then one store
 * adds them to the selected mask, their publication callbacks are pending, the deferred mask
 * drops them and the ARMED anchor carries the new mask (a later wake checks the kept code against
 * it). Any failure: DRAINING, the normal restore (conservative: the groups written in this pass
 * are never selected, their adapters pass, their records go back as INSTALLED records do). A
 * standby before the anchor write finds code the anchor's mask does not name: the stale restore.
 * 1 when the gate left LIVE. */
NOINLINE static u32 stage2(u64 page) {
    u32 d=R32(page+DEFER_OFF);
    if(!d)return 0;
    u32 sel=groups_of(page),k=eligible(d,sel),c=R32(page+S2_OFF);
    c=!k?0:(c>>16)==k?c+1:k<<16|1u;
    W32(page+S2_OFF,c);
    if((c&0xffffu)<SETTLE_POLLS||eligible(d,sel)!=k)return 0;
    W32(page+S2_OFF,0);
    u32 e=k,n=table_count(),why=install(n,page,0,e)!=n?R_INSTALL:!self_ok(n,page,e)?R_SELF:R_NONE;
    if(!why) {
        st_rel32(page+STATE_OFF+28,sel|e);
        W32(page+DEFER_OFF,d&~e);
#if Z63_PUBCB_COUNT
        W32(page+PUBCB_OFF,R32(page+PUBCB_OFF)|e);
#endif
        struct map m;
        if(mapping(&m)&&anchor_put(&m,S_ARMED,R_NONE,R32(page+GATE_OFF+G_REQUESTED)|(sel|e)<<16,page))return 0;
        why=R_ANCHOR;
    }
    flip(page,why,0);
    return 1;
}
#endif
static void finish(u64 page,u32 g,const struct pass *o) {
    u32 n=table_count(),bad=0,stuck=R32(page+GATE_OFF+G_STUCK)&15u;
    for(u32 i=0;i<n;i++) {
        u32 st=R8(page+STATUS_OFF+i);
        if(st!=ST_RESTORED&&st!=ST_ALREADY)continue;
        struct rec r;rec_get(i,&r);
        if(rec_live(&r)!=r.old) {W8(page+STATUS_OFF+i,ST_VERIFY);bad++;}
    }
    u32 partial=o->mapfail||o->left||bad;
    W32(page+GATE_OFF+G_OUTCOME,partial?2:1);
    gate_store(page,P_STOCK,REASON(g),BROKEN(g));
    struct map m;
    if(!mapping(&m))return;
    u32 state=partial?S_PARTIAL:S_STOCK,streak=stuck<<24;
    anchor_put(&m,state,REASON(g),streak,page);
    if(!o->mapfail&&hook_to_stock(&m))anchor_put(&m,state,REASON(g),streak|A_FINISHED,page);
}
#if Z63_BREAKCB_COUNT
/* R-G10-1: break callbacks u32 z63n_g<id>(u64 page) (Group.restore_now), each run from the poll
 * once its group is broken in LIVE or DRAINING, as soon as nothing is recording (movie state and
 * movie outstanding 0) and every TX and ACT count is 0; retried on later polls until it returns 0,
 * then marked done (raise word bits 16-31, by CAS). It puts the group's run-time words back to
 * stock now; the restore pass still runs its restore callback later. */
#define Z63_X_DECLARE_BCB(g,f) u32 f(u64 page);
Z63_BREAKCBS(Z63_X_DECLARE_BCB)
#undef Z63_X_DECLARE_BCB
NOINLINE static void break_callbacks(u64 page,u32 broken) {
    u64 a=page+Z63_RAISE_OFF;
    u32 w=R32(a),need=broken&~(w>>16)&R32(page+STATE_OFF+28),done=0;
    if(!need||R32(MOVIE_WORK)||R32(MOVIE_OUTSTANDING)||counted(page+GATE_OFF))return;
#define Z63_X_BCB(g,f) if(((need>>(g))&1u)&&!f(page))done|=1u<<(16+(g));
    Z63_BREAKCBS(Z63_X_BCB)
#undef Z63_X_BCB
    if(done)(void)x_cas32(a,w,w|done);     /* lost (a raise meanwhile): run again, idempotent */
}
#endif
static void gate_poll(u64 page) {
    u64 G=page+GATE_OFF;
    u32 g=gate_word(page),ph=PHASE(g),reason=REASON(g),broken=BROKEN(g);
    struct s5 *s=(void *)(uintptr_t)(page+STATE_OFF);
    if(!data_identity(page))return;
    if(ph==P_LIVE) {
        u32 why=R_NONE,b=0;
        if(!table_rules())why=R_TABLE;
        else {
            if(R32(G+G_FAULT)&(F_OVERFLOW|F_UNDERFLOW|F_LEAK))why=R_FAULT;
            b=integrity(page);
#if Z63_RAISE
            b|=R32(page+Z63_RAISE_OFF)&0xfffeu;          /* R-G10-4: group-raised breaks */
#endif
            if(b&&!why)why=R_SELF;
        }
        u32 out=R8(DETECT)==1&&R8(DETECT+5)!=Z63_CARD_PRESENT?R32(G+G_CARDOUT)+1:0;
        W32(G+G_CARDOUT,out);
        if(!why&&out>=CARD_OUT_POLLS)why=R_CARD;
#if STAGE2
        if(!why&&stage2(page))return;
#endif
        if(!why) {publish(page);s->polls=s->polls+1;return;}
        flip(page,why,broken|b);
#if Z63_BREAKCB_COUNT
        break_callbacks(page,broken|b);
#endif
        return;
    }
    if(ph==P_DRAINING) {
        /* The anchor follows the gate word (flip() writes it only when the mapping holds): a
         * session that is draining never takes the wake path after a standby. */
        if(anchor_state()==S_ARMED) {struct map m;if(mapping(&m))anchor_put(&m,S_DRAINING,reason,0,page);}
        if(reason!=R_TABLE&&table_rules()) {
            u32 b=integrity(page);
#if Z63_RAISE
            b|=R32(page+Z63_RAISE_OFF)&0xfffeu;
#endif
            if(b&~broken) {broken|=b;gate_store(page,P_DRAINING,reason,broken);}
        }
#if Z63_BREAKCB_COUNT
        break_callbacks(page,broken);
#endif
        dmb();
        u32 busy=0;
        if(reason!=R_TABLE)for(u32 c=0;c<4;c++)if(ld_acq32(G+G_TX+4*c))busy|=1u<<c;
        u32 q=R32(G+G_QUIET);q=quiet()?q+1:0;W32(G+G_QUIET,q);
        u32 stuck=R32(G+G_STUCK);
        if(q>=STUCK_POLLS&&(busy&~stuck)) {stuck|=busy;W32(G+G_STUCK,stuck);}
        busy&=~stuck;
        if(busy||q<QUIET_POLLS)return;
        gate_store(page,P_RESTORING,reason,broken);
        if(!quiet())fault(page,F_RACE);
        struct map m;
        if(mapping(&m))anchor_put(&m,S_RESTORING,reason,0,page);
        g=gate_word(page);ph=P_RESTORING;
    }
    else if(ph!=P_RESTORING&&ph!=P_STOCK) {
        gate_store(page,P_RESTORING,R_STATE,broken);
        g=gate_word(page);ph=P_RESTORING;
    }
    /* After the RESTORING store (every wrapper now refuses a new ACT hold: z63g_act_enter and
     * z63_enter re-read the phase after their increment and barrier), wait until every ACT count
     * reads 0, so no hold admitted in LIVE or DRAINING (a producer storing a patch value, a
     * dispatcher callback) is still running when the first record is restored. Store-buffering
     * argument as for TX (spec 1.6c). Bounded: after STUCK_POLLS polls the pass runs anyway (the
     * previous behaviour; ACT pins nothing). */
    dmb();
    u32 act=0;
    for(u32 c=0;c<4;c++)act|=ld_acq32(G+G_ACT+4*c);
    u32 waited=R32(G+G_ACTWAIT);
    if(act&&waited<STUCK_POLLS) {W32(G+G_ACTWAIT,waited+1);return;}
    struct pass o;
    restore_pass(page,1,quiet(),R32(G+G_STUCK)&15u,&o);
    if(o.pending)return;
    finish(page,g,&o);
}
NOINLINE static void stale_poll(u32 st,u32 reason,u32 streak,u64 page) {
    struct map m;
    if(!mapping(&m))return;
    if(st!=S_DRAINING||reason!=R_STALE)streak=0;
    streak=quiet()?streak+1:0;
    if(streak<QUIET_POLLS) {anchor_put(&m,S_DRAINING,R_STALE,streak,page);return;}
    struct pass o;
    restore_pass(page,0,1,0,&o);
    if(o.pending) {anchor_put(&m,S_DRAINING,R_STALE,streak,page);return;}
    u32 state=o.mapfail||o.left?S_PARTIAL:S_STOCK;
    anchor_put(&m,state,R_STALE,0,page);
    if(!o.mapfail&&hook_to_stock(&m))anchor_put(&m,state,R_STALE,A_FINISHED,page);
}
NOINLINE static void post_stock(u32 st,u32 reason,u32 streak,u64 page) {
    if(streak&A_FINISHED)return;
    struct map m;
    if(!mapping(&m))return;
    u32 cadence=(streak&255u)+1,attempts=(streak>>8)&0xffffu,stuck=(streak>>24)&15u;
    if(cadence<RETRY_EVERY) {anchor_put(&m,st,reason,(streak&~255u)|cadence,page);return;}
    attempts++;
    struct pass o;
    restore_pass(page,0,quiet(),stuck,&o);
    u32 finished=0;
    if((!o.mapfail&&!o.pending)||attempts>=RETRY_MAX)finished=hook_to_stock(&m)!=0||attempts>=RETRY_MAX;
    u32 state=o.mapfail||o.left||o.pending?S_PARTIAL:S_STOCK;
    anchor_put(&m,state,reason,(finished?A_FINISHED:0)|stuck<<24|(attempts&0xffffu)<<8,page);
}
static void armed_poll(void) {
    u64 w=R64(ANCHOR),page=R64(ANCHOR+8);
    u32 st=anchor_state(),reason=(u32)(w>>24)&255u,streak=(u32)(w>>32);
    if(st==S_STOCK||st==S_PARTIAL) {post_stock(st,reason,streak,page);return;}
    if(st>=S_INSTALL&&st!=S_STOCK&&page_like(page)&&
       __atomic_load_n((const u64 *)(uintptr_t)ITEMS_POINTER,__ATOMIC_ACQUIRE)==page&&
       R32(page+GATE_OFF+G_MAGIC)==GMAGIC&&R64(page+GATE_OFF+G_SELF)==page&&
       R64(page+GATE_OFF+G_BUILD)==R64(HEADER+8)) {gate_poll(page);return;}
    /* A standby wake of an ARMED session takes the wake path; anything else (and a WAKE that
     * no longer looks like one) the stale path, as before. A WAKE anchor's +8 is never a page. */
    if((st==S_ARMED||st==S_WAKE)&&wake_poll(st,streak,page))return;
    stale_poll(st,reason,streak,st==S_WAKE?0:page);
}
u64 z63_armed(void) {
    u64 result=stock_poll();
    armed_poll();
    __asm__ volatile("":::"memory");
    return result;
}
#pragma clang attribute pop
