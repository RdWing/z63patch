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
"""G3, indicators: recording-menu, live-view and i-menu frame-size icons that follow the H.265
tier (G5) and the ProRes grade (G4), and the readers that resolve the 25 extension icon ids
0x7000..0x7018.

One coupling set of 12 records, ranks rising in install order:
  1-2   the value reader's two consumers, BL 0x402F844C and BL 0x402F87DC (they
        replace the entry record 0x40339B30 "mov x8,#0x2850", a non-branch word; the reader is
        reached only by blr through the run-time slot 0xA02FC0D78, so no B/BL form of it exists)
  3-6   the four BL call sites of the index reader 0x40339A60
  7-12  producers: recording-menu bind, live-view tail, i-menu tile, the three setter call sites
Restore runs producers first and the readers last, so an extension id can never be handed out
while a reader is stock (a restore that the set's first failure stops leaves the readers
installed, which is harmless: they read no setting).

No state, no save, no SETTING_GET (file type from w25 at the menu site, else the byte
0xA0343BED0), no text slot, no shared site. Requires no group: the design found no technical
dependency on G2 (it reads the core's published word and the type byte); it maps only while an
owner (G4 for ProRes, G5 for H.265) is LIVE or DRAINING and is inert otherwise."""
import dataclasses

from . import Group, Code, Derived, imenu_terms, bl_word, b_word
from . import open_gate_entries as _ENT

# G10 ENT row label 'mark' (open_gate_entries): six more extension ids 0x7019..0x701E, rows
# pointing at G10's derived marked icons (z63patch/gate_icons.py gate_g3_block_mark). GROUP has them
# only if ENT's default is 'mark'; otherwise they are the variant indicators_mark (stage
# s7-opengate-mark, open_gate.py), and GROUP's readers, table and bytes are those of 25 ids.
ENT_MARK = _ENT.ROW_LABEL == 'mark'

# Reviewed composed stages this group introduces. H.265 is composed so the replays exercise a
# live owner (G4 does not exist yet; its phase is simulated in the replays).
STAGES = {'s7-indicators': ('marker', 'h265', 'indicators')}

INDEX_READER, VALUE_READER = 0x40339A60, 0x40339B30
INDEX_SITES = tuple((at, bl_word(at, INDEX_READER)) for at in (0x40329CB4, 0x40336DB4, 0x40336F3C, 0x4034EAEC))
SETTER = 0x40331D04         # the shared icon setter the three sites call
SETTER_SITES = ((0x4040A1E0, bl_word(0x4040A1E0, SETTER), True, 'BL in TEXT_BIND 0x4040A1AC'),
                (0x4040A248, bl_word(0x4040A248, SETTER), True, 'BL in 0x4040A210'),
                (0x40409F54, b_word(0x40409F54, SETTER), False, 'tail B in 0x40409F44'))
EXT_TABLE, EXT_IDS = 0x401AC000, 31 if ENT_MARK else 25
VALUE_SLOT = 0xA02FC0D78     # filled by 0x402F6814 from the provider record 0x42B6C1B0 (+0x10)
# The value reader's consumers: (BL site, stock word, stock callee, symbol, kind source)
VALUE_SITES = tuple((at, bl_word(at, callee), callee, sym, src) for at, callee, sym, src in (
    (0x402F844C, 0x402F9140, 'z63_g3_value_f1', 'w21'),
    (0x402F87DC, 0x402F92F8, 'z63_g3_value_f2', '[x21+4]')))
VALUE_BLR = (0x402F8424, 0x402F8794)   # the two blr through the slot

_readers = tuple(
    Code(address=at, old=old, symbol=sym, link=True, cls='S', wrapper='S',
         set='icons', rank=1 + k, needs_q=False, stores=True, lookup=False,
         purpose=f'value reader consumer {k + 1}/2: bl {callee:#x} -> bl {sym} (after the blr through 0xA02FC0D78: an '
                 'extension id gets the extension table\'s bytes/png in sp+0x28/sp+0x20, then tail b to the stock callee)')
    for k, (at, old, callee, sym, _) in enumerate(VALUE_SITES)
) + tuple(
    Code(address=at, old=old, symbol='z63_g3_icon_index', link=True, cls='S', wrapper='S',
         set='icons', rank=3 + k, needs_q=False, stores=True, lookup=False,
         purpose=f'index reader call site {k + 1}/4: bl 0x40339A60 -> bl z63_g3_icon_index (ids 0x7000..0x7018 from the '
                 'extension table, else tail b 0x40339A60)')
    for k, (at, old) in enumerate(INDEX_SITES))
_producers = (
    Code(address=0x4059652C, old=bl_word(0x4059652C, 0x4040A1AC), symbol='z63_g3_menu_draw_entry', link=True, cls='C', wrapper='C',
         set='icons', rank=7, needs_q=False,
         purpose='recording-menu frame-size row: bl TEXT_BIND 0x4040A1AC -> bl z63_g3_menu_draw_entry (file type from w25)'),
    Code(address=0x40430A18, old=b_word(0x40430A18, 0x40435914), symbol='z63_g3_lv_icons', link=False, cls='C', wrapper='C',
         set='icons', rank=8, needs_q=False,
         purpose='live-view frame-size owner tail: b 0x40435914 -> b z63_g3_lv_icons (fill w3, outline w4)'),
    Code(address=0x404EDCDC, old=bl_word(0x404EDCDC, 0x40409F44), symbol='z63_g3_imenu_icon', link=True, cls='C', wrapper='C',
         set='icons', rank=9, needs_q=False,
         purpose='i-menu tile: bl 0x40409F44 -> bl z63_g3_imenu_icon (kind 1 numbers, fill set)'),
) + tuple(
    Code(address=at, old=old, symbol='z63_g3_setter', link=link, cls='C', wrapper='C',
         set='icons', rank=10 + k, needs_q=False,
         purpose=f'shared icon setter 0x40331D04, {where}: -> {"bl" if link else "b"} z63_g3_setter (type 1, union map)')
    for k, (at, old, link, where) in enumerate(SETTER_SITES))

GROUP = Group(
    id=3, name='indicators', title='indicators (frame-size icons and extension icon readers)',
    source='native/arm_time/groups/indicators.c',
    slot=(0x401BC000, 0x401BF000),
    records=_readers + _producers,
    requires=(),
    stock_calls={'stock_icon_index': INDEX_READER, 'stock_value_f1': VALUE_SITES[0][2], 'stock_value_f2': VALUE_SITES[1][2], 'stock_text_bind': 0x4040A1AC, 'stock_lv_icons': 0x40435914,
                 'stock_imenu_icon': 0x40409F44, 'stock_setter': 0x40331D04},
    defines=('-fno-jump-tables',) + ((f'-DZ63_G3_EXT={EXT_IDS}',) if ENT_MARK else ()),   # no indirect branch from the pair tables
    install_needs_q=True,               # kept from the entry-record layout (every record is a B/BL word)
    terms=imenu_terms(),                # i-menu closed (movie-view arming design 2.2; gate_groups.IMENU_CLOSED)
    derived=(Derived(lo=EXT_TABLE, hi=0x401B0000, builder='z63patch.gate_icons:gate_g3_block' + ('_mark' if ENT_MARK else ''),
                     purpose='extension icon table (25 x 16 B) and the 21 derived PNGs, made from the user\'s MAIN at build time; '
                             'rows 0x7015..0x7018 point at G8\'s Log PNGs (gate_groups.GATE_LOG_ICONS)'),),
    # Readers last across groups: G8's live-view Log icon record hands out 0x7015..0x7018, so G3's
    # readers are restored only after every G8 record is finished.
    restore_after=(8, 10) if ENT_MARK else (8,),   # 'mark': G10's rows hand out 0x7019..0x701E
    replay_tests=('tests/test_arm_time_s7_indicators.py',),
    # The published word is read only to choose an icon number. During a recording the work
    # words are nonzero (nothing publishes), so every read in one recording sees one word; in idle
    # movie view a publication changes the icons, which is the intended effect.
    movie_publish=True)


def _mark_variant():
    d = GROUP.derived[0]
    return dataclasses.replace(
        GROUP, name='indicators_mark', title='indicators with the six G10 ENT marked icon ids 0x7019..0x701E',
        defines=('-fno-jump-tables', '-DZ63_G3_EXT=31'),
        derived=(dataclasses.replace(d, builder='z63patch.gate_icons:gate_g3_block_mark',
                                     purpose=d.purpose + '; rows 0x7019..0x701E point at G10\'s marked icons (0x401F2000)'),),
        restore_after=(8, 10))


VARIANTS = () if ENT_MARK else (_mark_variant(),)
