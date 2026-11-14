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
/* Shared site "nraw_copy" (z63patch/gate_groups/sites/nraw_copy.py): the hook signatures, defined once.
 * A contributor includes this header and defines its hook with exactly this prototype; the core
 * adapter (nraw_copy.c) includes it too.
 *
 * Role "copy", order G6, G10: after the stock copy, each contributor whose phase is unchanged
 * receives the tier applied so far (Z63_TIER_NONE when none) and returns the tier it leaves
 * applied (unchanged when it did nothing). The adapter returns the stock result.
 * Stock 0x40D22220(x0 = request, x1 = row): x0 is the request record (type, size, rate, quality)
 * the copy reads; x1 is the 24-byte output rate row it writes. A hook reads `request` and
 * writes `row`. */
#ifndef Z63_SITE_NRAW_COPY_H
#define Z63_SITE_NRAW_COPY_H
#include <stdint.h>
#ifndef Z63_TIER_NONE
#define Z63_TIER_NONE 0xffffffffu
#endif
uint32_t z63h_nraw_copy_g6(uint64_t page,uint32_t phase,void *request,void *row,uint32_t applied);   /* role copy */
uint32_t z63h_nraw_copy_g10(uint64_t page,uint32_t phase,void *request,void *row,uint32_t applied);   /* role copy */
#endif
