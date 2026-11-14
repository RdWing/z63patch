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
"""G10 part ENT: the [FX 3:2] entries on the stock Frame size/frame rate page;
adapters in native/arm_time/groups/open_gate_entries.c.

Stock facts (C2.01 MAIN, read with capstone; guarded by tests/test_arm_time_s7_opengate_ent.py):

 - Object 280's page record 0xA028CB270 (only reference: data word 0xA029094A0) holds rows
   0x43CB628C (44 B, id at +0x18), events 0x43CB7860 and the u16 count 127. Row i and mode record i
   (table 0x42BD02DC, 24 B {id, type, rate, size, extra, icon}) are the same row in the same order.
   Rows are found by id everywhere (0x403C07B8 index by id, bounded; 0x403C0F8C cursor;
   0x403C0FFC mark, whose second loop at 0x403C1080 is unbounded: the id must be a row).
 - The mode table is built in code only, at the list 0x4058F72C, the selection 0x405938CC and the
   summary 0x40596430; no data word points to it.
 - The six rows ENT needs are already on the page: 0x3192/0x3193/0x3194 (rows 70-72, records
   (6, rate 6/7/8, size 6)) and 0x3199/0x319A/0x319B (rows 82-84, (6, 6/7/8, 4)). Their records
   equal those of the visible 6K and 4K 30/25/24p rows 0x3203-0x3205 (75-77) and 0x3208-0x320A
   (87-89), their event tables are equal in content (selection 0x4059377C), and they sit directly
   above those rows (the old layout's insert positions). The page initializer 0x4058F68C hides
   them at the end of every pass with six explicit bl 0x403C2F78(ctx, index, 1).

So the design's 2.9 design (private 133-record table, page record swap, Data records, count
values) is not needed: no table, count or pointer changes, nothing derived, nothing sanitised.
Ten BL->BL Code records, one all-or-none set 'ent' (needs_q). Install order (rank ascending):
selection, cursor, mark, then the six show sites; restore in reverse. So whenever an [FX 3:2] row
can be visible, its selection save and the current-row mapping are installed; with a partial set
the rows stay hidden (stock) and the installed adapters only map ids of hidden rows.

Selection behaviour (LIVE, N-RAW): an [FX 3:2] row saves bit 20 = 1 through z63s_commit and
stock writes the twin's native size and rate; any other N-RAW row saves bit 20 = 0. A refused or
failed save of an [FX 3:2] row writes nothing (lookup -1, stock's no-record path). The page marks
the [FX 3:2] row when the STORED word has bit 20. PASS/DRAINING: every adapter makes the stock
call with the caller's registers, the rows stay hidden and a stored bit 20 is ignored.

Row icons (part ICONS; guarded by the same test module):
 - The list builder 0x403B6D98 builds all 127 rows in one pass. A row is its own widget
   (0x403BA6F8: id row+0x18, value row+0 = static string id, type 2 for flags 0x00101005) and,
   from layout 5 (row+0x1C; kinds 0x80000, 0x40, 4), the child 0x4DC0 (0x403BA900: type 1 image,
   value row+0x14, the load at 0x403BA9A0). The image is drawn by 0x40336D74 (type & ~4 == 1),
   which hands the value to the resource reader 0x40339A60 at 0x40336DB4.
 - So the list draws row+0x14: 0x1641/0x1640/0x163F and 0x162F/0x162D/0x162B for the six hidden
   rows ("8.3K"/"4.1K" P-star artwork of other bodies) and 0x1824/0x1826/0x1828, 0x183A/0x183C/
   0x183E for the twins. The mode record's last word is read only by the summary, for records
   outside 68..103; for N-RAW records 68..103 the summary (0x40596430..0x405964D8) draws table
   0x42BD1094 (12 B {id, icon star, icon}; +4 when setting 0x05000F55 reads 1, else +8) of the
   matched record, and the matcher's last match is the twin. The live-view indicator (0x404308A0;
   N-RAW u16 pairs 0x42BBAA5C) and the i-menu tile (0x404EDC88, 24 B records) are keyed by the
   setting values and hold none of the six numbers.
 - The six numbers are also words of another page's templates (0x43CA95A0.., page records
   0xA028CA788 / 0xA028CAB48) and of table 0x42BD1094, so a resource-reader remap is not
   harmless; ENT does not change any resource. g10_ent_show sets the row's child 0x4DC0 to the
   twin's icon (0x40409F44(child, 1, icon): the stock setter, accepted only while the child is
   type 1) in the same call that shows the row; a row whose child is missing or refuses the value
   stays hidden.
 - TEXT_BIND keeps the widget type (0x40331D04 refuses a change from a nonzero type), so the
   [FX 3:2] text cannot be shown through a slot on these rows (the earlier bind of slot 286-291 to
   the image child drew image resources 286-291); the text is stored, not bound
   (G10-ICONS-REQUESTS.md R-ICONS-1).

Row label (switch ROW_LABEL below: the value the default build GROUP uses; the other value is
the variant open_gate_<label> with stage s7-opengate-<label>, open_gate.py; both adapters are in
every build and the switch names the one the six show records call):
 - 'slot' (default; option 2): g10_ent_show stores the [FX 3:2] text in slot 286-291 in stock's
   format ("[FX] 6048x3402;  30p" style, English table ids 0x8BE..0x8C7) with the open-gate frame,
   sets the image child to the twin's icon, releases the row widget's static string with the
   toolkit clear 0x40405598 -> 0x4032ACC0 (types 2..4: the text-layout cache +0x68 is destroyed
   and freed by stock and nulled; type 0) and sets type 3 with the slot (0x40409F44(row, 3, slot);
   accepted from type 0). The row is found by 0x403ADA94(id, ctx+0x18) and must have the row
   class's teardown 0x4032B7C8 at +0x58 and the stock label 0x448 ("Disable"), or the slot from an
   earlier pass. The draw 0x40337984 remakes a missing cache (0x40337570) from type and value; the
   teardown 0x4032B7C8 treats types 2..4 alike.
 - 'mark' (option 3, fallback): g10_ent_show_mark keeps the row widget at type 2 and sets the stock
   string 0x31F "FX" (identical in all 29 languages; 0x40409F44(row, 2, 0x31F)); the image child
   takes the extension id 0x7019 + k with G3 LIVE (the twin's icon with a "3:2" mark, derived at
   build time by z63patch.open_gate_icons into DERIVED below; G3's table rows 0x7019..0x701E point
   at it, gate_icons.py and indicators.py, only in this variant), else the twin's stock icon.
In both, every check precedes the first change, each setter must return 1, and any refusal keeps
the row hidden (stock)."""
from . import Asset, Code, Derived, bl_word

LOOKUP, CURSOR, MARK, ROW_STATE = 0x403FEFE0, 0x403C0F8C, 0x403C0FFC, 0x403C2F78
ROW_CHILD, TEXT_STORE, TEXT_BIND = 0x4052C900, 0x404066C8, 0x4040A1AC
SET_VALUE = 0x40409F44          # (widget, type, value) -> 0x40331D04: same type (or 0) only; w0 1 = set
SEL_SITE = 0x405938F0           # selection 0x4059377C: lookup(table, 127, 24, row id, -1)
CURSOR_SITE = 0x4058F824        # initializer: cursor(ctx, matched id), when 0x403BF168() != 1
MARK_SITE = 0x4058F834          # initializer: mark(ctx, matched id, 1)
# (call site, [FX 3:2] row id, row index, visible twin id) - the forced hides in 0x4058F68C.
SHOW_SITES = ((0x4058F930, 0x3192, 70, 0x3203), (0x4058F94C, 0x3193, 71, 0x3204), (0x4058F968, 0x3194, 72, 0x3205),
              (0x4058F9F4, 0x3199, 82, 0x3208), (0x4058FA10, 0x319A, 83, 0x3209), (0x4058FA2C, 0x319B, 84, 0x320A))
PAGE_RECORD, ROWS, EVENTS, COUNT, TABLE = 0xA028CB270, 0x43CB628C, 0x43CB7860, 127, 0x42BD02DC
ASSET = (0x401F1000, 0x401F1100)
LABELS = ('[FX 3:2] 6048x4032;  30p', '[FX 3:2] 6048x4032;  25p', '[FX 3:2] 6048x4032;  24p',
          '[FX 3:2] 4032x2688;  30p', '[FX 3:2] 4032x2688;  25p', '[FX 3:2] 4032x2688;  24p')
TEXT_SLOTS = (286, 287, 288, 289, 290, 291)
# Row icons: the six hidden rows' stock row+0x14 (never shown) and the visible twins' row+0x14.
FOREIGN_ICONS = (0x1641, 0x1640, 0x163F, 0x162F, 0x162D, 0x162B)
TWIN_ICONS = (0x1824, 0x1826, 0x1828, 0x183A, 0x183C, 0x183E)
ICON_CHILD = 0x4DC0             # the row's image child (0x403BA900)
SUMMARY_ICONS = 0x42BD1094      # summary: {id, icon star, icon} for records 68..103
LIST_BUILDER, ROW_WIDGET, ICON_WIDGET, IMAGE_DRAW, IMAGE_READ_SITE = 0x403B6D98, 0x403BA6F8, 0x403BA900, 0x40336D74, 0x40336DB4
SAVES = True
# Row label: 'slot' (option 2, the [FX 3:2] text slot on the row widget) or 'mark' (option 3,
# "FX" and the marked icon). Change here only; the other value is the variant (open_gate.py).
ROW_LABEL = 'slot'
VARIANTS_ROW_LABEL = ('slot', 'mark')
FIND, CLEAR = 0x403ADA94, 0x40405598   # widget of an id (ctx+0x18 list); toolkit clear -> 0x4032ACC0
ROW_CLASS = 0x4032B7C8          # the row widget's +0x58 method (teardown)
STOCK_LABEL, FX_LABEL = 0x448, 0x31F   # "Disable"; "FX"
MARK_IDS = tuple(range(0x7019, 0x701F))
SHOW_SYMBOL = {'slot': 'g10_ent_show', 'mark': 'g10_ent_show_mark'}
# The page's N-RAW icon refresh (G10-UI Task 1): vtable 0xA028F5900 slot +0x20 of object 280
# (its only data reference is the object entry 0xA02909478, page record word 0xA029094A0). For file
# type 6 it re-binds every N-RAW row's child 0x4DC0 from ICON_TABLE (36 x {id, icon when 0x05000F55
# reads 1, icon otherwise}) with `bl TEXT_BIND` at ICON_SITE (x24 entry, x19 window, x20 quality),
# after the list routine: this is what drew the foreign artwork on the shown rows (emulator).
# A hidden row's entry is ICON_TWIN bytes before its twin's.
REFRESH, ICON_SITE, ICON_TABLE, ICON_TWIN, ICON_ENTRIES = 0x4058FD24, 0x4058FD9C, 0x42BD1094, 0x3C, 36
ICON_FX_ENTRIES = (2, 3, 4, 14, 15, 16)          # the six hidden rows' entries; twins at +5
ROW_HIDDEN = 0xA03415A48                         # + id - 0x298A: 0x403C2F78's hidden byte per row
ICON_SYMBOL = {'slot': 'g10_ent_icons', 'mark': 'g10_ent_icons_mark'}

STOCK_CALLS = {'stock_ent_lookup': LOOKUP, 'stock_ent_cursor': CURSOR, 'stock_ent_mark': MARK,
               'stock_ent_row_state': ROW_STATE, 'stock_ent_row_child': ROW_CHILD,
               'stock_ent_text_store': TEXT_STORE, 'stock_ent_set_value': SET_VALUE,
               'stock_ent_find': FIND, 'stock_ent_clear': CLEAR, 'stock_ent_text_bind': TEXT_BIND}


def _records(label):
    out = []

    def add(at, callee, symbol, cls, wrapper, purpose, stores=False, tx=0):
        out.append(Code(address=at, old=bl_word(at, callee), symbol=symbol, link=True, cls=cls, wrapper=wrapper,
                        purpose=purpose, set='ent', rank=len(out) + 1, needs_q=True, all_or_none=True,
                        stores=stores, tx=tx))
    add(SEL_SITE, LOOKUP, 'g10_ent_lookup', 'A', 'A',
        'frame-size selection: bl 0x403FEFE0 -> bl g10_ent_lookup (LIVE N-RAW: [FX 3:2] row saves bit 20 = 1, '
        'other rows bit 20 = 0, through z63s_commit; refused [FX 3:2] save -> -1, nothing written)', stores=True)
    add(CURSOR_SITE, CURSOR, 'g10_ent_cursor', 'A', 'C',
        'frame-size page cursor: bl 0x403C0F8C -> bl g10_ent_cursor (stored bit 20: twin id -> [FX 3:2] row id)',
        stores=True)   # the settings decoder writes its result through a pointer into ent_stored_on's frame
    add(MARK_SITE, MARK, 'g10_ent_mark', 'A', 'C',
        'frame-size page mark: bl 0x403C0FFC -> bl g10_ent_mark (stored bit 20: twin id -> [FX 3:2] row id)',
        stores=True)   # as the cursor
    # before the show records: whenever a row can be shown, the refresh's bind maps its icon
    add(ICON_SITE, TEXT_BIND, ICON_SYMBOL[label], 'A', 'C',
        f'frame-size page N-RAW icon refresh 0x4058FD24: bl TEXT_BIND 0x4040A1AC -> bl {ICON_SYMBOL[label]} (a visible '
        '[FX 3:2] row\'s child: the twin entry\'s icon for the same quality' + (', 0x7019 + k with G3 LIVE' if label == 'mark' else '') +
        '; LIVE/DRAINING; else stock)')
    sym = SHOW_SYMBOL[label]
    for k, (at, rid, idx, twin) in enumerate(SHOW_SITES):
        what = (f'child 0x4DC0 = twin {twin:#x}\'s icon {TWIN_ICONS[k]:#x}, row widget = text slot {286 + k}' if label == 'slot' else
                f'child 0x4DC0 = {MARK_IDS[k]:#x} (G3 LIVE) or {TWIN_ICONS[k]:#x}, row widget = string {FX_LABEL:#x} "FX"')
        add(at, ROW_STATE, sym, 'A', 'C',
            f'frame-size page forced hide of row {rid:#x} (index {idx}): bl 0x403C2F78 -> bl {sym} (LIVE N-RAW: {what}, state 0)')
    return tuple(out)


def _derived(label):
    return ((Derived(0x401F2000, 0x401F4000, 'z63patch.open_gate_icons:ent_marked_icons',
                     'ENT variant mark: the twins\' icons with a "3:2" mark for ids 0x7019..0x701E (G3 readers)'),)
            if label == 'mark' else ())


if ROW_LABEL not in VARIANTS_ROW_LABEL:
    raise ValueError(f'ROW_LABEL {ROW_LABEL!r}')
# The default build (GROUP) takes ROW_LABEL; the other value is the variant open_gate_<label> with
# its own stage (open_gate.py VARIANTS / STAGES, R-ICONS-2).
VARIANT_LABEL = [x for x in VARIANTS_ROW_LABEL if x != ROW_LABEL][0]
RECORDS, DERIVED = _records(ROW_LABEL), _derived(ROW_LABEL)
VARIANT_RECORDS, VARIANT_DERIVED = _records(VARIANT_LABEL), _derived(VARIANT_LABEL)
ASSETS = (Asset(ASSET[0], ASSET[1], 'ENT: the six [FX 3:2] row labels'),)
