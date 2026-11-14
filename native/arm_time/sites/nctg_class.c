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
/* Shared site "nctg_class": NCTG descriptor #15 metadata-class getter pointer 0xA02993530 (stock 0x40707038(request, out)), read live by 0x407067A4.
 * Declared by z63patch/gate_groups/sites/nctg_class.py; included into the gate core through the
 * generated gate_sites.inc when a group of the composition contributes. One merged adapter for
 * every record of the site: the gate lookup first, the contributors' phases, the stock call
 * exactly once with the caller's argument registers x0-x7, the hooks in the site's order.
 * Hook signatures: sites/nctg_class.h. */
#include "nctg_class.h"
__asm__("Z63_STOCK stock_nctg_class,0x7038,0x4070\n");
u64 stock_nctg_class(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7);
#define Z63_X_DECLARE_CLS(g,hook) u32 hook(u64 page,u32 phase,void *request,void *out,u32 result);
Z63_SITE_NCTG_CLASS_CLS(Z63_X_DECLARE_CLS)
u64 z63_site_nctg_class(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4,u64 a5,u64 a6,u64 a7) {
    u64 p=gate_page();
    #define Z63_X_PHASE_CLS(g,hook) u32 ph_##hook=site_phase(p,g);
    Z63_SITE_NCTG_CLASS_CLS(Z63_X_PHASE_CLS)
    u64 r=stock_nctg_class(a0,a1,a2,a3,a4,a5,a6,a7);
    u32 res=(u32)r;
#define Z63_X_AFTER(g,hook) if(site_same(p,g,ph_##hook))res=hook(p,ph_##hook,(void *)a0,(void *)a1,res);
    if(p) {Z63_SITE_NCTG_CLASS_CLS(Z63_X_AFTER)}
    __asm__ volatile("":::"memory");
    return res==(u32)r?r:(u64)res;
}
/* The X macros are per site: several sites are included into one core. */
#undef Z63_X_DECLARE_CLS
#undef Z63_X_PHASE_CLS
#undef Z63_X_AFTER
