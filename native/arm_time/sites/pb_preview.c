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
/* Shared site "pb_preview": the 17 BL call sites of the paused-movie preview 0x408250C8(request, source).
 * Declared by z63patch/gate_groups/sites/pb_preview.py; included into the gate core through the
 * generated gate_sites.inc when a group of the composition contributes. One merged adapter for
 * every record of the site: the gate lookup first, the contributors' phases, the pre hooks in the
 * site's order, then the stock call exactly once with the caller's argument registers x0-x7.
 * Hook signatures: sites/pb_preview.h. */
#include "pb_preview.h"
__asm__("Z63_STOCK stock_pb_preview,0x50c8,0x4082\n");
u64 stock_pb_preview(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7);
#define Z63_X_DECLARE_PRE(g,hook) void hook(u64 page,u32 phase,void *request,const void *source);
Z63_SITE_PB_PREVIEW_PRE(Z63_X_DECLARE_PRE)
u64 z63_site_pb_preview(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=gate_page();
    #define Z63_X_PHASE_PRE(g,hook) u32 ph_##hook=site_phase(p,g);
    Z63_SITE_PB_PREVIEW_PRE(Z63_X_PHASE_PRE)
#define Z63_X_PRE(g,hook) if(ph_##hook)hook(p,ph_##hook,(void *)a0,(const void *)a1);
    if(p) {Z63_SITE_PB_PREVIEW_PRE(Z63_X_PRE)}
    __asm__ volatile("":::"memory");
    return stock_pb_preview(a0,a1,a2,a3,a4,a5,a6,a7);
}
/* The X macros are per site: several sites are included into one core. */
#undef Z63_X_DECLARE_PRE
#undef Z63_X_PHASE_PRE
#undef Z63_X_PRE
