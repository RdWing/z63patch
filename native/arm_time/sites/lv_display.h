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
/* Shared site "lv_display" (z63patch/gate_groups/sites/lv_display.py): the hook signatures, defined once.
 * A contributor includes this header and defines its hook with exactly this prototype; the core
 * adapter (lv_display.c) includes it too.
 *
 * A call-and-return adapter replacing the tail branch (b -> b): role "pre" (G9) runs before the
 * stock builder 0x40F107EC, role "post" (G10) after it, each only in LIVE or DRAINING (a post
 * hook only when its phase is unchanged across the call). The stock builder takes no stack
 * arguments, so call-and-return is equivalent. The adapter returns the stock
 * result to the thunk's caller. */
#ifndef Z63_SITE_LV_DISPLAY_H
#define Z63_SITE_LV_DISPLAY_H
#include <stdint.h>
#ifndef Z63_TIER_NONE
#define Z63_TIER_NONE 0xffffffffu
#endif
void z63h_lv_display_g9(uint64_t page,uint32_t phase,void *self,uint32_t flag);   /* role pre */
void z63h_lv_display_g10(uint64_t page,uint32_t phase,void *self,uint32_t flag,uint64_t result);   /* role post */
#endif
