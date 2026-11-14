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
"""G10 part 10b: the recording side of open gate; adapters in
native/arm_time/groups/open_gate_rec.c. Imported by open_gate.py through PARTS.

What an open-gate recording writes, and where the old layout changed it (native/open_gate.S value
sites, native/open_gate_crop_tag.S, native/open_gate_proxy_sar.S), re-sited at B/BL words and
data words only:

 - N-RAW file header (the "container"). Every old value site of the container
   (0x4109EF34, 0x4109F700, 0x4109F718, 0x410A040C, 0x410A0414, 0x410A0434, 0x410A043C) runs
   inside one call: the NRAW header builder 0x4109E61C(w0 = 4), whose per-tag functions are
   reached by `blr` through the NOR tables 0x441C2818 and 0x441C29C0 (outside the text and data
   windows: no record possible there). The builder has two direct callers and no data
   pointer, so the two BL words 0x40E7A10C and 0x40E7A174 become a post-fix that edits the
   finished 0x400-byte NRAW box (walked by its own sizes) exactly as the old sites did:
   NRMT 0x01100101 coded rows, NRMT 0x0190000B ratio (4K only) and IFD tag 0x45 (in NRMT
   0x02000001) active rows. The design's "B 0x4109EC88 -> 0x4109EEC0" tail does not reach these
   sites (0x4109EEC0 is "b 0x4109FAFC").
 - NCTG descriptor words (the .data copy at 0xA02993400, stride 0x40, read live):
   0xA029934F0 height getter 0x40706FA8 (old sites 0x40707028 / 0x40707018), 0xA02993870 proxy
   main-size writer 0x40707404 (old sites 0x40707500 / 0x407074E8), 0xA02993E70 crop tag getter
   0x40707D28 (old entry hook). The width getter 0xA029934B0 is not taken: the old layout never
   changed the width (6048), so its wrapper would be pass-through only.
 - Proxy SPS (old entry hook at 0x40CEF168, the avcC sample-entry writer): its two BL callers
   0x403526C4 and 0x40353ECC (no data pointer).

One all-or-none set `rec` (needs_q, installed and restored under Q like `geom`). Every adapter
decides on the kind the recording was planned with (10a's plan latch, og_kind_latched)
confirmed by the live decision, and edits only an output that holds the stock value of that
kind's path (received geometry), so a recording keeps one geometry through DRAINING; an
integrity break passes through at once. Words claimed by other groups on the same
descriptor table: G8 0xA029935B0 / 0xA02993F70, site nctg_class 0xA02993530; G6 dropped
0xA02993530 / 0xA02993870 (tests check that no word is declared twice)."""
from . import Code, Data, bl_word

NRAW_BUILDER = 0x4109E61C
NRAW_SITES = (0x40E7A10C, 0x40E7A174)
NCTG_HEIGHT, NCTG_PROXY, NCTG_CROP = 0x40706FA8, 0x40707404, 0x40707D28
NCTG_HEIGHT_AT, NCTG_PROXY_AT, NCTG_CROP_AT = 0xA029934F0, 0xA02993870, 0xA02993E70
NCTG_WIDTH_AT, NCTG_WIDTH = 0xA029934B0, 0x40706F18          # not taken (see above)
SPS_WRITER = 0x40CEF168
SPS_SITES = (0x403526C4, 0x40353ECC)
# Old-layout value sites now covered by the post-fixes above (site, stock, open, size).
OLD_SITES = ((0x4109EF34, 3410, 4040, 6), (0x4109EF34, 2276, 2692, 4), (0x4109F718, 3410, 4040, 6),
             (0x4109F700, 2276, 2692, 4), (0x410A0434, 3402, 4032, 6), (0x410A043C, 3410, 4040, 6),
             (0x410A040C, 2268, 2688, 4), (0x410A0414, 2276, 2692, 4), (0x40707028, 3402, 4032, 6),
             (0x40707018, 2268, 2688, 4), (0x40707500, 3402, 4032, 6), (0x407074E8, 2268, 2688, 4))
STOCK_CALLS = {'stock_og_nraw_header': NRAW_BUILDER, 'stock_og_nctg_height': NCTG_HEIGHT,
               'stock_og_nctg_proxy': NCTG_PROXY, 'stock_og_nctg_crop': NCTG_CROP, 'stock_og_sps': SPS_WRITER}


def _records():
    out = []

    def code(at, callee, symbol, purpose):
        out.append(Code(address=at, old=bl_word(at, callee), symbol=symbol, link=True, cls='A', wrapper='C',
                        purpose=purpose, set='rec', rank=len(out) + 1, needs_q=True, all_or_none=True, stores=True))

    def data(at, old, symbol, purpose):
        out.append(Data(address=at, old=old, symbol=symbol, cls='D', wrapper='C', purpose=purpose,
                        set='rec', rank=len(out) + 1, needs_q=True, all_or_none=True, stores=True))
    for at in NRAW_SITES:
        code(at, NRAW_BUILDER, 'g10_nraw_header',
             'NRAW header builder call: bl 0x4109E61C -> bl g10_nraw_header (stock builder, then for w0 = 4 the open-gate '
             'rows of NRMT 0x01100101 / 0x0190000B / IFD 0x45, old sites 0x4109EF34, F700, F718, A040C-A043C)')
    data(NCTG_HEIGHT_AT, NCTG_HEIGHT, 'g10_nctg_height',
         'NCTG height getter word (descriptor +0x30): 0x40706FA8, then record +0xA6 3402 -> 4032 / 2268 -> 2688 '
         '(old sites 0x40707028 / 0x40707018)')
    data(NCTG_PROXY_AT, NCTG_PROXY, 'g10_nctg_proxy',
         'NCTG proxy main-size writer word: 0x40707404, then record +0x104 3402 -> 4032 / 2268 -> 2688 '
         '(old sites 0x40707500 / 0x407074E8)')
    data(NCTG_CROP_AT, NCTG_CROP, 'g10_nctg_crop',
         'NCTG crop tag 0x0200001B getter word: 0x40707D28, then record +0x336 = (11, 6064, 4040, 6064, 4040, 0, 0) '
         'for the main file (old entry hook z63_og_crop_entry)')
    for at in SPS_SITES:
        code(at, SPS_WRITER, 'g10_proxy_sps',
             'avcC sample-entry writer call: bl 0x40CEF168 -> bl g10_proxy_sps (a known stock 1920x1080 proxy SPS '
             'replaced by the 27:32 SAR SPS; old entry hook z63_og_sar_entry)')
    return tuple(out)


RECORDS = _records()
