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
"""G6, N-RAW quality tiers.

Nothing is written at arming. The stored tier t (settings word bits 18-19,
native/nraw_quality.h order: 0 High, 1 Fine, 2 Normal, 3 Basic; Fine belongs to Nikon's High
class, Basic to Normal) applies at a site only when its two-class projection (t < 2 = High)
equals Nikon's quality class in the request that site sees; otherwise the site behaves exactly
as stock ("inherit Nikon" until the user picks a tier through G2). Only t = 1 with Nikon High and
t = 3 with Nikon Normal change anything (t = 0 and t = 2 give the stock High and Normal rows).

Seven records, all BL -> BL:
 - shared site nraw_copy (3 call sites of the rate-row copy 0x40D22220), hook z63h_nraw_copy_g6
   (first in the site's order, before G10): after the stock copy, the Fine/Basic row;
 - shared site nraw_budget (2 call sites of the budget 0x40E67D0C), hook z63h_nraw_budget_g6
   (after G10; a tier G10 decided is left): request +0xB8 index 2 -> 1 (Fine) or 7 -> 0 (Basic);
 - menu text 0x40596350 (bl TEXT_STORE 0x404066C8) and menu value 0x40596308 (bl SETTING_GET
   0x403FFCD0, key 0x05000F55) inside the stock video-menu refresh 0x40596140: the N-RAW quality
   row shows FINE / BASIC for a consistent Fine / Basic tier.
Restore callback z63r_g6: during RESTORING, under Q, the global rate
row 0xA03325C10 is put back from a Fine/Basic row of a supported pair to that pair's High/Normal
bits/s by one bounded CAS of its u64 at +16 (the only store); any other content is left alone, and
a row with a pair's geometry but an unknown rate is reported (PARTIAL). No state, no save, no
settings-word write. The bank projection record 0x41004718 and the
metadata/proxy getter adapters (0xA02993530 / 0xA02993870) are dropped.
Movie-view publication: movie_publish=True with two mechanisms.
 - Anchor reads (value word at state +0x9A0): the copy into the global row (movie-subsystem
   start) and the budget decision (encoder setup) take the composition's anchor hold; while it is
   held (until 8 consecutive native-idle polls with no anchor read) nothing publishes, so the
   reads of one recording setup agree. The budget hook also takes back a Fine (1) or Basic (0)
   index an earlier tier left (only the hook writes those), so a descriptor built at movie entry
   is right at the read even when the callback has not run yet.
 - Publication callback z63p_g6: after a publication, with nothing recording and the hold free, the
   global row's bits/s and the TENC descriptor's index (0xA03D2B2E0 +0xB8) are put in line with
   the new tier, as the hooks would have made them; the restore callback also puts a Fine/Basic
   index back to High/Normal. No constant-address read of the global row's bits/s was found in
   MAIN (its consumers read +0..+0xC only), so the row alignment is consistency, not a known
   consumer."""
from . import Group, Code, Shared, bl_word

STAGES = {'s7-nraw': ('marker', 'nraw')}

MENU_TEXT = 0x40596350
MENU_TEXT_OLD = bl_word(MENU_TEXT, 0x404066C8)      # bl 0x404066C8 TEXT_STORE(text, slot 0x4C, 4)
MENU_VALUE = 0x40596308
MENU_VALUE_OLD = bl_word(MENU_VALUE, 0x403FFCD0)    # bl 0x403FFCD0 SETTING_GET(0x05000F55)

GROUP = Group(
    id=6, name='nraw', title='N-RAW quality tiers',
    source='native/arm_time/groups/nraw.c',
    slot=(0x401C7000, 0x401CB000),
    records=(
        Code(address=MENU_TEXT, old=MENU_TEXT_OLD, symbol='z63_g6_menu_text', link=True, cls='A', wrapper='C',
             set='menu', rank=1, needs_q=True, stores=True,
             purpose='N-RAW quality row text: bl TEXT_STORE -> bl z63_g6_menu_text (FINE/BASIC for the empty '
                     'stock label when the stored tier is Fine/Basic and consistent with Nikon\'s class)'),
        Code(address=MENU_VALUE, old=MENU_VALUE_OLD, symbol='z63_g6_menu_value', link=True, cls='A', wrapper='C',
             set='menu', rank=2, needs_q=True, stores=True,
             purpose='N-RAW quality row value: bl SETTING_GET -> bl z63_g6_menu_value (High 1, Fine 2, Normal 0, '
                     'Basic 3 when the tier is consistent with the stock value; else the stock value)'),
    ),
    shared=(Shared(site='nraw_copy'), Shared(site='nraw_budget')),
    stock_calls={'stock_setting_get': 0x403FFCD0, 'stock_text_store': 0x404066C8},
    hook_stores=True,          # the copy hook writes the 24-byte row, the budget hook request +0xB8
    restore=True,              # z63r_g6: the global rate row 0xA03325C10 (and the TENC index) back to stock at restore (R-G6-2)
    state=(0x9A0, 8), hold=0x9A0,   # Anchor value word
    publish=True,              # z63p_g6: global row and TENC index in line with a newly published tier
    replay_tests=('tests/test_arm_time_s7_nraw.py', 'tests/test_arm_time_s7_movie_publish.py'),
    movie_publish=True)
