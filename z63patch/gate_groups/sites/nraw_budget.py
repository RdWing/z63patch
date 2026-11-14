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
"""Shared site "nraw_budget": the two BL call sites of the N-RAW budget 0x40E67D0C(request). Hook signatures and order are fixed here and in
native/arm_time/sites/nraw_budget.h; any subset of the contributors works."""
from .. import Site

SITE = Site(
    name='nraw_budget', kind='code32', address=0x0, old=0x0,
    adapter='z63_site_nraw_budget', source='native/arm_time/sites/nraw_budget.c',
    cls='A', wrapper='C', set='', rank=0, needs_q=True,
    stock_calls={'stock_nraw_budget': 0x40e67d0c},
    order=(('decide', 10), ('decide', 6)),
    addresses=(0x40e678c4, 0x40e67a4c), callee=0x40e67d0c,
    purpose='N-RAW budget call sites -> bl z63_site_nraw_budget (G10, G6 decide, then the stock budget once)')
