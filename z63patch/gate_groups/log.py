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
"""G8, log recording.

N-Log, S-Log3 / S-Gamut3.Cine and Log3G10 / REDWideGamutRGB recording as the old-layout units
(native/log_record.c/.S, log_keys.S, log_lv_icon.S) did it, on the arm-time gate, plus the
tone-page relabel. The log type is the published settings word's log field (bits 16-17); G8
saves nothing; its only state is the anchor value word. Source: native/arm_time/groups/log.c.

Records (19) and clones (2):
 - producers, one all-or-none set: the four producer call sites (BL -> BL; the wrapper calls the
   stock producer in PASS and the build-time clone in LIVE/DRAINING with a type), CPXS (BL -> BL,
   the custom table stored at [x19+0xB0] after the stock call), NLG re-sited onto the two memcpy
   BLs (A class instead of the old madd value sites), RGB (the first of the four row-copy memcpy
   BLs after the old value site). The clones (
   only the two approved ranges) are made at build time from the user's MAIN; M1 +0x3CC is a BL
   to the curve adapter, M2 +0x708 (frameless leaf) a B whose adapter returns by B to +0x70C.
 - metadata, one all-or-none set: the NCTG tone-tag and picture-control-name data words. Static
   evidence that their pointers are re-read at each use: the serializer 0x40706744 reaches each
   descriptor through 0x40702AC4 and loads [descriptor+0x18] just before its blr x8 (0x407067A4/
   0x407067BC; +0x28 at 0x40706808, +0x20 at 0x407068B4); every address builder of the table
   (0xA02993158, 100 descriptors of 0x40) is the init 0x407026B4 (writes +8/+0x10 only) and the
   two lookups; no data word holds a table address. Lower bound (indirect accesses not enumerable).
 - labels: the four TEXT_STORE BLs of the file-type label function 0x40590218, each the first
   consumer of the "N-Log" text one label block builds on the stack (the old records replaced
   the movk at +8 of each "N-Log" site, non-branch words).
 - keys: the ProRes 422 colour key, an all-or-none set of three (key count, value copy, next
   offset), all B/BL words: the key count on the B that follows the cinc, the next
   offset on the B that follows its add. The next offset follows what the copy wrote, so the pair
   cannot disagree.
 - LV icon: B -> B; also requires G3 LIVE/DRAINING (its readers resolve the private icons).
 - tone_relabel: the refresh word of the tone page's method tuple (screen 0x117 = 279,
   record 0x452F63F0, tuple 0xA028F5978, +0x20). The generic refresh loop 0x405A4F58 loads
   [record+0x28] then [tuple+0x20] and calls it with x0 = the menu root for each open screen
   (O, the same loop calls the video menu's 0x40596140 that the marker hooks). The label widget id
   of a tone row is NOT established (Z63_G8_LABEL_WIDGET, default 0x4DC0, the value widget of the
   file-type rows). In the integration: OFF in GROUP (and so in every composed image); the
   variant log_relabel (VARIANTS, stage s7-log-relabel) adds it, for the camera emulator only.
Text slot 292 (TEXT_SLOTS[8]; the design's 281 lies in G2's range). No settings bits.

Movie-view publication: movie_publish=True. The producers (M1, M2, the three
base value sites, NLG, CPXS) are anchor reads: they take the composition's anchor hold and
record the type in the value word (state +0x9A8), so publication is refused until 8 consecutive native-idle polls pass with no producer
call, or at once under Q. The file writers (NCTG tag and name, colour-key copy, key count) use
the last anchor value, so a recording's metadata agrees with its curve. The file-type labels,
the LV icon and the tone-page refresh stay fresh (display only). No publication callback:
no cache of the type outside the producers' outputs was found. Not established: whether the
producers run continuously in idle movie view (then the hold never releases outside Q and
movie-view publication stays held, the old behaviour) and whether the recording job reuses
outputs built at movie entry (then the new curve applies once the job is rebuilt; metadata
follows the anchor either way). Both are on the emulator watch list (README section 6).
"""
import dataclasses

from . import Group, Code, Data, Asset, Derived, Clone, Patch, bl_word, GATE_LOG_ICONS


def b_word(at, target):
    return 0x14000000 | (((target - at) >> 2) & 0x3FFFFFF)

STAGES = {'s7-log': ('marker', 'menu', 'indicators', 'log'),
          's7-log-relabel': ('marker', 'menu', 'indicators', 'log_relabel')}

M1, M2 = 0x4111CD60, 0x4111D618
CPXS, MEMCPY = 0x4111C854, 0x4163B5C0
TONE_TAG, PC_NAME = 0x40707108, 0x40707EA8
LV_TAIL = 0x404311A8
LABEL_SITES = (0x4059027C, 0x405902EC, 0x40590364, 0x405903DC)       # the old records' movk words (stay stock)
LABEL_STORES = (0x40590294, 0x40590304, 0x4059037C, 0x4059043C)      # bl TEXT_STORE 0x404066C8 of each label block
TEXT_STORE = 0x404066C8
RGB_COPY = 0x411272D0      # bl memcpy: the first row copy after the RGB base (old value site 0x4112729C, stays stock)
KEYCOUNT_B, KEYCOUNT_T = 0x406F4EF0, 0x406F4F04      # b after the key-count cinc 0x406F4EEC (stays stock)
CC_NEXT_B, CC_NEXT_T = 0x406F54B0, 0x406F557C        # b after the next-offset add 0x406F54AC (stays stock)
SLOT = 292
TONE_RELABEL = False     # the tone-page relabel record is in the variant log_relabel only (its widget id is not established)


def _call(at, callee, symbol, purpose, **kw):
    return Code(address=at, old=bl_word(at, callee), symbol=symbol, link=True, purpose=purpose, **kw)


_producers = (
    _call(0x4111C7A8, M1, 'z63_g8_m1', 'M1 producer call, ordinary builder 0x4111C770: stock in PASS, clone m1 when live',
          cls='A', wrapper='C', set='producers', rank=1, all_or_none=True),
    _call(0x4111EA5C, M1, 'z63_g8_m1', 'M1 producer call, alternate builder 0x4111EA28',
          cls='A', wrapper='C', set='producers', rank=2, all_or_none=True),
    _call(0x4111C7CC, M2, 'z63_g8_m2', 'M2 producer call, ordinary builder 0x4111C770: stock in PASS, clone m2 when live',
          cls='A', wrapper='C', set='producers', rank=3, all_or_none=True),
    _call(0x4111EAB8, M2, 'z63_g8_m2', 'M2 producer call, alternate builder 0x4111EA74',
          cls='A', wrapper='C', set='producers', rank=4, all_or_none=True),
    _call(0x41121610, CPXS, 'z63_g8_cpxs', 'CPXS: bl 0x4111C854, then [x19+0xB0] = custom table for tone N-Log ([[x19+0x98]+0x31] == 1)',
          cls='A', wrapper='C', set='producers', rank=5, all_or_none=True, stores=True),
    _call(0x411216B4, MEMCPY, 'z63_g8_nlg', 'NLG, ordinary route: memcpy of the N-Log row 0xA029EA808 takes the custom curve',
          cls='A', wrapper='C', set='producers', rank=6, all_or_none=True),
    _call(0x411258D4, MEMCPY, 'z63_g8_nlg', 'NLG, alternate route: memcpy of the N-Log row 0xA029EA808 takes the custom curve',
          cls='A', wrapper='C', set='producers', rank=7, all_or_none=True),
    _call(RGB_COPY, MEMCPY, 'z63_g8_rgb', 'RGB curves: the first row copy bl memcpy -> bl (old site 0x4112729C movk x10; '
          'rows 0..2 of the stock base, i.e. w8 == 3: x1 and the callee-saved row bases x22/x19/x20 take the custom rows)',
          cls='A', wrapper='C', set='producers', rank=8, all_or_none=True),
)
_metadata = (
    Data(address=0xA029935B0, old=TONE_TAG, symbol='z63_g8_tag', cls='D', wrapper='C',
         purpose='NCTG tone tag (descriptor 17 +0x18): leaf 0x40707108, then tag 2 -> 1 for file class < 5 with a type',
         set='metadata', rank=1, all_or_none=True, stores=True),
    Data(address=0xA02993F70, old=PC_NAME, symbol='z63_g8_name', cls='D', wrapper='C',
         purpose='NCTG 0x2000023 picture-control name (descriptor 56 +0x18): writer 0x40707EA8, then the type name at record +0x360',
         set='metadata', rank=2, all_or_none=True, stores=True),
)
_labels = tuple(
    _call(at, TEXT_STORE, 'z63_g8_label', f'file-type label "N-Log" -> "Log": bl TEXT_STORE -> bl (the buffer the block '
          f'built at {site - 8:#x}..{site + 8:#x}; old site {site:#x})', cls='A', wrapper='C', stores=True)
    for at, site in zip(LABEL_STORES, LABEL_SITES))
_keys = (
    Code(address=KEYCOUNT_B, old=b_word(KEYCOUNT_B, KEYCOUNT_T), symbol='z63_g8_keycount', link=False, cls='A', wrapper='C',
         purpose='ProRes 422 key count: b 0x406F4F04 after cinc w24,w8,eq (old site 0x406F4EEC) -> b (2 keys for '
                 'Log3G10 under N-Log), then b 0x406F4F04',
         set='keys', rank=1, all_or_none=True),
    _call(0x406F54A8, MEMCPY, 'z63_g8_cc_copy', 'colour-key value copy: S-Log3 key (20 bytes) instead of the N-Log key (22)',
          cls='A', wrapper='C', set='keys', rank=2, all_or_none=True),
    Code(address=CC_NEXT_B, old=b_word(CC_NEXT_B, CC_NEXT_T), symbol='z63_g8_cc_next', link=False, cls='A', wrapper='C',
         purpose='colour-key next offset: b 0x406F557C after add w8,w29,#0x2e (old site 0x406F54AC) -> b (two shorter '
                 'when the S-Log3 key was copied), then b 0x406F557C',
         set='keys', rank=3, all_or_none=True),
)
_lv_icon = (
    Code(address=0x404310A8, old=b_word(0x404310A8, 0x404311A8), symbol='z63_g8_lv_icon', link=False, cls='A', wrapper='C', needs_q=False,
         purpose='live-view N-Log icon: b 0x404311A8 -> b (w3/w4 = 0x7015/0x7016 or 0x7017/0x7018 with G3 live)'),
)
_tone_relabel = (
    Data(address=0xA028F5998, old=0x40590D70, symbol='z63_g8_tone_refresh', cls='D', wrapper='C',
         purpose='tone page (screen 279) refresh: the type name into text slot 292, bound to rows 0x30FC and 0x30FF',
         set='tone_relabel', rank=1, stores=True),
)

GROUP = Group(
    id=8, name='log', title='log recording',
    source='native/arm_time/groups/log.c',
    slot=(0x401D0000, 0x401D8000),
    records=_producers + _metadata + _labels + _keys + _lv_icon + (_tone_relabel if TONE_RELABEL else ()),
    requires=(2, 3),     # G2: the log row and its save; G3: the readers of the LV icon ids 0x7015..0x7018
    stock_calls={'stock_m1': M1, 'stock_m2': M2, 'stock_cpxs': CPXS, 'stock_memcpy': MEMCPY,
                 'stock_tone_tag': TONE_TAG, 'stock_pc_name': PC_NAME, 'stock_lv_icon_tail': LV_TAIL,
                 'stock_row_value': 0x4052C900, 'stock_text_store': TEXT_STORE, 'stock_text_bind': 0x4040A1AC,
                 'stock_text_store8': TEXT_STORE,     # the label sites' replaced call, all eight argument registers
                 'stock_keys_f04': KEYCOUNT_T, 'stock_keys_57c': CC_NEXT_T},   # tail targets of the two key B records
    defines=(f'-DZ63_G8_SLOT={SLOT}u',),
    text_slots=(SLOT,),
    assets=(Asset(0x401B4000, GATE_LOG_ICONS, 'log tables (Z8 LG_DATA layout, Z6III M1 bases), 0x1500 bytes'),),
    # The four Z8-patch Log live-view PNGs (S-Log3, Log3G10 fill/outline; repository files pinned by
    # hash in log_icons.LV_ICON_FILES) that G3's extension rows 0x7015..0x7018 point at.
    derived=(Derived(GATE_LOG_ICONS, 0x401B6000, 'z63patch.gate_icons:gate_g8_log_icons',
                     'Log live-view icons for ids 0x7015..0x7018 (G3 readers)'),),
    clones=(Clone('m1', M1, 0x4111D480, 0x401D6000, patches=(Patch(0x3CC, 'z63_g8_m1_site', True),)),
            Clone('m2', M2, 0x4111DF4C, 0x401D6800, patches=(Patch(0x708, 'z63_g8_m2_site', False),))),
    state=(0x9A8, 8), hold=0x9A8,   # Anchor value word
    replay_tests=('tests/test_arm_time_s7_log.py', 'tests/test_arm_time_s7_movie_publish.py'),
    movie_publish=True)

# The tone-page relabel as its own stage for the camera emulator; never in the default
# image while the label widget of a tone row is not established.
VARIANTS = () if TONE_RELABEL else (dataclasses.replace(GROUP, name='log_relabel', title='log recording with the tone-page relabel',
                                                        records=GROUP.records + _tone_relabel),)
