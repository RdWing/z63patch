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
"""The video menu refresh handler word: one record, one merged adapter, contributors G1 (the
marker) and G2 (the video-menu refresh client). Role
"after": each runs after the stock refresh, G1 then G2. Signature: sites/video_refresh.h."""
from .. import Site, VIDEO_MENU_SET

SITE = Site(
    name='video_refresh', kind='data64', address=0xA028F5600, old=0x40596140,
    adapter='z63_site_video_refresh', source='native/arm_time/sites/video_refresh.c',
    cls='M', wrapper='C', set=VIDEO_MENU_SET, rank=4, needs_q=True,
    stock_calls={'stock_video_refresh': 0x40596140},
    order=(('after', 1), ('after', 2)),
    purpose='video menu refresh handler 0xA028F5600 -> z63_site_video_refresh (stock refresh once, then each contributor)')
