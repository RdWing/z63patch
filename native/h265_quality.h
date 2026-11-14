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
#ifndef Z63_H265_QUALITY_H
#define Z63_H265_QUALITY_H
#include <stdint.h>
/* Apply a tier to a fresh copy of a C2.01 stock row. The target adapter must
 * supply its effective clip snapshot and call this once, after stock memcpy.
 * No target hook is installed by this portable unit. */
int z63_h265_quality(uint32_t row_index, uint32_t tier, uint32_t *row, uint32_t bytes);
uint32_t z63_h265_menu_index(uint32_t file_type, uint32_t size, uint32_t rate);
int z63_h265_label(uint32_t tier, uint32_t nominal_bps, char *out, uint32_t capacity);
#ifdef Z63_H265_TARGET_LABELS
/* "<share>%  <m> Mbps" and "<m>M" with m = ((bps / 1200) + 500) / 1000. */
int z63_h265_target_label(uint32_t tier, uint32_t bps, char *out, uint32_t capacity);
int z63_h265_target_value(uint32_t bps, char *out, uint32_t capacity);
#endif
#endif
