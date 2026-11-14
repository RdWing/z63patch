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
"""G9, Log view assist per panel and Log playback.

Eleven records of the group plus the pre hook of the shared site lv_display (0x40EA9934, G10 has
the post hook; either works alone). Every record is a B->B or BL->BL swap of a stock branch word
(A class), restored under Q as the default. Departures from the design, each because the
verifier or the stock image requires it (see native/arm_time/groups/viewassist.c):

 - the playback detail label moves from 0x4067CC00 (mov w9,#0x2d4e, a non-branch word with two
   exits, which the verifier refuses) to the label copy 0x4067CC90 (bl 0x4163BBB0), so no record
   needs Q at install;
 - requires=(8,) (plan rule; G8 brings G2 and G3). The code still
   reads the Log type from the published settings word only while G8 is selected and live, so a
   session where G8 drops at arming behaves as stock N-Log for recorded settings;
 - a sanitiser entry (the design needs none) moves the cached LUT index CB 0xA02FA4FA1 from a
   patch marker 0x81 / 0x82 to 0xFF at restore, so stock re-uploads its own tables even if the
   current index were ever 0x81 / 0x82 (belt and braces; width 2, the aligned half-word
   0xA02FA4FA0 whose low byte stock zeroes at init 0x402D397C and never writes otherwise).

stores=True on the two selects: their only stores outside the adapter frame are the ACT count
(taken and released) and the settings decoder's output struct in the helper's own stack, which
the verifier cannot tell from a pointer store."""
from . import Group, Code, Shared, Sanitise, Asset, bl_word, b_word, PLAN_ASSETS

STAGES = {'s7-viewassist': ('marker', 'menu', 'indicators', 'log', 'viewassist')}

CACHE_A, CACHE_B = 0x402D4E24, 0x402DDB30
PB_COPY, PB_BUILD, LABEL_COPY = 0x404106E8, 0x407FA12C, 0x4163BBB0
CB_HALF = 0xA02FA4FA0
# The view-assist tables need a read-only asset (their curve words decode as branches out of the
# code, which the verifier refuses in a code slot): PLAN_ASSETS[9] = ((0x401DB000, 0x401DC000),),
# the last 4 KiB of G9's own reserved slot (in the registry).
# Without that line the group would build as the marked STUB: no tables, the patch
# pointers naming the stock N-Log records (stock assist).
ASSET = (0x401DB000, 0x401DC000)
HAS_ASSET = ASSET in PLAN_ASSETS.get(9, ())


def _bl(at, callee, symbol, purpose):
    return Code(address=at, old=bl_word(at, callee), symbol=symbol, link=True, cls='A', wrapper='C',
                stores=True, purpose=purpose)


GROUP = Group(
    id=9, name='viewassist', title='Log view assist per panel and Log playback',
    source='native/arm_time/groups/viewassist.c',
    slot=(0x401D8000, 0x401DB000),
    assets=(Asset(lo=ASSET[0], hi=ASSET[1], purpose='view-assist matrices, curves B and pointer tables (0x300 B)'),) if HAS_ASSET else (),
    defines=('-DZ63_G9_ASSET',) if HAS_ASSET else ('-DZ63_G9_STUB_TABLES',),
    records=(
        Code(address=0x402D8F7C, old=b_word(0x402D8F7C, 0x402D8ED0), symbol='z63_g9_matrix', link=False, cls='A', wrapper='C', stores=True,
             purpose='view-assist matrix select, mode 1: b 0x402D8ED0 -> b z63_g9_matrix (x10 = the Log type\'s matrix table)'),
        Code(address=0x402D91D0, old=b_word(0x402D91D0, 0x402D9060), symbol='z63_g9_curves', link=False, cls='A', wrapper='C', stores=True,
             purpose='view-assist curve select, mode 1: b 0x402D9060 -> b z63_g9_curves (x10 matrix, x12 curve-B table)'),
        _bl(0x402D4D38, CACHE_A, 'z63_g9_cache_a', 'LUT cache call A: bl 0x402D4E24 -> bl z63_g9_cache_a (CB marker 0x80|type)'),
        _bl(0x402DF580, CACHE_A, 'z63_g9_cache_a', 'LUT cache call A: bl 0x402D4E24 -> bl z63_g9_cache_a (CB marker 0x80|type)'),
        _bl(0x402DA7A4, CACHE_B, 'z63_g9_cache_b', 'LUT cache call B: bl 0x402DDB30 -> bl z63_g9_cache_b (CB marker 0x80|type)'),
        _bl(0x402DB6FC, CACHE_B, 'z63_g9_cache_b', 'LUT cache call B: bl 0x402DDB30 -> bl z63_g9_cache_b (CB marker 0x80|type)'),
        _bl(0x406B41A0, PB_COPY, 'z63_g9_pb_copy', 'playback info copy: bl 0x404106E8 -> bl z63_g9_pb_copy (classify 0xA03439CA0)'),
        _bl(0x407FA000, PB_BUILD, 'z63_g9_pb_build', 'playback info builder call: bl 0x407FA12C -> bl z63_g9_pb_build (classify x2)'),
        _bl(0x407FB7FC, PB_BUILD, 'z63_g9_pb_build', 'playback info builder call: bl 0x407FA12C -> bl z63_g9_pb_build (classify x2)'),
        Code(address=0x40704940, old=b_word(0x40704940, 0x407047C0), symbol='z63_g9_parse', link=False, cls='A', wrapper='C', stores=True,
             purpose='NCTG 0x2000023 parse tail: b 0x407047C0 -> b z63_g9_parse (custom Log clip: tone tag 1 -> 2)'),
        _bl(0x4067CC90, LABEL_COPY, 'z63_g9_label', 'playback detail label copy: bl 0x4163BBB0 -> bl z63_g9_label (custom Log name over "N-Log")'),
    ),
    shared=(Shared(site='lv_display'),),
    hook_stores=True,
    requires=(8,),
    stock_calls={'stock_cache_a': CACHE_A, 'stock_cache_b': CACHE_B, 'stock_pb_copy': PB_COPY,
                 'stock_pb_build': PB_BUILD, 'stock_label_copy': LABEL_COPY,
                 'z63_g9_back_matrix': 0x402D8ED0, 'z63_g9_back_curves': 0x402D9060, 'z63_g9_back_parse': 0x407047C0},
    state=(0x8C8, 8),
    sanitise=(Sanitise(address=CB_HALF, width=2, stock=0xFF00, match_lo=0x8100, match_hi=0x8101,
                       purpose='CB 0xA02FA4FA1 = 0x81 (S-Log3 tables marked in the hardware) -> 0xFF: stock re-uploads its own tables'),
              Sanitise(address=CB_HALF, width=2, stock=0xFF00, match_lo=0x8200, match_hi=0x8201,
                       purpose='CB 0xA02FA4FA1 = 0x82 (Log3G10 tables marked in the hardware) -> 0xFF: stock re-uploads its own tables')),
    replay_tests=('tests/test_arm_time_s7_viewassist.py',),
    # The published word only chooses the monitor's assist tables; nothing of a recording
    # depends on it, and a change in idle movie view takes effect at the next LUT upload.
    movie_publish=True)
