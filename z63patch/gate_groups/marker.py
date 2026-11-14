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
"""G1, the visible marker ("Z63 ARMED" in LIVE, "Z63 OFF*" in DRAINING, the stock value otherwise),
as in S5M/S6, as a contributor of the shared site video_refresh. Text slot 285 (TEXT_SLOTS registry;
moved off 253, which G2 uses for the quality row value).

Row: the value of the video-menu row item 0x31ED (stock row 34, the
microphone input "MIC"/"LINE", drawn by the stock refresh at 0x40596CF8..0x40596D60 with TEXT_STORE
slot 0x4E style 4 and TEXT_BIND on its value widget 0x4DC0, the same mechanism as the N-RAW row the
S3-S6 marker used). Moved off 0x31B5 (N-RAW quality), where G6 draws FINE/BASIC and next to which
G2 inserts its rows. No feature group reads, binds or replaces anything of row 0x31ED
(tests/test_arm_time_s7_integration.py checks every declared group). While the marker shows, the
microphone input value is hidden; it is drawn again by the stock refresh after STOCK. On screen:
not yet seen in the camera emulator (the S5m/S6 runs used 0x31B5)."""
from . import Group, Shared, MARKER

GROUP = Group(
    id=MARKER, name='marker', title='visible marker (trial)',
    source='native/arm_time/groups/marker.c',
    slot=(0x401B6000, 0x401B7000),
    shared=(Shared(site='video_refresh', hook='z63h_video_refresh_g1'),),
    stock_calls={'stock_row_value': 0x4052C900, 'stock_text_store': 0x404066C8, 'stock_text_bind': 0x4040A1AC},
    defines=('-DZ63_TRIAL_ITEM=0x31edu', '-DZ63_TRIAL_SLOT=285u'),
    text_slots=(285,),
    replay_tests=('tests/test_arm_time_s7.py',),
    hook_stores=False,
    movie_publish=True)   # Never reads the published word
