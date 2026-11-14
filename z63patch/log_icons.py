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
"""The four live-view Log icons (C2.01; DO NOT FLASH).

The S-Log3 and Log3G10 fill and outline PNGs of NikonFirmwarePatch (z8log, upstream commit
a21fc8c04b6dd863439c4192d387b755633287f6, GPL-3.0-only), appended to the live-view icon extension
as private ids 0x7015..0x7018, for the gate's icon assets (gate_icons). This module holds no
firmware bytes: only the icon ids, the old layout's asset address and each PNG's SHA-256 and size.
The old-layout log_record keeps an identical copy of the icon list, because its own SHA-256 is
pinned as a control source by old-layout N-RAW profiles; tests/test_gate_import_graph.py requires
the two to stay equal.

The PNGs are read from ICON_DIR (assets/log-icons, unmodified upstream files) unless the environment variable
Z63_LOG_ICON_DIR names another directory holding the same four files. Every file is checked
against its pinned SHA-256 and geometry wherever it comes from, so the location cannot change a
built byte."""
import hashlib
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
ICON_DIR = ROOT / 'assets/log-icons'
ICON_DIR_ENV = 'Z63_LOG_ICON_DIR'
LV_ICON_IDS = (0x7015, 0x7016, 0x7017, 0x7018)
LV_ICON_PNGS = 0x401b4000  # old-layout asset pool, above the Log type menu tables
LV_ICON_FILES = (('slog3_fill', '34266622d2ecbee118002dd1da56b3f05986ac97a25c0f5369a1707c5f982e44', (64, 32)),
                 ('slog3_outline', 'fb99de6066385019279faf29d855faf81b7c3ce9292dfbc40343985c1153b626', (68, 36)),
                 ('log3g10_fill', '4d1be1c5d89e1076ad703de4084ed688fbd14975cdcad05a672ff9d82ebf89e3', (64, 32)),
                 ('log3g10_outline', '3807023191ad9537375c4e29deda0b1035d98010fbd7a8094d7df418a7e9e1dc', (68, 36)))


def icon_dir():
    """The directory the four PNGs are read from: $Z63_LOG_ICON_DIR if set, else ICON_DIR."""
    value = os.environ.get(ICON_DIR_ENV)
    return Path(value) if value else ICON_DIR


def lv_icons():
    """The Z8 patch's four Log icon PNGs (8-bit grayscale, the stock N-LOG
    icon sizes), pinned by hash, in private-id order."""
    out = []
    folder = icon_dir()
    for name, digest, (width, height) in LV_ICON_FILES:
        try:
            raw = (folder / (name + '.png')).read_bytes()
        except FileNotFoundError:
            raise ValueError(f'missing Log icon {name}.png in {folder} (set {ICON_DIR_ENV})') from None
        if hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError('upstream Log icon differs: ' + name)
        if raw[:8] != b'\x89PNG\r\n\x1a\n' or raw[12:16] != b'IHDR' or \
                raw[16:26] != struct.pack('>IIBB', width, height, 8, 0):
            raise ValueError('unexpected Log icon geometry: ' + name)
        out.append(raw)
    return out
