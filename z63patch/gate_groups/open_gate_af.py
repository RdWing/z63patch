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
"""G10 part 10c: AF transforms (area alias), AF area, zoom, navigator, display size and lit AF
points in open gate. Adapters in native/arm_time/groups/open_gate_af.c (included by open_gate.c).

Rules: lit AF points use the camera's photo layout; no install-time risk at
half-press, touch or magnify sites: every record here replaces a B or BL word, so installing it is
one atomic word swap, and every adapter is the identity unless open gate is on, i.e. og_size(p)
is non-zero (movie view, published bit 20, N-RAW 6K/4K, 30/25/24p) and G10 is LIVE/DRAINING; in
photo live view og_size(p) is 0), F3 (no clone).

Every decision is the old layout's (native/open_gate.c Z63_OPEN_GATE_AF / _AF_AREA / _ZOOM /
_DISPLAY, native/open_gate_lit_points.S), re-sited to B/BL words:

 set af.alias (all-or-none, consumers first: install ascending, restore descending)
   ranks 1-6   consumers inside the transforms: the size-function calls 0x4104E528/0x4104F1A8, the
               family calls 0x4104E67C/0x4104F220, the 0x4105D2E0 calls 0x4104E6B4/0x4104F258.
               They act only on PROVENANCE: the enclosing transform's saved return address is the
               return address of a producer's alias call (so the plan is the producer's own copy)
               and the area is the sentinel 0x80000004. Then: the size call gets a copy of the
               spec with area 4 (plus the 1920 x 1280 AF planes), the family call
               returns family 0 (the old family rule), the 0x4105D2E0 call gets area 4. Anything
               else (including a 0x80000004 stock might produce) goes to stock unchanged.
   ranks 7-16  producers: the 10 BL callers of 0x4104E4A8 and 0x4104F124. LIVE, open gate on, plan
               area 4 and type 10/12 (6K) or 11/13 (4K): the transform runs on a copy of the plan
               whose area is the sentinel, inside an ACT hold, so the 16:9 crop decision falls
               through to the skip (old 0x4104E5A4 / 0x4104F2E0 edits).
 family    the 7 other BL callers of 0x4105D31C: family 0 under the old rule (old entry hook).
 AF area   local area: BL 0x40F18D14 (property 0x2024 getter) and B 0x40F18CF8 (override path),
           both into the old site 0x40F18D38; grid: the 13 BL callers of 0x40F286AC (area-0 record
           where it exists, kind <= 8; old sites 0x40F28770/0x40F28828); class: BL 0x40F10D24 and
           BL 0x40F1E0EC, the first call after the old stores 0x40F10D04 / 0x40F1E0D8 on their only
           path (class 0 for info area 4).
 zoom      the 5 BL 0x403FFCD0 (key 0x2024) of magnify and navigator: area 4 -> 0; bounds: the 2 BL callers of 0x403973A4 (old
           epilogue site 0x403974F8, the function's only return path).
 display   BL 0x41058894 to 0x4105AA10: area-0 entry for (area 4, row 2).
 lit       BL 0x40F14E98 to 0x40F1605C: mode 4 -> 0 (photo layout) for area 4, w1 != 6.

Deferred: the navigator inset (old class/aspect reads 0x404298D4/F0, 0x4042995C/0x404299AC). Every
path of 0x4042971C reaches the set-rect call 0x40429A64 with the rect at the caller's sp+0x48, and
the old layout's offset changes that rect for everything after it (the second set-rect 0x40429A70,
the inner rectangle, the frame derived from sp+0x48), so the faithful re-site is the in-place 3:2
rect at 0x40429A64. That word is G7's record g7_nav_rect (in place, squeeze); two groups at one
word need a shared site (G10 first, then G7): G10-AF-REQUESTS.md R-AF-3. Until then this part
leaves the navigator in the 16:9 inset.

Not used: the lv_display post hook. The class stored at 0x40F10D04 is never read by the
builder itself (every path to the read 0x40F13770 passes the stores 0x40F13678/0x40F13684), and a
hook after the builder cannot tell the 0x40F10D04 path from the property-0x209E path, which the
old layout left alone. The BL at 0x40F10D24 is exact."""
from . import Code, bl_word


S = 0x80000004                                      # the area sentinel
FWD, INV, SIZE, FAMILY, D2E0 = 0x4104E4A8, 0x4104F124, 0x4105D550, 0x4105D31C, 0x4105D2E0
FWD_SITES = (0x40EB4DE0, 0x4104F06C, 0x4104F084, 0x4104F09C, 0x4104F0B4)
INV_SITES = (0x40EB4E84, 0x4104F47C, 0x4104F490, 0x4104F4A4, 0x4104F4B8)
# (site, callee, symbol) consumers inside the transforms
CONSUMERS = ((0x4104E528, SIZE, 'g10_af_size_f'), (0x4104F1A8, SIZE, 'g10_af_size_i'),
             (0x4104E67C, FAMILY, 'g10_af_fam_f'), (0x4104F220, FAMILY, 'g10_af_fam_i'),
             (0x4104E6B4, D2E0, 'g10_af_d2e0_f'), (0x4104F258, D2E0, 'g10_af_d2e0_i'))
# The transforms' frames: (function, frame bytes, saved x30 offset, plan register)
FRAMES = {FWD: (0x190, 0x148, 21), INV: (0x1B0, 0x158, 22)}
FAMILY_SITES = (0x4104F610, 0x41050544, 0x410506DC, 0x410508B8, 0x41050C70, 0x41050CC4, 0x41050DBC)
PROP, LOCAL_BL, LOCAL_B, LOCAL_JOIN, LOCAL_SITE = 0x40F0C0B0, 0x40F18D14, 0x40F18CF8, 0x40F18D2C, 0x40F18D38
GRID = 0x40F286AC
GRID_SITES = (0x40F0FFE0, 0x40F19690, 0x40F1A324, 0x40F1AA7C, 0x40F1AC60, 0x40F1AD44, 0x40F1BE68, 0x40F1C73C,
              0x40F1C8A0, 0x40F1D6B4, 0x40F1E9B0, 0x40F1FA28, 0x40F1FB44)
CLASS_SITES = ((0x40F10D24, 0x40F0C1D8, 'g10_cls_d24', 19, 0x40F10D04),
               (0x40F1E0EC, 0x40D32F74, 'g10_cls_e0ec', 21, 0x40F1E0D8))
ZOOM_GET = 0x403FFCD0
ZOOM_SITES = (0x4048EEC0, 0x4048F0DC, 0x4048FA8C, 0x404905D8, 0x40429BEC)
NAV_RECT, NAV_SITE = 0x40409F5C, 0x40429A64        # deferred (shared with G7, R-AF-3)
NAV_TABLE, NAV_CLASS, NAV_ASPECT = 0x42BA4740, 0xA0343BDB0, 0xA0343BF0C
BOUNDS, BOUNDS_SITES = 0x403973A4, (0x403970EC, 0x4042500C)
DISPLAY, DISPLAY_SITE = 0x4105AA10, 0x41058894
LIT, LIT_SITE = 0x40F1605C, 0x40F14E98

STOCK_CALLS = {'stock_af_fwd': FWD, 'stock_af_inv': INV, 'stock_af_size': SIZE, 'stock_af_family': FAMILY,
               'stock_af_d2e0': D2E0, 'stock_af_prop': PROP, 'stock_af_grid': GRID,
               'stock_af_cls_d24': CLASS_SITES[0][1], 'stock_af_cls_e0ec': CLASS_SITES[1][1],
               'stock_zoom_get': ZOOM_GET, 'stock_zoom_bounds': BOUNDS,
               'stock_disp_size': DISPLAY, 'stock_lit_mode': LIT, 'stock_afa_join': LOCAL_JOIN}


def b_word(at, callee):
    return 0x14000000 | (((callee - at) >> 2) & 0x3FFFFFF)


def _records():
    out = []

    def alias(at, callee, symbol, wrapper, lookup, stores, purpose):
        out.append(Code(address=at, old=bl_word(at, callee), symbol=symbol, link=True, cls='A', wrapper=wrapper,
                        purpose=purpose, set='af.alias', rank=len(out) + 1, needs_q=True, all_or_none=True,
                        stores=stores, lookup=lookup))
    for at, callee, sym in CONSUMERS:
        if callee == SIZE:
            alias(at, callee, sym, 'C', True, True,
                  'F3 consumer: size-function call inside the AF transform; an aliased plan (sentinel area, producer '
                  'provenance) gets a spec copy with area 4, then the 1920 x 1280 AF planes')
        else:
            alias(at, callee, sym, 'S', False, False,
                  f'F3 consumer: {"family" if callee == FAMILY else "0x4105D2E0"} call inside the AF transform; '
                  'pure map on producer provenance (sentinel -> family 0 / area 4), else a tail branch to stock')
    for at in FWD_SITES:
        alias(at, FWD, 'g10_af_fwd', 'C', True, True,
              'F3 producer: forward AF transform call; open gate, area 4, plan 10/12 (6K) or 11/13 (4K): the plan copy '
              'with the sentinel area (16:9 crop skipped), in an ACT hold')
    for at in INV_SITES:
        alias(at, INV, 'g10_af_inv', 'C', True, True, 'F3 producer: inverse AF transform call (as the forward one)')

    def plain(at, old, symbol, link, purpose, stores=True):   # og_size (ACT hold, latch) stores
        out.append(Code(address=at, old=old, symbol=symbol, link=link, cls='A', wrapper='C', purpose=purpose,
                        needs_q=False, stores=stores))
    for at in FAMILY_SITES:
        plain(at, bl_word(at, FAMILY), 'g10_af_family', True,
              'coordinate family call: family 0 for open gate, area 4, plan 10/12 or 11/13 (old entry hook 0x4105D31C)')
    plain(LOCAL_BL, bl_word(LOCAL_BL, PROP), 'g10_afa_local_get', True,
          'AF local area: property 0x2024 read for 0x40F18D38; area 4 -> 0 in open gate', stores=True)
    plain(LOCAL_B, b_word(LOCAL_B, LOCAL_JOIN), 'g10_afa_local_ovr', False,
          'AF local area, override path (b 0x40F18D2C): w8 4 -> 0 in open gate, then the same join')
    for at in GRID_SITES:
        plain(at, bl_word(at, GRID), 'g10_afa_grid', True,
              'AF grid record 0x40F286AC: area 4 -> the area-0 record where it exists (kind <= 8) in open gate', stores=True)
    for at, callee, sym, reg, store in CLASS_SITES:
        plain(at, bl_word(at, callee), sym, True,
              f'AF class: first call after the class store {store:#x} (only path); class 0 for info area 4 in open gate',
              stores=True)
    for at in ZOOM_SITES:
        plain(at, bl_word(at, ZOOM_GET), 'g10_zoom_area', True, 'zoom area: property 0x2024 4 -> 0 in open gate')
    for at in BOUNDS_SITES:
        plain(at, bl_word(at, BOUNDS), 'g10_zoom_bounds', True,
              'zoom display bounds 0x403973A4: 16:9 source rows -> full height in open gate (old 0x403974F8)', stores=True)
    plain(DISPLAY_SITE, bl_word(DISPLAY_SITE, DISPLAY), 'g10_disp_size', True,
          'fallback display size: (area 4, row 2) -> the area-0 entry in open gate')
    plain(LIT_SITE, bl_word(LIT_SITE, LIT), 'g10_lit_mode', True,
          'lit AF points: mode 4 -> 0 (photo layout) for area 4, w1 != 6, in open gate')
    return tuple(out)


RECORDS = _records()
