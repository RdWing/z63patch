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
"""Shared site "nctg_class": NCTG descriptor #15 metadata-class getter pointer 0xA02993530 (stock 0x40707038(request, out)), read live by 0x407067A4. Hook signatures and order are fixed here and in
native/arm_time/sites/nctg_class.h; any subset of the contributors works."""
from .. import Site

SITE = Site(
    name='nctg_class', kind='data64', address=0xa02993530, old=0x40707038,
    adapter='z63_site_nctg_class', source='native/arm_time/sites/nctg_class.c',
    cls='A', wrapper='C', set='nctg_class.class', rank=1, needs_q=True,
    stock_calls={'stock_nctg_class': 0x40707038},
    order=(('cls', 5), ('cls', 4)),
    purpose='NCTG class getter pointer 0xA02993530 -> z63_site_nctg_class (stock getter once, then G5, G4)')
