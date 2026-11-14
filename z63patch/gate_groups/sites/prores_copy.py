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
"""Shared site "prores_copy": the four BL call sites of the ProRes rate-row copy 0x40D223B4(request, row). Hook signatures and order are fixed here and in
native/arm_time/sites/prores_copy.h; any subset of the contributors works."""
from .. import Site

SITE = Site(
    name='prores_copy', kind='code32', address=0x0, old=0x0,
    adapter='z63_site_prores_copy', source='native/arm_time/sites/prores_copy.c',
    cls='A', wrapper='C', set='', rank=0, needs_q=True,
    stock_calls={'stock_prores_copy': 0x40d223b4},
    order=(('copy', 4), ('copy', 10)),
    addresses=(0x40d20dd8, 0x40d22178, 0x40d22834, 0x40d253b0), callee=0x40d223b4,
    purpose='ProRes rate-row copy call sites -> bl z63_site_prores_copy (stock copy once, then G4, G10)')
