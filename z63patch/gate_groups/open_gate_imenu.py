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
"""G10 part IM: the six [FX 3:2] options in the i-menu frame-size control for N-RAW;
adapters in native/arm_time/groups/open_gate_imenu.c. Only B/BL code words are replaced (COMMON-G10 rule).

The control on screen is the horizontal STRIP of i-menu tile 0xB3, stepped by the sub-command dial
(emulator: count 17, wraps both ways, OK closes it). The six options are appended after
the 17 regular ones as strip positions 17..22 (6K 30/25/24p, then 4K 30/25/24p). Position
17 + k is twin row k (stock indices 2, 3, 4, 7, 8, 9: the same N-RAW size and rate), shown with
the twin's strip icon and label; the [FX 3:2] meaning is the saved choice bound to that pair.
The first build's 23-cell LIST (layout, option map, current cell, 12 group slots, title list) is
withdrawn: its cells 17..22 were drawn blank and could not be reached (emulator).

Stock facts (C2.01 MAIN, read with capstone; guarded by tests/test_arm_time_s7_opengate_imenu.py):
 - Strip 0x404802F8(w0) (only reached through table 0x43B330E0..): w21 = file type
   (0x06000F06), the current index from the dial's matcher `bl 0x40506010(0xA034826EC)` at
   0x404803C0, `bl 0x404790A0(4)` (strip object 0xA034826F8: +0x18 callbacks 0xA03483408, +0x20
   record 0xA03482D18). N-RAW case 0x404804E8: icons x8 = 0xA028C00AC (quality 1) / 0xA028C00CE
   (u16[17]), labels x21 = 0xA028C00F0 (u32[17] 2238..2254), count `mov w9,#17` at 0x40480524. The
   record {count, 1, index, byte; 0, icons; 0} is stored at 0x40480554..0x40480564, then
   `bl 0x404C5E28(0xA034826F8)` at 0x40480568, `bl 0x404793E4(labels)` at 0x40480570 (stores the
   list at 0xA03483CB8; the per-cell label callback 0x40480590 reads list[i]); the per-cell
   availability callback 0x404805AC is `b 0x40508B30` (g10_im_avail maps 17..22). The strip walks
   cells 0..count-1 (0x404C76D8..); its cursor (record +8) moves by the dial's step with wrap at the
   record's count (0x404C7B9C..0x404C7BF0, store 0x404C7BE0).
 - Dial 0x40505CC4(w1 direction 0x4B/0x48 = previous, w2 wrap, x4 &step): w22 file type, current
   `bl 0x40506010(&cur)` at 0x40505D08 -> w20; N-RAW (w22 6, jump table 0x42BB5ED9 + 5 ->
   0x40505E50): x28 = rows High 0x42BBB340 / Normal 0x42BBB4D8, x2 = conditions 0x43AC81B8,
   `mov w1,#17` at 0x40505E90; `bl 0x40504C14(cur-1, 17, ids, cur, wrap)` at 0x40505EB0 or
   `bl 0x40504C88(cur+1, ...)` at 0x40505EC4 (first selectable index in that direction: id via
   0x403AF5C8 bit 0 clear; wraps only with the flag; none -> cur); new == cur -> nothing; row
   x28 + new * 24 (no bound), `bl 0x403FFCEC(keys, values, n)` at 0x40505F68 (no session byte),
   `bl 0x40506010(&new)` at 0x40505F70, step (mod the wrap word 0x42BB8758[5] = 17) stored at [x4].
 - Title (Task A): 0x404793E4 (every strip event) re-makes the pooled title widgets through
   0x40486B58(list[cursor]) (0x900, 0x901 in mode 2; +0 id, +8 label, +0xC type 2 written directly;
   class teardown 0x4032DED4, types 2..4 alike; init 0x4032D7F0 -> 0x40337570 releases a cache
   that does not match type 2 and the label). Then 0x4047945C calls the label callback
   0x40480590 (callbacks +8 is 0) with the cursor (record +8): list[cursor], b 0x4048699C, which
   finds 0x900 / 0x901 and binds `bl 0x404054FC(widget, label)` at 0x40486A04 / 0x40486A98.
   0x404054FC keeps the widget's type (type 3 clamps the value to slot 0x12B) and tails into
   0x4032A570, which refuses a type change from 2 to 3 (hence clear 0x40405598 first, as ENT).
 - Saves (Task C): each patch save is a stock settings save (0x41003BC0 -> settings task -> worker
   0x41003538 -> NOR program when the record changed); stock's own dial pick (0x403FFCEC =
   b 0x410DF72C) changes RAM only. A deferred save at the strip's close (record at 0x404FC4F4 in
   the i-menu item close 0x404FC478) was built and withdrawn: emulator shows 0x404FC478
   is not run when the strip is left by OK, half-press or 'i' (stock the same), and no single strip
   teardown was found statically (the strip's events go through the i-menu table 0x43B32098,
   0xEF entries of {id, flags, handler}; strip items 0xB3..0xB8 -> 0x404802F8). So the dial apply
   saves at each step onto or off a 3:2 position, as the first strip build (emulator-confirmed).
 - The list (17 cells) and its apply 0x404FB4C8 / rollback 0x403FFEF8 / code search 0x404FB2C4
   are as in the first build (see the C header); they keep the 17 regular entries.

Records, set 'im' (all-or-none, needs_q; install in rank order, restore reversed):
 rank 1  g10_im_avail    B  0x40508B8C  S consumer (every phase)
 rank 2  g10_im_codes    BL 0x404FB2C4  S consumer (every phase)
 rank 3  g10_im_slabels  BL 0x40480570  S consumer: labels follow the record (every phase)
 rank 4  g10_im_title    BL 0x40486A04  S: strip title 0x900 ([FX 3:2] text slot on 17..22; Task A)
 rank 5  g10_im_title2   BL 0x40486A98  S: strip title 0x901 (mode 2)
 rank 6  g10_im_dprev    BL 0x40505EB0  S: previous index over 23 / regular only (every phase)
 rank 7  g10_im_dnext    BL 0x40505EC4  S: next index
 rank 8  g10_im_lapply   BL 0x404FB4C8  A: list pick saves open gate off; session start
 rank 9  g10_im_dapply   BL 0x40505F68  A: dial pick saves (3:2 bound / off) before the write
 rank 10 g10_im_rollback BL 0x403FFEF8  A: session-start choice back
 rank 11 g10_im_dnew     BL 0x40505F70  C: new position (step over 23)
 rank 12 g10_im_dcur     BL 0x40505D08  C producer: current position, dial state
 rank 13 g10_im_strip    BL 0x40480568  C producer: 23-entry strip record, installed last

Asset (DERIVED, built from the user's MAIN, recomputed by the verifier): the strip icon lists
(u16[23] for quality 1 and otherwise: stock 0..16, then the twins') and the 23 condition ids
(stock 0..16, then the twins'). ASSETS (C, open_gate_imenu.c): the 23 label numbers."""
import struct

from . import Asset, Code, Derived, bl_word
from ..image import Image, require

# ---- stock words ----
LAPPLY_SITE, DAPPLY_SITE, ROLLBACK_SITE = 0x404FB4C8, 0x40505F68, 0x403FFEF8
AVAIL_SITE, CODES_SITE = 0x40508B8C, 0x404FB2C4
DCUR_SITE, DPREV_SITE, DNEXT_SITE, DNEW_SITE = 0x40505D08, 0x40505EB0, 0x40505EC4, 0x40505F70
STRIP_SITE, LABELS_SITE = 0x40480568, 0x40480570
TITLE_SITES, BIND = (0x40486A04, 0x40486A98), 0x404054FC   # title widget 0x900 / 0x901 bind in 0x4048699C
TITLE_SET, TITLE_MAKE = 0x4048699C, 0x40486B58
LAPPLY, DAPPLY, SESSION_END = 0x403FFE38, 0x403FFCEC, 0x410E14C0
AVAIL, CODES_SEARCH = 0x403AF5C8, 0x403FEF58
MATCH, PREV, NEXT, STRIP_SET, LABELS_SET = 0x40506010, 0x40504C14, 0x40504C88, 0x404C5E28, 0x404793E4
DIAL, STRIP = 0x40505CC4, 0x404802F8
ROWS_HIGH, ROWS_NORMAL = 0x42BBB340, 0x42BBB4D8          # 17 x {size, rate, 0, quality, index, code}
CONDITIONS = 0x43AC81B8                                   # 17 availability ids
WRAP = 0x42BB8758                                         # wrap word per file type - 1 (N-RAW: [5] = 17)
STRIP_OBJ, STRIP_REC = 0xA034826F8, 0xA03482D18
ICONS_HIGH, ICONS_NORMAL, LABELS = 0xA028C00AC, 0xA028C00CE, 0xA028C00F0
LABEL_LIST = 0xA03483CB8
SESSION_BYTE, ITEM = 0xA03427F34, 0xA03488638
STOCK, ROWS = 17, 23
TWINS = (2, 3, 4, 7, 8, 9)                                # 6K 30/25/24p, 4K 30/25/24p
TWIN_PAIRS = ((6, 6), (6, 7), (6, 8), (4, 6), (4, 7), (4, 8))
FOREIGN_ICONS = (0x1641, 0x1640, 0x163F, 0x162F, 0x162D, 0x162B)   # never used

# ---- the derived asset (offsets mirrored in open_gate_imenu.c, IM_OFF_*; checked by the tests) ----
DERIVED_LO, DERIVED_HI = 0x401F4000, 0x401F6000
OFF_ICONS_HIGH, OFF_ICONS_NORMAL, OFF_COND = 0x000, 0x040, 0x080
DERIVED_BYTES = OFF_COND + 4 * ROWS
ASSET = (0x401F1100, 0x401F2000)                          # C asset: the 23 strip labels
LABEL_NUMBERS = tuple(range(2238, 2238 + STOCK)) + tuple(2238 + t for t in TWINS)


def _u32s(image, va, n):
    return list(struct.unpack('<%dI' % n, image.read(va, 4 * n)))


def _u16s(image, va, n):
    return list(struct.unpack('<%dH' % n, image.read(va, 2 * n)))


def stock_rows(image, base):
    rows = [_u32s(image, base + 24 * i, 6) for i in range(STOCK)]
    require([r[4] for r in rows] == list(range(STOCK)) and all(r[2] == 0 for r in rows), 'unexpected i-menu N-RAW rows')
    require([tuple(rows[i][:2]) for i in TWINS] == list(TWIN_PAIRS), 'unexpected i-menu 6K/4K rows')
    return rows


def extend(values):
    """23 entries: stock 0..16, then the twins' as 17..22."""
    return list(values) + [values[t] for t in TWINS]


def tables(original):
    """{name: (offset, bytes)} of the derived asset, every input checked against the stock shape."""
    image = Image(original)
    for base in (ROWS_HIGH, ROWS_NORMAL):
        stock_rows(image, base)
    require(_u32s(image, LABELS, STOCK) == list(LABEL_NUMBERS[:STOCK]), 'unexpected i-menu strip labels')
    require(_u32s(image, WRAP + 4 * 5, 1) == [STOCK], 'unexpected i-menu N-RAW wrap word')
    out = {}
    for name, va, off in (('icons_high', ICONS_HIGH, OFF_ICONS_HIGH), ('icons_normal', ICONS_NORMAL, OFF_ICONS_NORMAL)):
        icons = extend(_u16s(image, va, STOCK))
        require(not set(icons) & set(FOREIGN_ICONS), 'a foreign icon in the i-menu strip')
        out[name] = (off, struct.pack('<%dH' % ROWS, *icons))
    out['conditions'] = (OFF_COND, struct.pack('<%dI' % ROWS, *extend(_u32s(image, CONDITIONS, STOCK))))
    return out


def imenu_tables(original):
    """The derived asset bytes (Derived builder)."""
    blob = bytearray(DERIVED_BYTES)
    spans = []
    for name, (off, raw) in tables(original).items():
        for a, b in spans:
            require(off + len(raw) <= a or b <= off, f'i-menu asset overlap at {name}')
        spans.append((off, off + len(raw)))
        blob[off:off + len(raw)] = raw
    require(len(blob) <= DERIVED_HI - DERIVED_LO, 'i-menu asset too large')
    return bytes(blob)


def _records():
    out = []

    def code(at, callee, symbol, link, cls, wrapper, purpose, stores=False, lookup=True):
        old = bl_word(at, callee) if link else 0x14000000 | (((callee - at) >> 2) & 0x3FFFFFF)
        out.append(Code(address=at, old=old, symbol=symbol, link=link, cls=cls, wrapper=wrapper, purpose=purpose,
                        set='im', rank=len(out) + 1, needs_q=True, all_or_none=True, stores=stores, lookup=lookup))
    code(AVAIL_SITE, AVAIL, 'g10_im_avail', False, 'A', 'S',
         'i-menu availability tail b 0x403AF5C8 -> b g10_im_avail (N-RAW conditions, index 17..22 -> the twin\'s condition; '
         'every phase; the strip\'s per-cell callback reaches it)', lookup=False)
    code(CODES_SITE, CODES_SEARCH, 'g10_im_codes', True, 'A', 'S',
         'i-menu code search bl 0x403FEF58 -> bl g10_im_codes (N-RAW code tables, index 17..22 -> the twin\'s index; every phase)',
         lookup=False)
    code(LABELS_SITE, LABELS_SET, 'g10_im_slabels', True, 'A', 'S',
         'i-menu strip labels bl 0x404793E4 -> bl g10_im_slabels (N-RAW labels -> the 23-entry list while the strip record '
         'is this part\'s; every phase)', lookup=False)
    code(TITLE_SITES[0], BIND, 'g10_im_title', True, 'A', 'S',
         'i-menu strip title bl 0x404054FC -> bl g10_im_title (widget 0x900: a type-3 title of ours back to the label first; '
         'LIVE, cursor 17 + k: the [FX 3:2] text slot 286 + k after the bind; refusal: the twin\'s label; every phase)',
         stores=True)
    code(TITLE_SITES[1], BIND, 'g10_im_title2', True, 'A', 'S',
         'i-menu strip title (mode 2) bl 0x404054FC -> bl g10_im_title2 (widget 0x901, as g10_im_title)', stores=True)
    code(DPREV_SITE, PREV, 'g10_im_dprev', True, 'A', 'S',
         'i-menu dial previous index bl 0x40504C14 -> bl g10_im_dprev (extended dial: search over 23, a 3:2 result -> its '
         'twin index and recorded; otherwise the regular entries; every phase)', stores=True)
    code(DNEXT_SITE, NEXT, 'g10_im_dnext', True, 'A', 'S',
         'i-menu dial next index bl 0x40504C88 -> bl g10_im_dnext (as the previous index)', stores=True)
    code(LAPPLY_SITE, LAPPLY, 'g10_im_lapply', True, 'A', 'A',
         'i-menu list apply bl 0x403FFE38 -> bl g10_im_lapply (position 17..22 -> the twin row\'s values in every phase; LIVE: '
         'the choice saved through z63s_commit before stock\'s write; a pick whose save did not happen writes nothing; '
         'session start recorded)', stores=True)
    code(DAPPLY_SITE, DAPPLY, 'g10_im_dapply', True, 'A', 'A',
         'i-menu dial apply bl 0x403FFCEC -> bl g10_im_dapply (LIVE N-RAW: a recorded 3:2 move saves the choice bound to the '
         'twin\'s size and rate, any other pick saves open gate off, before stock\'s write; no save, no write)', stores=True)
    code(ROLLBACK_SITE, SESSION_END, 'g10_im_rollback', True, 'A', 'A',
         'i-menu session rollback bl 0x410E14C0 -> bl g10_im_rollback (LIVE: the session-start choice saved back before stock '
         're-applies the start size and rate)', stores=True)
    code(DNEW_SITE, MATCH, 'g10_im_dnew', True, 'A', 'C',
         'i-menu dial new index bl 0x40506010 -> bl g10_im_dnew (extended dial: the new position reported so stock\'s step '
         'equals the step over 23 positions; dial state cleared)', stores=True)
    code(DCUR_SITE, MATCH, 'g10_im_dcur', True, 'A', 'C',
         'i-menu dial current index bl 0x40506010 -> bl g10_im_dcur (file type 6 and the strip record this part\'s: the '
         'current position 17 + k when chosen; dial state)', stores=True)
    code(STRIP_SITE, STRIP_SET, 'g10_im_strip', True, 'A', 'C',
         'i-menu strip bl 0x404C5E28 -> bl g10_im_strip (producer may act: strip record count 23, the asset icon list, '
         'the current position)', stores=True)
    return tuple(out)


RECORDS = _records()
STOCK_CALLS = {'stock_im_lapply': LAPPLY, 'stock_im_dapply': DAPPLY, 'stock_im_session': SESSION_END,
               'stock_im_avail': AVAIL, 'stock_im_codes': CODES_SEARCH, 'stock_im_labels': LABELS_SET,
               'stock_im_match': MATCH, 'stock_im_prev': PREV, 'stock_im_next': NEXT, 'stock_im_strip': STRIP_SET,
               'stock_im_bind': BIND}
SAVES = True
ASSETS = (Asset(ASSET[0], ASSET[1], 'IM: the 23 strip label numbers'),)
DERIVED = (Derived(DERIVED_LO, DERIVED_HI, 'z63patch.gate_groups.open_gate_imenu:imenu_tables',
                   'IM: the i-menu N-RAW strip icon lists and condition ids, 23 entries each'),)
