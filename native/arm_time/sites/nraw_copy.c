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
/* Shared site "nraw_copy": the three BL call sites of the N-RAW rate-row copy 0x40D22220(request, row).
 * Declared by z63patch/gate_groups/sites/nraw_copy.py; included into the gate core through the
 * generated gate_sites.inc when a group of the composition contributes. One merged adapter for
 * every record of the site: the gate lookup first, the contributors' phases, the stock call
 * exactly once with the caller's argument registers x0-x7, the hooks in the site's order.
 * Hook signatures: sites/nraw_copy.h. */
#include "nraw_copy.h"
__asm__("Z63_STOCK stock_nraw_copy,0x2220,0x40d2\n");
u64 stock_nraw_copy(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7);
#define Z63_X_DECLARE_COPY(g,hook) u32 hook(u64 page,u32 phase,void *request,void *row,u32 applied);
Z63_SITE_NRAW_COPY_COPY(Z63_X_DECLARE_COPY)
u64 z63_site_nraw_copy(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=gate_page();
    #define Z63_X_PHASE_COPY(g,hook) u32 ph_##hook=site_phase(p,g);
    Z63_SITE_NRAW_COPY_COPY(Z63_X_PHASE_COPY)
    u64 r=stock_nraw_copy(a0,a1,a2,a3,a4,a5,a6,a7);
    u32 applied=Z63_TIER_NONE;
#define Z63_X_AFTER(g,hook) if(site_same(p,g,ph_##hook))applied=hook(p,ph_##hook,(void *)a0 /* x0: request */,(void *)a1 /* x1: row */,applied);
    if(p) {Z63_SITE_NRAW_COPY_COPY(Z63_X_AFTER)}
    (void)applied;
    __asm__ volatile("":::"memory");
    return r;
}
/* The X macros are per site: several sites are included into one core. */
#undef Z63_X_DECLARE_COPY
#undef Z63_X_PHASE_COPY
#undef Z63_X_AFTER
