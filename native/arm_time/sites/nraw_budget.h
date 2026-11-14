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
/* Shared site "nraw_budget" (z63patch/gate_groups/sites/nraw_budget.py): the hook signatures, defined once.
 * A contributor includes this header and defines its hook with exactly this prototype; the core
 * adapter (nraw_budget.c) includes it too.
 *
 * Role "decide", order G10, G6, BEFORE the stock call: each contributor in LIVE or DRAINING
 * receives the tier decided so far (Z63_TIER_NONE when none) and returns the tier it leaves
 * decided; a contributor that sees a decision already made must leave it. G10 decides first.
 * (G10 cannot call G6: it derives the tier from the published word itself.) The adapter then
 * calls the stock budget once with the original arguments and returns its result. */
#ifndef Z63_SITE_NRAW_BUDGET_H
#define Z63_SITE_NRAW_BUDGET_H
#include <stdint.h>
#ifndef Z63_TIER_NONE
#define Z63_TIER_NONE 0xffffffffu
#endif
uint32_t z63h_nraw_budget_g10(uint64_t page,uint32_t phase,void *request,uint32_t decided);   /* role decide */
uint32_t z63h_nraw_budget_g6(uint64_t page,uint32_t phase,void *request,uint32_t decided);   /* role decide */
#endif
