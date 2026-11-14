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
/* Shared site "lv_display": the tail branch 0x40EA9934 (b 0x40F107EC) of the LV display thunk 0x40EA9930(self, flag).
 * Declared by z63patch/gate_groups/sites/lv_display.py; included into the gate core through the
 * generated gate_sites.inc when a group of the composition contributes. One merged adapter for
 * every record of the site: the gate lookup first, the contributors' phases, the stock call
 * exactly once with the caller's argument registers x0-x7, the hooks in the site's order.
 * Hook signatures: sites/lv_display.h. */
#include "lv_display.h"
__asm__("Z63_STOCK stock_lv_display,0x7ec,0x40f1\n");
u64 stock_lv_display(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7);
#define Z63_X_DECLARE_PRE(g,hook) void hook(u64 page,u32 phase,void *self,u32 flag);
Z63_SITE_LV_DISPLAY_PRE(Z63_X_DECLARE_PRE)
#define Z63_X_DECLARE_POST(g,hook) void hook(u64 page,u32 phase,void *self,u32 flag,u64 result);
Z63_SITE_LV_DISPLAY_POST(Z63_X_DECLARE_POST)
u64 z63_site_lv_display(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=gate_page();
    #define Z63_X_PHASE_PRE(g,hook) u32 ph_##hook=site_phase(p,g);
    Z63_SITE_LV_DISPLAY_PRE(Z63_X_PHASE_PRE)
#define Z63_X_PHASE_POST(g,hook) u32 ph_##hook=site_phase(p,g);
    Z63_SITE_LV_DISPLAY_POST(Z63_X_PHASE_POST)
#define Z63_X_PRE(g,hook) if(ph_##hook)hook(p,ph_##hook,(void *)a0,(u32)a1);
    if(p) {Z63_SITE_LV_DISPLAY_PRE(Z63_X_PRE)}
    u64 r=stock_lv_display(a0,a1,a2,a3,a4,a5,a6,a7);
#define Z63_X_POST(g,hook) if(site_same(p,g,ph_##hook))hook(p,ph_##hook,(void *)a0,(u32)a1,r);
    if(p) {Z63_SITE_LV_DISPLAY_POST(Z63_X_POST)}
    __asm__ volatile("":::"memory");
    return r;
}
/* The X macros are per site: several sites are included into one core. */
#undef Z63_X_DECLARE_PRE
#undef Z63_X_DECLARE_POST
#undef Z63_X_PHASE_PRE
#undef Z63_X_PHASE_POST
#undef Z63_X_PRE
#undef Z63_X_POST
