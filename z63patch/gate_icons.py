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
"""Build-time derived icon blocks of the composed arm-time gate.

G3's asset 0x401AC000..0x401B0000 holds the 25-record extension table and the 21 plain PNGs
derived from the user's MAIN (video_lv_icons.assets, the old layout's builder, unchanged); the four
Log PNGs that rows 0x7015..0x7018 name (produced only by G8's live-view Log icon record, loaded by
log_icons) sit in G8's asset range at gate_groups.GATE_LOG_ICONS, after G8's log tables. Both are Derived assets,
recomputed by the verifier; nothing extracted from the image is stored in the repository or a
recipe. Kept out of video_lv_icons.py so the old layout's compiled-unit source identities
(z63patch/units.py) do not change; for the same reason the 25-row layout is rebuilt here
(_lv_assets_with_logs) rather than taken from assets(log_icons=True), which imports the old-layout
log_record. Nothing here imports log_record or verify (tests/test_gate_import_graph.py)."""
import json
import struct

from .image import require
from . import video_lv_icons as LV

G3_END, G8_END = 0x401B0000, 0x401B6000
LOG_ROWS = range(21, 25)


def _lv_assets_with_logs(original):
    """Byte for byte video_lv_icons.assets(original, log_icons=True), without its import of the
    old-layout log_record (video_lv_icons.py itself stays unchanged: it is a compiled-unit source
    identity). The 21 plain records come from assets(original); both header sizes (21 and 25 rows
    of 16 bytes) are multiples of 8, so the PNG data and its padding are the same and every plain
    pointer moves by the four extra rows. The four Log PNGs follow as assets() lays them out at
    LV_ICON_PNGS; tests/test_gate_import_graph.py compares the two on the user's MAIN."""
    from .log_icons import LV_ICON_PNGS, lv_icons
    profile = json.loads(LV.PROFILE.read_text())
    start, end = int(profile['asset_start'], 16), int(profile['asset_end'], 16)
    plain = LV.assets(original)
    count, extra = profile['extension_count'], lv_icons()
    require(count == 21 and len(extra) == 4, 'icon asset count')
    shift = 16 * len(extra)
    out = bytearray(16 * (count + len(extra))) + plain[16 * count:]
    for index in range(count):
        flags, z, size, ptr = struct.unpack_from('<HHIQ', plain, 16 * index)
        struct.pack_into('<HHIQ', out, 16 * index, flags, z, size, ptr + shift)
    require(start + len(out) <= end, 'icon assets exceed reservation')
    logs = bytearray()
    for index, raw in enumerate(extra, count):
        logs.extend(bytes(-len(logs) % 8)); pointer = LV_ICON_PNGS + len(logs)
        struct.pack_into('<HHIQ', out, index * 16, 0x100, 0, len(raw), pointer); logs.extend(raw)
    require(LV_ICON_PNGS + len(logs) <= end, 'Log icon assets exceed reservation')
    return bytes(out), bytes(logs)


def _blocks(original):
    from .gate_groups import GATE_LOG_ICONS
    from .log_icons import LV_ICON_PNGS
    table, logs = _lv_assets_with_logs(original)
    require(LV_ICON_PNGS % 8 == 0 and GATE_LOG_ICONS % 8 == 0, 'Log icon bases must be 8-aligned')
    table = bytearray(table)
    for row in LOG_ROWS:                            # move the four Log pointers to GATE_LOG_ICONS
        flags, z, size, ptr = struct.unpack_from('<HHIQ', table, 16 * row)
        require(LV_ICON_PNGS <= ptr and ptr + size <= LV_ICON_PNGS + len(logs), 'Log icon row')
        struct.pack_into('<HHIQ', table, 16 * row, flags, z, size, ptr - LV_ICON_PNGS + GATE_LOG_ICONS)
    start = int(json.loads(LV.PROFILE.read_text())['asset_start'], 16)
    require(start + len(table) <= G3_END, 'G3 icon block exceeds 0x401B0000')
    require(GATE_LOG_ICONS + len(logs) <= G8_END, 'Log icons exceed 0x401B6000')
    return bytes(table), logs


def _ent_mark(original, table):
    """G10 ENT variant 'mark': rows 25..30 (0x7019..0x701E) for G10's derived marked icons at
    open_gate_icons.ICONS; the table grows by 96 bytes, so every pointer into G3's own block moves
    by 96 (8-aligned offsets stay 8-aligned)."""
    from . import open_gate_icons as OI
    start = int(json.loads(LV.PROFILE.read_text())['asset_start'], 16)
    n = len(OI.EXT_IDS)
    old_rows = 25
    head = bytearray(table[:16 * old_rows])
    for row in range(old_rows):
        flags, z, size, ptr = struct.unpack_from('<HHIQ', head, 16 * row)
        if start <= ptr < G3_END:
            struct.pack_into('<HHIQ', head, 16 * row, flags, z, size, ptr + 16 * n)
    for at, size in OI.layout(original)[1]:
        head.extend(struct.pack('<HHIQ', 0x100, 0, size, at))
    return bytes(head) + table[16 * old_rows:]


def gate_g3_block(original):
    """G3: the extension table (25 rows) and the 21 plain PNGs."""
    return _blocks(original)[0]


def gate_g3_block_mark(original):
    """G3 with G10 ENT row label 'mark': 31 rows, 0x7019..0x701E at G10's marked icons."""
    table = _ent_mark(original, _blocks(original)[0])
    require(int(json.loads(LV.PROFILE.read_text())['asset_start'], 16) + len(table) <= G3_END, 'G3 icon block exceeds 0x401B0000')
    return table


def gate_g8_log_icons(original):
    """G8: the four Log live-view PNGs at GATE_LOG_ICONS."""
    return _blocks(original)[1]
