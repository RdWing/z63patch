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
"""Shared site "lv_display": the tail branch 0x40EA9934 (b 0x40F107EC) of the LV display thunk 0x40EA9930(self, flag). Hook signatures and order are fixed here and in
native/arm_time/sites/lv_display.h; any subset of the contributors works."""
from .. import Site, b_word

SITE = Site(
    name='lv_display', kind='code32', address=0x40ea9934, old=b_word(0x40ea9934, 0x40f107ec),
    adapter='z63_site_lv_display', source='native/arm_time/sites/lv_display.c',
    cls='A', wrapper='C', set='lv_display.view', rank=1, needs_q=True,
    stock_calls={'stock_lv_display': 0x40f107ec},
    order=(('pre', 9), ('post', 10)),
    link=False,
    purpose='LV display thunk tail 0x40EA9934 -> b z63_site_lv_display (G9 pre, stock builder once, G10 post)')
