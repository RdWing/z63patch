# SPDX-License-Identifier: AGPL-3.0-only
# SPDX-FileCopyrightText: 2026 RdWing
#
# z63patch - Nikon Z6III Modifications
# AGPLv3 Open Source. Use is subject to license terms.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# @package z63patch
# @license AGPLv3 License (https://opensource.org/licenses/AGPL-3.0)
#
#   Copyright (C) 2026 RdWing
#
"""Shared site "pb_preview": the 17 BL call sites of the paused-movie preview 0x408250C8(request,
source). Hook signatures and order are fixed here and in native/arm_time/sites/pb_preview.h
(R-PLAY-1 (a)): G10 first (an open-gate file's
full page gets the whole 3:2 canvas, a 3:2 thumbnail loses its crop byte: needs the per-file
lookup, so it cannot be a peer-state read in G7), then G7 (the desqueeze squeeze of the 1920x1080
preview), both before the stock call, as the old combined entry hook did. Any subset works."""
from .. import Site

PREVIEW_SITES = (0x408170B0, 0x40817E50, 0x40818C74, 0x40819F98, 0x4081A1AC, 0x4081BF24, 0x4081C824, 0x4081CC54,
                 0x4081D7D4, 0x4081DAB8, 0x4081DBA8, 0x4081EBFC, 0x4081ECDC, 0x408208C4, 0x40820AB4, 0x408287B0,
                 0x4082AFE8)

SITE = Site(
    name='pb_preview', kind='code32', address=0x0, old=0x0,
    adapter='z63_site_pb_preview', source='native/arm_time/sites/pb_preview.c',
    cls='A', wrapper='C', set='', rank=0, needs_q=False,
    stock_calls={'stock_pb_preview': 0x408250c8},
    order=(('pre', 10), ('pre', 7)),
    addresses=PREVIEW_SITES, callee=0x408250c8,
    purpose='paused-movie preview call sites -> bl z63_site_pb_preview (G10 pre, G7 pre, stock preview once)')
