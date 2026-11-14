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
"""G10 part 10d: playback of open-gate clips; adapters in
native/arm_time/groups/open_gate_play.c.

Every record replaces a BL with a BL (class A: atomic at a branch word, no quiescence needed to
install or restore). The old layout's sites (native/open_gate_playback.S) were non-branch words
(0x40747DD0 mov, 0x40748010 ldr, 0x407E0558 / 0x407E05E4 ldrh, 0x407E9A44 mov) and are re-sited:

 - set `play` (ranks: the four setup calls first, the capture call last, so the latch is never
   written by an installed capture record while a setup call that clears it is still stock;
   restore in reverse):
     4 BL to the playback setup 0x40747DBC (latch clear for the main object, before the stock call);
     BL 0x4074800C to the attribute getter 0x406F065C (latch after the stock getter);
 - 15 + 2 BL to the composer aspect-class leaves 0x407E0558 / 0x407E05E4 (class 1 -> 0 for an
   open-gate N-RAW file);
 - BL 0x407E9A4C to 0x408088AC inside the film classifier 0x407E9A04 (classes 3 / 4 for an
   open-gate N-RAW record, through the classifier's own dims compares; see the C file).

The paused-movie preview 0x408250C8: hook z63h_pb_preview_g10 at the shared site pb_preview (its
17 BL sites; G10 first, then G7; R-PLAY-1 (a), G10 integration). The movie fit 0x4074AA30 (8 BL
sites, G7's records): G7 reads the play latch at G10 state +0x38 (R-PLAY-1 (b)); with G7 absent
the fit of an open-gate clip stays stock (display only).
The FX icon needs no hook (old layout finding)."""
from . import Code, Shared, bl_word

SETUP, ATTR, CLASS, CLASS_T, FILM, FILM_TABLE = 0x40747DBC, 0x406F065C, 0x407E0558, 0x407E05E4, 0x407E9A04, 0x408088AC
LOOKUP, RELEASE, KIND = 0x4081F9DC, 0x408A9258, 0x40808BE8
SETUP_SITES = (0x40747278, 0x40749CB0, 0x40749D24, 0x40749DA4)
ATTR_SITE = 0x4074800C
CLASS_SITES = (0x407E3474, 0x407E435C, 0x407E50D4, 0x407E7D28, 0x407E847C, 0x407E8D4C, 0x407E8DB0, 0x407F1974, 0x407F19BC,
               0x407F1A34, 0x407F1A64, 0x407F3A44, 0x407F3AE4, 0x407F6158, 0x407FAAD8)
CLASS_T_SITES = (0x407F4678, 0x407FB5C0)
FILM_SITE = 0x407E9A4C
OBJECT, CURRENT, NCTG = 0xA04881A70, 0xA04881950, 0xA047FA6A0
LATCH = 0x38                       # G10 state offset of the u64 play latch (+0x10: kind mirror)
STOCK_CALLS = {'stock_og_pb_setup': SETUP, 'stock_og_pb_attr': ATTR, 'stock_og_pb_class': CLASS,
               'stock_og_pb_class_t': CLASS_T, 'stock_og_pb_table': FILM_TABLE, 'stock_og_pb_lookup': LOOKUP,
               'stock_og_pb_release': RELEASE, 'stock_og_pb_kind': KIND}


def _records():
    out = []

    def add(at, callee, symbol, purpose, **kw):
        out.append(Code(address=at, old=bl_word(at, callee), symbol=symbol, link=True, cls='A', wrapper='C',
                        purpose=purpose, needs_q=False, **kw))
    for k, at in enumerate(SETUP_SITES):
        add(at, SETUP, 'g10_pb_setup', 'playback setup call: bl 0x40747DBC -> bl g10_pb_setup (open-gate play latch cleared '
            'for the main playback object, then the stock setup; old site 0x40747DD0)', set='play', rank=k + 1, stores=True)
    add(ATTR_SITE, ATTR, 'g10_pb_attr', 'attribute getter call in the playback setup: bl 0x406F065C -> bl g10_pb_attr (stock '
        'getter, then the clip latched from its attribute record; old site 0x40748010)', set='play', rank=len(SETUP_SITES) + 1,
        stores=True)
    for at in CLASS_SITES:
        add(at, CLASS, 'g10_pb_class', 'composer aspect class call: bl 0x407E0558 -> bl g10_pb_class (stock class; 1 -> 0 '
            'for an open-gate N-RAW file; old site 0x407E0558)')
    for at in CLASS_T_SITES:
        add(at, CLASS_T, 'g10_pb_class_t', 'composer aspect class call (thumbnail rule): bl 0x407E05E4 -> bl g10_pb_class_t '
            '(old site 0x407E05E4)')
    add(FILM_SITE, FILM_TABLE, 'g10_pb_film', 'film classifier table call: bl 0x408088AC -> bl g10_pb_film (an open-gate N-RAW '
        'record gets class 3 / 4 through the stock dims compares: x0 0, w19 low half the stock twin height; old site 0x407E9A44)')
    return tuple(out)


RECORDS = _records()
SHARED = (Shared(site='pb_preview'),)
