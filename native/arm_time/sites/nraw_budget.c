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
/* Shared site "nraw_budget": the two BL call sites of the N-RAW budget 0x40E67D0C(request).
 * Declared by z63patch/gate_groups/sites/nraw_budget.py; included into the gate core through the
 * generated gate_sites.inc when a group of the composition contributes. One merged adapter for
 * every record of the site: the gate lookup first, the contributors' phases, the stock call
 * exactly once with the caller's argument registers x0-x7, the hooks in the site's order.
 * Hook signatures: sites/nraw_budget.h. */
#include "nraw_budget.h"
__asm__("Z63_STOCK stock_nraw_budget,0x7d0c,0x40e6\n");
u64 stock_nraw_budget(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7);
#define Z63_X_DECLARE_DECIDE(g,hook) u32 hook(u64 page,u32 phase,void *request,u32 decided);
Z63_SITE_NRAW_BUDGET_DECIDE(Z63_X_DECLARE_DECIDE)
#ifdef Z63_NRAW_BUDGET_CAP
/* R-G10-2: the composition's pass cap (Group.pass_cap; only G10's
 * og_budget_cap), called in every phase, also with no page. Verifier: no gate or published-word
 * access, no call, no constant address but its declared table, at most one store, of +0xB8. */
u32 Z63_NRAW_BUDGET_CAP(void *request);
#endif
u64 z63_site_nraw_budget(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=gate_page();
    #define Z63_X_PHASE_DECIDE(g,hook) u32 ph_##hook=site_phase(p,g);
    Z63_SITE_NRAW_BUDGET_DECIDE(Z63_X_PHASE_DECIDE)
    u32 decided=Z63_TIER_NONE;
#define Z63_X_BEFORE2(g,hook) if(ph_##hook)decided=hook(p,ph_##hook,(void *)a0,decided);
    if(p) {Z63_SITE_NRAW_BUDGET_DECIDE(Z63_X_BEFORE2)}
    (void)decided;
#ifdef Z63_NRAW_BUDGET_CAP
    (void)Z63_NRAW_BUDGET_CAP((void *)a0);
#endif
    __asm__ volatile("":::"memory");
    return stock_nraw_budget(a0,a1,a2,a3,a4,a5,a6,a7);
}
/* The X macros are per site: several sites are included into one core. */
#undef Z63_X_DECLARE_DECIDE
#undef Z63_X_PHASE_DECIDE
#undef Z63_X_BEFORE2
