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
"""G5, H.265 quality tiers.

Two independent records, each its own set of one (plan 5.2): the selector tail and the rate
threshold. The tier comes from the settings word the gate publishes (z63_effective); no menu
(the tier is set by G2 later, or stored already). Requires only the infrastructure: G2 does not
exist yet (the plan's rule "every feature group requires G2" applies once it does)."""
from . import Group, Code, bl_word, b_word

THRESHOLD_CALL = 0x402FF284      # bl RATE_LEAF; the old value site 0x402FF294 stays stock
RATE_LEAF = 0x402FFA48           # frameless: w0 = table[w0] (0..11) or 0; clobbers x8, NZCV
OLD_THRESHOLD = 0x402FF294

# Reviewed composed stages this group introduces (profiles/z6iii-c201-arm-time-<stage>-recipe.json).
STAGES = {'s7-h265': ('marker', 'h265')}

GROUP = Group(
    id=5, name='h265', title='H.265 quality tiers',
    source='native/arm_time/groups/h265.c',
    slot=(0x401C5000, 0x401C7000),
    records=(
        Code(address=0x402FBFCC, old=b_word(0x402FBFCC, 0x4163B5C0), symbol='z63_g5_select', link=False, cls='A', wrapper='C',
             set='selector', rank=1, needs_q=True, stores=True,
             purpose='H.265 selector tail: b memcpy 0x4163B5C0 -> b z63_g5_select (stock copy, then the stored tier on the 36-byte row)'),
        # The BL two words before the old value site 0x402FF294 (lsr w9,w8,#7, now stock).
        Code(address=THRESHOLD_CALL, old=bl_word(THRESHOLD_CALL, RATE_LEAF), symbol='z63_g5_threshold', link=True, cls='Q', wrapper='C',
             set='threshold', rank=1, needs_q=True, stores=False,
             purpose='H.265 rate threshold: bl 0x402FFA48 -> bl z63_g5_threshold (stock call, then the divisor 800 '
                     'above 200,000 kbit/s for H.265 applied to the budgets w27/w24 the two udivs read)'),
    ),
    stock_calls={'stock_memcpy': 0x4163B5C0, 'stock_rate_leaf': RATE_LEAF},
    replay_tests=('tests/test_arm_time_s7_h265.py',),
    movie_publish=True)   # The tier is read under ACT on the fresh row copy; no read while recording
