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
#ifndef Z63_NV_QUALITY_H
#define Z63_NV_QUALITY_H
#include <stdint.h>
/* Low byte remains Nikon's one-byte setting. Three descriptor-unowned bytes
 * hold version, bounded quality and CRC8; no playback value is persisted. */
int z63_nv_decode(uint32_t word, uint32_t *quality);
int z63_nv_encode(uint32_t before, uint32_t quality, uint32_t *after);
#endif
