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
/* Shared site "nctg_class" (z63patch/gate_groups/sites/nctg_class.py): the hook signatures, defined once.
 * A contributor includes this header and defines its hook with exactly this prototype; the core
 * adapter (nctg_class.c) includes it too.
 *
 * Role "cls", order G5, G4: after the stock getter, each contributor whose phase is unchanged
 * receives the result so far and returns the result to pass on (unchanged = stock). The adapter
 * returns the stock x0 when the chain left the 32-bit result unchanged, else the chain's result. */
#ifndef Z63_SITE_NCTG_CLASS_H
#define Z63_SITE_NCTG_CLASS_H
#include <stdint.h>
#ifndef Z63_TIER_NONE
#define Z63_TIER_NONE 0xffffffffu
#endif
uint32_t z63h_nctg_class_g5(uint64_t page,uint32_t phase,void *request,void *out,uint32_t result);   /* role cls */
uint32_t z63h_nctg_class_g4(uint64_t page,uint32_t phase,void *request,void *out,uint32_t result);   /* role cls */
#endif
