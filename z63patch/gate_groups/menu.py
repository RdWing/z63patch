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
"""G2, shared menu and settings saves.

G2 owns the patch's video-menu rows, their sub-pages, the text slots that label them and the one
persistence path of the gate (native/arm_time/save_api.h, with the save-result observer). It reads
the settings word 0xA03F0EEF4 as stored (never the published copy) and writes it only through
z63s_commit. Native code: native/arm_time/groups/menu.c.

Records (6 plus the core's page record P, video-menu set 1, rank 1):
  events  data64 0xA028CAE28  -> z63_g2_events (48 event-table pointers, asset)   video_menu rank 2
  count   u16    0xA028CAE30  0x2D -> 0x30 (48 rows)                           video_menu rank 3
  refresh site video_refresh 0xA028F5600 (core adapter; hook z63h_video_refresh_g2) video_menu rank 4
  entry   data64 0xA028F55E0  -> z63_g2_video_entry                               video_menu rank 5
  sub     data64 0xA028F5928  -> z63_g2_sub_entry                                 video_menu rank 6
  save    code32 0x41003A7C   bl 0x41003538 -> bl z63_g2_save_result (TX0 pin, no Q)
The core copies the 48-row template z63_g2_rows to page +0 when G2 is selected (rows=...).
Install runs P, events, count, refresh, entry, sub; restore the reverse, so every intermediate
count is at most the length of the arrays in place (45 or 48).

Rows (fixed whatever the composition, so G2's bytes never depend on it): the 45 stock rows with
three rows inserted after the stock N-RAW quality row (index 9), made from it:
  10 quality   item 10675  label 252 value 253   always enabled (it holds "Reset patch settings")
  11 desqueeze item 11677  label 260 value 261   enabled only while G7 is live
  12 log type  item 11763  label 276 value 277   enabled only while G8 is live
Every inserted row and option uses an item id that no stock row uses (checked against the image
in tests/test_arm_time_s7_menu.py; the old layout's 11699..11713 are stock ids, see the report).

Contract for the groups whose settings G2 shows and saves (G4, G5, G6, G7, G8; G10 saves its own
field through save_api.h). Groups cannot call each other's code, so the contract is data only:

 1. The field. Each group owns one field of the shared v2/v3 record (native/feature_settings.c,
    Z63_SETTINGS_SHARED_V3): G4 `prores` (word bits 8-9), G5 `h265` (10-12), G7 `desqueeze`
    (13-15), G8 `log_colour` (16-17), G6 `nraw` (18-19, meaningful in a v3 record only), G10
    `open_gate` (20). A consumer reads only its own field, from z63g_effective() inside an ACT
    hold, decoded with z63_settings_decode (FOREIGN or EMPTY = its default 0, which is stock).
    G2 never writes another bit; every save keeps the other fields and the low byte (Nikon's).
 2. Liveness. A group's row or options are live in G2's menu exactly when z63g_phase(page, id)
    is nonzero for that group (selected at arming, LIVE or DRAINING, not broken). Otherwise the
    row shows "Off" and is greyed (desqueeze, log), its parent guard refuses the sub-page, and
    the quality sub-page lists only "Reset patch settings". A save of a group's field is admitted
    (z63s_commit, TX0) only while that group is LIVE, so an unarmed group's field never changes.
    Nothing has to be declared by the other group: it attaches by being armed.
 3. Where the choices are. The quality row's sub-page follows the recording file type read from
    Z63_SLOT_FILE_TYPE (0xA03F0EEE0): type 4 -> G4 grades HQ/Standard/LT/Proxy (prores 0-3);
    types 2 and 3 -> G5 tiers 100/60/45/30/20 % (h265 0-4); type 6 -> G6 tiers High/Fine/Normal/
    Basic (nraw 0-3, saved as a v3 record, only within Nikon's class: High/Fine while key
    0x05000F55 (slot Z63_SLOT_KEY_0F55) and key 0x0400201B are 1, Normal/Basic while both are
    0; G2 never calls the stock setter, so a change of class stays with the stock N-RAW row);
    any other type -> no options. "Reset patch settings" is always the last option and
    clears bits 8-31 through the same save. The desqueeze row (G7) offers 8 ratios
    (desqueeze 0-7), the log row (G8) N-Log/S-Log3/Log3G10 (log_colour 0-2).
 4. Labels. G2 draws every label and value into its own text slots 252-281. A group that needs a
    different label set (G4/G5 target-rate labels, G8's tone page) is a core change; it does
    not write G2's slots.
 5. Publication. G2 does not publish. The core publishes the stored word at the armed poll (
    under Q, or after 4 polls of native idle when every selected group has movie_publish), so a
    consumer sees a new value at its next ACT hold after a save. G2 sets movie_publish=True: it
    never reads the published word (it shows the stored one), so publication timing cannot change
    anything G2 does during a recording.

Stock-derived tables (the 45 stock rows, the 45 stock event pointers, the stock N-RAW-row
transition table, the stock sub-page elements, option row and OK transition table) are not in
the repository: they are generated at build time from the user's own MAIN (SHA-256 checked) into
a header the compiler force-includes: Group.stock_header names stock_header() below, and
gate.link_group writes its output into the build's temporary directory and passes -include
(this replaced the tempdir cache of G2-REQUESTS R1). menu.c refuses to
compile without it. The generated bytes are a pure function of the stock image, so the group's code
and asset hashes do not depend on where the image was found."""
import hashlib
import struct

from . import Group, Code, Data, Value16, Shared, Entry, Asset, Sanitise, SAVE_TERM, VIDEO_MENU_SET, bl_word

STAGES = {'s7-menu': ('marker', 'menu'), 's7-menu-h265': ('marker', 'menu', 'h265')}

MAIN_SHA256 = '236bbd874e0942fd91fb54a99af456f4ebcc0fd966af930753c43efd1839fe87'
BASE, DATA_VA, DATA_OFFSET = 0x40160000, 0xA02808000, 0x5095000
VIDEO_ROWS, VIDEO_EVENTS, VIDEO_COUNT = 0xA028CAE20, 0xA028CAE28, 0xA028CAE30
ROWS_STOCK, EVENTS_STOCK = 0x43CB3868, 0x43CB4028
SUB_PAGE = 0xA028CB2C0
SUB_STOCK = (0x43CB7C58, 0x43CB7D68, 0x43CB7DC0, 2)        # elements, items, events, count
VIDEO_ENTRY, VIDEO_ENTRY_STOCK = 0xA028F55E0, 0x40595E3C
SUB_ENTRY, SUB_ENTRY_STOCK = 0xA028F5928, 0x4058FDC0
OBSERVER = 0x41003A7C
OBSERVER_OLD = bl_word(OBSERVER, 0x41003538)                  # bl 0x41003538
ASSET = (0x401A6000, 0x401AA000)
OPEN_SUBPAGE, OK_EVENT, PARENT_GUARD, GUARD_2AE = 0x119, 691, 0x403C2D14, 0x403C2D0C
# Item ids G2 puts in rows (checked against every stock row by the tests).
ITEMS = dict(ITEM_VIDEO=10675, ITEM_OPT_0=11601, ITEM_OPT_1=11603, ITEM_OPT_2=11671, ITEM_OPT_3=11673,
             ITEM_OPT_4=11675, ITEM_RESET=11717, ITEM_DESQ=11677, ITEM_DQ_0=11679, ITEM_DQ_1=11681,
             ITEM_DQ_2=11683, ITEM_DQ_3=11685, ITEM_DQ_4=11719, ITEM_DQ_5=11749, ITEM_DQ_6=11751,
             ITEM_DQ_7=11761, ITEM_LOG=11763, ITEM_LG_0=11605, ITEM_LG_1=11780, ITEM_LG_2=11720)
TEXT_SLOTS = (252, 253, 254, 255, 256, 257, 258, 259, 260, 261, *range(262, 270), 276, 277, 278, 279, 280, 281)


def off(va):
    if BASE <= va < BASE + DATA_OFFSET:
        return va - BASE
    if DATA_VA <= va < DATA_VA + 0x3D9028:
        return va - DATA_VA + DATA_OFFSET
    raise ValueError(f'unmapped {va:#x}')


def _need(ok, why):
    if not ok:
        raise ValueError(f'G2 stock tables: {why}')


def stock_tables(stock):
    """The stock-derived inputs of G2's tables, read and checked from the user's MAIN."""
    _need(hashlib.sha256(stock).hexdigest() == MAIN_SHA256, 'stock C2.01 MAIN required')
    q = lambda va: struct.unpack_from('<Q', stock, off(va))[0]
    _need((q(VIDEO_ROWS), q(VIDEO_EVENTS), struct.unpack_from('<H', stock, off(VIDEO_COUNT))[0]) == (ROWS_STOCK, EVENTS_STOCK, 45),
          'video page words')
    _need((q(SUB_PAGE + 8), q(SUB_PAGE + 16), q(SUB_PAGE + 24), struct.unpack_from('<H', stock, off(SUB_PAGE + 32))[0]) == SUB_STOCK,
          'sub-page descriptor')
    _need((q(VIDEO_ENTRY), q(VIDEO_ENTRY + 0x20), q(SUB_ENTRY)) == (VIDEO_ENTRY_STOCK, 0x40596140, SUB_ENTRY_STOCK), 'handler words')
    _need(struct.unpack_from('<I', stock, off(OBSERVER))[0] == OBSERVER_OLD, 'observer call')
    rows = [struct.unpack_from('<11I', stock, off(ROWS_STOCK + 44 * i)) for i in range(45)]
    events = list(struct.unpack_from('<45Q', stock, off(EVENTS_STOCK)))

    def table(at):
        out = []
        for i in range(80):
            r = struct.unpack_from('<4Q', stock, off(at + 32 * i))
            out.append(r)
            if r[0] == 1010:
                return out
        raise ValueError('G2 stock tables: unterminated event table')
    nraw = table(events[9])
    opens = [r for r in nraw if r[3] == OPEN_SUBPAGE]
    _need(opens and all(r[2] in (0, PARENT_GUARD) for r in opens), 'N-RAW row transitions')
    _need(rows[9][6] == 0x31B5, 'stock row 9 is not the N-RAW quality row')
    elements = [struct.unpack_from('<17I', stock, off(SUB_STOCK[0] + 68 * i)) for i in range(4)]
    _need(sum(1 for e in elements if e[1] == 1) == 1, 'sub-page title element')
    item = struct.unpack_from('<11I', stock, off(SUB_STOCK[1]))
    sub = table(q(SUB_STOCK[2]))
    ok = {r[1] for r in sub if r[0] == OK_EVENT}
    _need(len(ok) == 1 and next(iter(ok)), 'ambiguous OK action')
    return dict(rows=rows, events=events, nraw=nraw, elements=elements, item=item, sub=sub, ok=next(iter(ok)))


def stock_header(stock):
    """The generated C header (X-macros of the stock tables) for native/arm_time/groups/menu.c."""
    t = stock_tables(stock)
    u = lambda v: f'{v:#x}u'
    ull = lambda v: f'{v:#x}ull'
    lines = ['/* Generated by z63patch/gate_groups/menu.py from the stock C2.01 MAIN at build time. Not in the',
             ' * repository. */', '#define Z63_G2_STOCK 1']
    for i, r in enumerate(t['rows']):
        lines.append(f'#define Z63_G2_ROW_{i}(X) X({",".join(u(w) for w in r)})')
    for i, e in enumerate(t['events']):
        lines.append(f'#define Z63_G2_EVENT_{i} {ull(e)}')
    lines.append(f'#define Z63_G2_NRAW_ROWS {len(t["nraw"])}u')
    lines.append('#define Z63_G2_NRAW(X) ' + ' '.join(f'X({",".join(ull(w) for w in r)})' for r in t['nraw']))
    lines.append(f'#define Z63_G2_SUB_ROWS {len(t["sub"])}u')
    lines.append('#define Z63_G2_SUB_EVENTS(X) ' + ' '.join(f'X({",".join(ull(w) for w in r)})' for r in t['sub']))
    lines.append(f'#define Z63_G2_SUB_OK {ull(t["ok"])}')
    lines.append('#define Z63_G2_ELEMENTS(X) ' + ' '.join(f'X({",".join(u(w) for w in e)})' for e in t['elements']))
    lines.append(f'#define Z63_G2_SUB_ITEM(X,slot,id) X(slot,id,{",".join(u(w) for w in t["item"])})')
    return '\n'.join(lines) + '\n'


_PURPOSE_SAN = 'restoration sanitiser: the sub-page descriptor left on a G2 page goes back to the stock N-RAW page'
GROUP = Group(
    id=2, name='menu', title='shared menu and settings saves',
    source='native/arm_time/groups/menu.c',
    slot=(0x401B7000, 0x401BC000),
    records=(
        Data(address=VIDEO_EVENTS, old=EVENTS_STOCK, symbol='z63_g2_events', cls='D', wrapper='S', target='data',
             set=VIDEO_MENU_SET, rank=2, needs_q=True,
             purpose='video menu event-table pointers 0xA028CAE28 -> z63_g2_events (48 rows; N-RAW row through z63_g2_open_nraw)'),
        Value16(address=VIDEO_COUNT, old=0x2D, new=0x30, cls='D', set=VIDEO_MENU_SET, rank=3, needs_q=True,
                purpose='video menu row count 0xA028CAE30: 45 -> 48'),
        Data(address=VIDEO_ENTRY, old=VIDEO_ENTRY_STOCK, symbol='z63_g2_video_entry', cls='D', wrapper='C',
             set=VIDEO_MENU_SET, rank=5, needs_q=True, stores=True,
             purpose='video menu entry handler 0xA028F55E0 -> z63_g2_video_entry (texts, then the stock entry)'),
        Data(address=SUB_ENTRY, old=SUB_ENTRY_STOCK, symbol='z63_g2_sub_entry', cls='D', wrapper='C',
             set=VIDEO_MENU_SET, rank=6, needs_q=True, stores=True,
             purpose='sub-page entry handler 0xA028F5928 -> z63_g2_sub_entry (G2 pages; the stock entry for every other page)'),
        Code(address=OBSERVER, old=OBSERVER_OLD, symbol='z63_g2_save_result', link=True, cls='S', wrapper='S',
             needs_q=False, stores=True, tx=1,
             purpose='settings worker save call 0x41003A7C: bl 0x41003538 -> bl z63_g2_save_result (records the result of a G2-admitted save)'),
    ),
    shared=(Shared(site='video_refresh'),),
    entries=(Entry('z63_g2_open_nraw', wrapper='S', stores=True), Entry('z63_g2_open_quality', wrapper='S', stores=True),
             Entry('z63_g2_open_desqueeze', wrapper='S', stores=True), Entry('z63_g2_open_log', wrapper='S', stores=True),
             Entry('z63_g2_guard_desqueeze', wrapper='S'), Entry('z63_g2_guard_desqueeze_parent', wrapper='S'),
             Entry('z63_g2_guard_log', wrapper='S'), Entry('z63_g2_guard_log_parent', wrapper='S'),
             Entry('z63_g2_pick_quality', wrapper='A', stores=True), Entry('z63_g2_pick_reset', wrapper='A', stores=True),
             Entry('z63_g2_pick_desqueeze', wrapper='A', stores=True), Entry('z63_g2_pick_log', wrapper='A', stores=True)),
    rows=('z63_g2_rows', 48),
    assets=(Asset(ASSET[0], ASSET[1], 'G2 menu tables: 48-row template, event pointers, transitions, sub-page items'),),
    sanitise=(Sanitise(SUB_PAGE + 8, 8, SUB_STOCK[0], ASSET[0], ASSET[1], _PURPOSE_SAN + ' (elements)'),
              Sanitise(SUB_PAGE + 16, 8, SUB_STOCK[1], ASSET[0], ASSET[1], _PURPOSE_SAN + ' (items)'),
              Sanitise(SUB_PAGE + 24, 8, SUB_STOCK[2], ASSET[0], ASSET[1], _PURPOSE_SAN + ' (events)'),
              Sanitise(SUB_PAGE + 32, 2, SUB_STOCK[3], 1, 2, _PURPOSE_SAN + ' (count 1, reset only)', when=(0, 1, 2)),
              Sanitise(SUB_PAGE + 32, 2, SUB_STOCK[3], 3, 9, _PURPOSE_SAN + ' (count 3-8)', when=(0, 1, 2))),
    text_slots=TEXT_SLOTS,
    state=(0x840, 112),
    stock_calls={'stock_video_entry': VIDEO_ENTRY_STOCK, 'stock_sub_entry': SUB_ENTRY_STOCK,
                 'stock_save_worker': 0x41003538, 'stock_save_request': 0x41003BC0,
                 'stock_row_value': 0x4052C900, 'stock_text_store': 0x404066C8, 'stock_text_bind': 0x4040A1AC,
                 'stock_widget_find': 0x403ADA94, 'stock_widget_show': 0x40409F74, 'stock_row_enable': 0x404054A8,
                 'stock_entry_guard': 0x403BF168, 'stock_set_cursor': 0x403C0F8C, 'stock_set_mark': 0x403C0FFC,
                 'stock_parent_guard': PARENT_GUARD, 'stock_font_cache': 0x40339E44, 'stock_language': 0x4033994C,
                 'stock_font_face': 0x40339F9C, 'stock_font_size': 0x4033A0A8, 'stock_font_align': 0x4033A170},
    save_calls={'stock_save_request': 0x41003BC0},
    install_needs_q=True, terms=(SAVE_TERM,),
    hook_stores=True,
    stock_header='z63patch.gate_groups.menu:stock_header',   # -include from the build's temporary directory (gate.link_group)
    replay_tests=('tests/test_arm_time_s7_menu.py',),
    movie_publish=True)   # G2 never reads the published word (menus show the stored word)
