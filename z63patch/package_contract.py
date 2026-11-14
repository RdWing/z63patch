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
"""Explicit selections for frozen MAIN recipes. No manifest grants admission.

OLD_RECIPES are the old-layout (pre-gate) MAIN recipes, kept as test builds. GATE_RECIPES maps the
package recipe of the arm-time gate to its reviewed s7 stage; 'arm-time-all' is the single
deliverable recipe.

The old-layout verifier (z63patch.verify, which holds stock bytes) is imported only inside the
old-layout branch of verify_main, so the gate path never loads it (tests/test_gate_import_graph.py)."""
from .image import require

OLD_RECIPES=('stock','prores','precapture','prores-precapture','settings-v2','h265-quality','metadata-recovery','h265-metadata-recovery','video-menu-icons')
GATE_RECIPES={'arm-time-all':'s7-all'}
DELIVERABLE='arm-time-all'
RECIPES=OLD_RECIPES+tuple(GATE_RECIPES)


def verify_main(original,candidate,recipe):
    require(recipe in RECIPES,'unknown MAIN recipe')
    if recipe in GATE_RECIPES:
        from .package_gate import check_gate_main
        check_gate_main(original,candidate,GATE_RECIPES[recipe])
        return
    if recipe=='stock':
        require(original==candidate,'stock recipe changed MAIN')
        return
    from .verify import verify
    retention=recipe in ('precapture','prores-precapture','metadata-recovery','h265-metadata-recovery','video-menu-icons')
    verify(original,candidate,idle_config=True,native_settings=True,
        retention_probe=retention,precapture_only=recipe=='precapture',
        settings_v2=recipe in ('settings-v2','h265-quality','h265-metadata-recovery','video-menu-icons'),
        h265_quality=recipe in ('h265-quality','h265-metadata-recovery','video-menu-icons'),
        metadata_recovery=recipe in ('metadata-recovery','h265-metadata-recovery','video-menu-icons'),
        video_menu_icons=recipe=='video-menu-icons')
