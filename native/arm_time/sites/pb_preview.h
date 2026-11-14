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
/* Shared site "pb_preview" (z63patch/gate_groups/sites/pb_preview.py): the hook signatures, defined once.
 * A contributor includes this header and defines its hook with exactly this prototype; the core
 * adapter (pb_preview.c) includes it too.
 *
 * Role "pre", order G10, G7, BEFORE the stock preview 0x408250C8(request, source): each
 * contributor in LIVE or DRAINING may edit the request in place (G10: the open-gate full page /
 * thumbnail crop; G7: the desqueeze squeeze of the destination). The adapter then calls the
 * stock preview once with the caller's x0-x7 and returns its result. */
#ifndef Z63_SITE_PB_PREVIEW_H
#define Z63_SITE_PB_PREVIEW_H
#include <stdint.h>
void z63h_pb_preview_g10(uint64_t page,uint32_t phase,void *request,const void *source);   /* role pre */
void z63h_pb_preview_g7(uint64_t page,uint32_t phase,void *request,const void *source);    /* role pre */
#endif
