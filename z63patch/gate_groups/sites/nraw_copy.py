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
"""Shared site "nraw_copy": the three BL call sites of the N-RAW rate-row copy 0x40D22220(request, row). Hook signatures and order are fixed here and in
native/arm_time/sites/nraw_copy.h; any subset of the contributors works."""
from .. import Site

SITE = Site(
    name='nraw_copy', kind='code32', address=0x0, old=0x0,
    adapter='z63_site_nraw_copy', source='native/arm_time/sites/nraw_copy.c',
    cls='A', wrapper='C', set='', rank=0, needs_q=True,
    stock_calls={'stock_nraw_copy': 0x40d22220},
    order=(('copy', 6), ('copy', 10)),
    addresses=(0x40d20de8, 0x40d22184, 0x40d253cc), callee=0x40d22220,
    purpose='N-RAW rate-row copy call sites -> bl z63_site_nraw_copy (stock copy once, then G6, G10)')
