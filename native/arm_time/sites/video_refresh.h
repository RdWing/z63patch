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
/* Shared site "video_refresh" (z63patch/gate_groups/sites/video_refresh.py): the hook
 * signature, defined once. A contributor includes this header and defines its hook with exactly
 * this prototype; the core adapter (video_refresh.c) includes it too.
 *
 * Role "after", order G1, G2: each runs after the stock refresh 0x40596140(window), only when the
 * contributor's phase (z63g_phase, taken before the stock call) is LIVE or DRAINING and is still
 * the same after it. Returns nothing; the adapter returns the stock result. */
#ifndef Z63_SITE_VIDEO_REFRESH_H
#define Z63_SITE_VIDEO_REFRESH_H
#include <stdint.h>
void z63h_video_refresh_g1(uint64_t page,uint32_t phase,void *window);
void z63h_video_refresh_g2(uint64_t page,uint32_t phase,void *window);
#endif
