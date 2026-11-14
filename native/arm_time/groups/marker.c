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
/* Group 1, the marker (S5M/S6 behaviour on the composed gate): a contributor of the shared
 * site video_refresh (role "after", signature in ../sites/video_refresh.h). In LIVE it puts
 * "Z63 ARMED", in DRAINING "Z63 OFF*" into its text slot (Z63_TRIAL_SLOT, 285 from the
 * text-slot registry) and binds it to the value widget of the trial item's row (Z63_TRIAL_ITEM
 * from the declaration: 0x31ED, the microphone input row, which no feature group uses). Declared
 * by z63patch/gate_groups/marker.py; linked alone at
 * its fixed range. Calls three stock menu functions (direct BL, addresses from the declaration). */
#include <stdint.h>
#include "../gate_api.h"
#include "../sites/video_refresh.h"
typedef uint32_t u32;typedef uint64_t u64;
#ifndef Z63_TRIAL_ITEM
#error "the trial row comes from the declaration"
#endif
#ifndef Z63_TRIAL_SLOT
#error "the text slot comes from the declaration (TEXT_SLOTS registry)"
#endif
#define TRIAL_WIDGET 0x4dc0u
void *stock_row_value(void *window,u32 item,u32 widget);
void stock_text_store(const char *text,u32 slot,u32 style);
void stock_text_bind(void *widget,u32 slot);
void z63h_video_refresh_g1(u64 page,u32 phase,void *window) {
    static const char armed[]="Z63 ARMED";
    static const char draining[]="Z63 OFF*";
    (void)page;
    stock_text_store(phase==Z63_LIVE?armed:draining,Z63_TRIAL_SLOT,4);
    void *w=stock_row_value(window,Z63_TRIAL_ITEM,TRIAL_WIDGET);
    if(w)stock_text_bind(w,Z63_TRIAL_SLOT);
}
