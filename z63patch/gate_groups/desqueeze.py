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
"""G7, desqueeze (with section 2.4a for the mask).

Every record is a BL call site whose BL now goes to an adapter of the group (BL for BL, class A:
safe to install and restore while the code runs, so no record needs quiescence and the group
has no install terms). 23 records, and the shared preview site:

- view: the two LV window calls (0x40F1BAD4, 0x40F1E160 -> g7_view_window, ranks 1-2) and the
  two memcmp calls of the view pass that together cover every path to the change-mask point
  0x40F1B9EC (0x40F1B7F0, 0x40F1B880 -> g7_mask_cmp, ranks 3-4: installed after the window
  records and restored before them). g7_mask_cmp changes callee-saved w22 (the mask) on purpose;
  tests/test_arm_time_s7_desqueeze.py keeps the static path-coverage and liveness check.
- maps: AF frame (4 sites of 0x40394E88), touch (5 sites of 0x4045C1FC), lit points (0x40394368,
  thunk 0x403FF1B0), navigator (0x40429A64, first set-rect call after the background store;
  mapped in place, see the adapter); playback fit (8 sites of 0x4074AA30).
- paused-movie preview: the 17 sites of 0x408250C8 are the shared site `pb_preview` (G10 first,
  then G7's hook z63h_pb_preview_g7).

Corner marks are deferred. The navigator's second set-rect call 0x40429A70 is not a
record: the first one maps the caller's rectangle in place, as the old stores did, so the second
call, the inner rectangle and the frame derived later from [sp+0x48] see the mapped value (a
copy would leave them native). requires=(2,) (G2, the ratio row and its save); the group reads the published word only.
G10 is read through z63g_peer_state (open gate off when G10 is absent or not live): +0 og_active
(window 0..682 before the ratio, navigator 3:2 inset, mask) and +0x38 the play latch (fit).

Switches (the one place to change them; each is a camera-emulator item, G10-PLAY-REQUESTS):
  OG_AF_REFERENCE: R-PLAY-5. 'native' (default): with open gate on, the AF frame, touch and
    navigator maps go from the recorded native rows (stock 54..630), as the old combined layout
    did in the emulator runs. 'open_gate': from
    the 3:2 rows 0..682 (the old Z63_OPEN_GATE_AF reference). 1,048 open-gate cases differ.
  OG_LIT_BASIS: R-PLAY-4. 'wide' (default): lit points rescaled from 9W/16 (576), the old
    combined monitor build. 'photo': with open gate on, from round(2W/3), the 3:2 basis of the
    photo layout 10c's lit record selects, as the old viewfinder rule did."""
from . import Group, Code, Shared, bl_word

OG_AF_REFERENCE = 'native'         # R-PLAY-5: 'native' (old combined) or 'open_gate'
OG_LIT_BASIS = 'wide'              # R-PLAY-4: 'wide' (old combined, 576) or 'photo' (3:2)

STAGES = {'s7-desqueeze': ('marker', 'menu', 'desqueeze')}

WINDOW_LEAF, MEMCMP, AF_RECT, TOUCH, GUI_REFRESH = 0x411894D8, 0x4163B3C0, 0x40394E88, 0x4045C1FC, 0x403A4BF0
PB_FIT, LIT_THUNK, SET_RECT, PREVIEW = 0x4074AA30, 0x403FF1B0, 0x40409F5C, 0x408250C8
WINDOW_SITES = (0x40F1BAD4, 0x40F1E160)
MASK_SITES = (0x40F1B7F0, 0x40F1B880)
MASK_POINT = 0x40F1B9EC            # mov w25,w22 (the old layout's record)
AF_SITES = (0x40394324, 0x403944FC, 0x403945F4, 0x40395064)
TOUCH_SITES = (0x4045BE6C, 0x4045BF3C, 0x4045BF54, 0x4045BF60, 0x4045C0A8)
FIT_SITES = (0x4074FDD4, 0x4074FDEC, 0x4074FE04, 0x4074FE1C, 0x40750880, 0x40750898, 0x407508B0, 0x407508C8)
LIT_SITES = (0x40394368,)
NAV_SITES = (0x40429A64,)
from .sites.pb_preview import PREVIEW_SITES   # shared site pb_preview (G10, G7)


def _sites(addresses, callee, symbol, purpose, **fields):
    """One record per BL site of the stock callee; the old word is that BL (the verifier and the
    tests check it against the stock image)."""
    return tuple(Code(address=at, old=bl_word(at, callee), symbol=symbol, link=True, cls='A', wrapper='C', needs_q=False,
                      purpose=purpose, **fields) for at in addresses)


def _ranked(addresses, callee, symbol, purpose, first, **fields):
    out = ()
    for k, at in enumerate(addresses):
        out += _sites((at,), callee, symbol, purpose, set='view', rank=first + k, **fields)
    return out


RECORDS = (
    _ranked(WINDOW_SITES, WINDOW_LEAF, 'g7_view_window', 'LV window leaf call -> g7_view_window: stock leaf, then the ratio '
            'on the movie-view window (output 0, classes 0-1) and the native/shown rows of the global window recorded', 1, stores=True)
    + _ranked(MASK_SITES, MEMCMP, 'g7_mask_cmp', 'view-pass memcmp -> g7_mask_cmp: stock memcmp (x0 kept), then the change '
              'mask in w22 gets 5 once per change of the wanted ratio (replaces the old record at 0x40F1B9EC)', 3, stores=True)
    + _sites(AF_SITES, AF_RECT, 'g7_af_rect', 'AF frame builder call: centre row and height mapped native -> shown on frame copies')
    + _sites(TOUCH_SITES, TOUCH, 'g7_touch', 'touch consumer call: GUI refresh, then the touched row mapped shown -> native on a frame copy')
    + _sites(LIT_SITES, LIT_THUNK, 'g7_lit', 'lit AF points call: after the stock fill, rows rescaled to the shown height', stores=True)
    + _sites(NAV_SITES, SET_RECT, 'g7_nav_rect', 'navigator: the background rectangle at the caller\'s [sp+0x48] gets the squeezed '
             'height in place before the first set-rect call (the old stores at 0x40429A58/0x40429F98)', stores=True)
    + _sites(FIT_SITES, PB_FIT, 'g7_pb_fit', 'movie playback fit call: after the stock fit, the squeezed height centred', stores=True)
)

GROUP = Group(
    id=7, name='desqueeze', title='Desqueeze (movie view, AF/touch/lit/navigator maps, playback)',
    source='native/arm_time/groups/desqueeze.c',
    slot=(0x401CB000, 0x401D0000),
    records=RECORDS,
    shared=(Shared(site='pb_preview'),),   # z63h_pb_preview_g7: the 1920x1080 request squeezed (after G10's hook)
    hook_stores=True,
    defines=(f'-DG7_OG_AF_REFERENCE={int(OG_AF_REFERENCE == "open_gate")}', f'-DG7_OG_LIT_BASIS={int(OG_LIT_BASIS == "photo")}'),
    requires=(2,),
    state=(0x8B8, 16),
    stock_calls={'stock_window': WINDOW_LEAF, 'stock_memcmp': MEMCMP, 'stock_af_rect': AF_RECT, 'stock_touch': TOUCH,
                 'stock_gui_refresh': GUI_REFRESH, 'stock_pb_fit': PB_FIT, 'stock_lit': LIT_THUNK,
                 'stock_set_rect': SET_RECT},
    install_needs_q=False,
    replay_tests=('tests/test_arm_time_s7_desqueeze.py',),
    movie_publish=True)   # Display only; a ratio change published in idle movie view is taken by the
                          # next view pass (mask) and window call; nothing recorded depends on it
