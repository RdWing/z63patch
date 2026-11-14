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
"""G10, open gate: full-height FX N-RAW at 6K
(6060 x 4040 coded) and 4K (4040 x 2692 coded), 30/25/24p. One group (id 10), one translation
unit (native/arm_time/groups/open_gate.c), one state block (64 B at 0x8D0), one phase, one
integrity bit, one restore callback, one sanitise list. Built in parts that add files only.

Interface for the parts (stable from 10a; details in native/arm_time/groups/open_gate.h):
  part  module (this package)    C source (native/arm_time/groups/)   owns
  10a   open_gate.py, *_geom.py   open_gate.h, open_gate.c, *_geom.c    state, decision, og_sync,
                                                                       geometry records, budget and
                                                                       rate-row hooks, sanitise,
                                                                       restore z63r_g10
  ENT   open_gate_entries.py      open_gate_entries.c                   frame-size page [FX 3:2]
                                                                       entries
  10b   open_gate_rec.py          open_gate_rec.c                       container 2.5, NCTG/proxy 2.6,
                                                                       crop tag, proxy SPS, writers
  10c   open_gate_af.py           open_gate_af.c (.S)                   AF alias 2.8, AF area, zoom,
                                                                       lit points, display
  10d   open_gate_play.py         open_gate_play.c                      playback 2.11
  10e   (requests only)                                                site registrations, G7 peer
A part module may define RECORDS, SHARED, STOCK_CALLS, ENTRIES, SANITISE, ASSETS, DERIVED,
TEXT_SLOTS, SAVES (True if it calls z63s_commit). This module concatenates whichever exist.

Switches (change here only):
  SELECT_BY: 'frame_size_page' (the stock page's [FX 3:2] entries, part ENT).
    'quality_row' is the alternative (an On/Off row on G2's quality sub-page; G2 change, see
    G10-REQUESTS R-G10-5); with it, part ENT adds no records. The decision (bit 20 of the
    published word, og_size_word) is the same either way.
  PASS_BUDGET_CAP: the budget cap may act in pass-through. The cap is og_budget_cap()
    (PASS-safe by construction, verify_gate._pass_cap). True: it is the
    group's pass_cap, so the core's nraw_budget adapter calls it after the decide hooks in every
    phase, also with no page (G10-REQUESTS R-G10-2). False would leave it inside
    z63h_nraw_budget_g10 only (LIVE/DRAINING).
  REPLAN (re-plan build; supersedes IMMEDIATE_REPLAN below): requirement "choosing or
    leaving open gate on the frame-size page takes effect when the user exits the menu in movie view".
    One rule: every G10 reader decides from the PLAN LATCH (og_plan_kind: state +1, written with
    +0/+4/+6, read only while the movie word is 1), never from the choice. The latch follows the
    stored choice (bit 20 in a v2/v3 record with a good CRC, file type 6, size 6 or 4, rate 6..8):
      raised/switched/lowered in settled photo view by the publication callback z63p_g10 (card-
        detect poll, every count 0, Q checked by the callback), and in movie view by g10_replan at
        stock's applier call 0x40F681FC (LIVE, G2 LIVE, not recording, no ACT[2] or TX);
      lowered also by the plan post-fix when stock's chosen movie record is not one the latch
        applies to (or the record or a table word is foreign);
      cleared by the break and restore callbacks.
    Tables follow the latch at the first movie plan or g10_replan (tables first), and go back to
    stock at a photo plan. The movie-entry word (R-G10-0) no longer decides anything in G10
    geometry. movie_publish stays False.
  TWIN_COMPARE: option C (the trace's fallback), False by default. True would add g10_twin_cmp
    (second compare call 0x40F67FC4) to GROUP; with False it is built only in the variant
    open_gate_twin (stages s7-opengate-twin), for the camera emulator to decide.
  IMMEDIATE_REPLAN: (historical; superseded by REPLAN above). With movie_publish=False (README 6) the published word changes only under
    Q, so an [FX 3:2] choice made in movie view takes effect at the next movie-view entry from
    settled photo live view. The stock re-plan that runs at menu exit (old layout)
    would apply it at once only if the word were published in movie view. Hook points for a raised
    re-plan (not used): the re-plan compare epilogue 0x40F6EF2C and the applier call 0x40F681FC
   . False: no record.
    Evaluated (request: move the entry word at a successful [FX 3:2] commit over
    idle movie view so the menu-close re-plan reads it). Not enabled, three gaps:
    (1) the plan latch does not gate the live readers: the size-function value sites, leaves,
        classifier, AF and display adapters decide on og_size(), which reads the entry word; the
        first such call after the commit moves the og_sync sensor tables to open-gate rows while
        the plan record still holds the stock plan's rows (replay MenuClosePublication in
        tests/test_arm_time_s7_opengate_a.py). Whether stock calls them while the page is open is
        not established; the old layout's page-path arming froze live view.
    (2) the menu-close re-plan for a twin switch (same native size and rate) is seen only in the
        emulator; its static trigger is not traced.
    (3) the entry word is written only by publish() in the card-detect task inside the busy window
        (PUBBUSY by plain release stores, one writer); the commit runs inside the menu task's own
        TX0, so the 'every TX and ACT count 0' fence cannot hold there, and a second writer would
        need a CAS busy protocol in the core (a rule change)."""
import dataclasses
import importlib

from . import Group, Shared, SAVE_TERM

SELECT_BY = 'frame_size_page'
PASS_BUDGET_CAP = True             # The core calls og_budget_cap in every phase (R-G10-2)
IMMEDIATE_REPLAN = False           # historical: the old design; see REPLAN
REPLAN = True                      # g10_replan at 0x40F681FC (always built)
TWIN_COMPARE = False               # option C: only in the variant open_gate_twin

PARTS = ('open_gate_geom', 'open_gate_entries', 'open_gate_imenu', 'open_gate_rec', 'open_gate_af', 'open_gate_play')
# One stage for the group with every part present (each part's tests filter on its own sets), and
# the same with G7 (desqueeze) for the composition replays.
STAGES = {'s7-opengate': ('marker', 'menu', 'open_gate'),
          's7-opengate-dq': ('marker', 'menu', 'desqueeze', 'open_gate')}
if not TWIN_COMPARE:
    STAGES['s7-opengate-twin'] = ('marker', 'menu', 'open_gate_twin')
# The single deliverable recipe: the all-groups image, marker + G2..G10,
# default open-gate variant (not twin, not mark), composed as the
# all-groups integration composition. docs/using-the-patcher.md says how it is packaged.
STAGES['s7-all'] = ('marker', 'menu', 'indicators', 'prores', 'h265', 'nraw', 'desqueeze', 'log', 'viewassist', 'open_gate')

STOCK_CALLS_10A = {}
# og_sync tables (old native/open_gate.c og_sync): put back to stock at every restore pass (restoration sanitiser).
# The 6K plan record 0xA0298ECA0 and both frontend blocks are rewritten by every stock plan (the
# photo plan writes 4040 rows and block 0xA02BC8070 there itself), so they are not sanitised.
_SAN = ((0xA02BC7944, 3432, 4040, 'RAW stream source rows, area 3'),
        (0xA02BC7970, 3432, 4040, 'readout rows, area 3'),
        (0xA0298EEBE, 3432, 4038, '4K reduction record input rows (not written by stock code)'),
        (0xA0298F05A, 3432, 4038, '4K vertical block input rows (not written by stock code)'),
        (0xA0298F0B0, 2288, 2692, '4K vertical block output rows (not written by stock code)'),
        (0xA02BC820E, 3432, 4040, 'size function rectangle rows'),
        (0xA02BC80F2, 3402, 4032, '6K active rows'),
        (0xA02BC80FA, 14, 4, '6K active top'),
        (0xA02BC8112, 2268, 2688, '4K active rows'),
        (0xA02BC811A, 10, 2, '4K active top'))
# Not declared as core Sanitise entries (10 x 40 B of core table would leave the composed core with
# every group at 1,008 B free, below the 1,024 B margin): z63r_g10 puts them back itself, under
# the same rule (Q, the group's terms, known value -> stock by one bounded CAS, anything else left
# and reported). SANITISE_TABLES keeps the list for the tests and the parts.
SANITISE_TABLES = _SAN
SANITISE_10A = ()



def _parts():
    out = []
    for name in PARTS:
        try:
            out.append(importlib.import_module(f'{__package__}.{name}'))
        except ModuleNotFoundError as e:
            if e.name != f'{__package__}.{name}':
                raise
    return out


def _gather():
    parts = _parts()
    cat = lambda attr: tuple(x for m in parts for x in getattr(m, attr, ()))
    calls = dict(STOCK_CALLS_10A)
    for m in parts:
        for k, v in getattr(m, 'STOCK_CALLS', {}).items():
            if calls.get(k, v) != v:
                raise ValueError(f'G10 part {m.__name__}: stock call {k} redeclared')
            calls[k] = v
    saves = any(getattr(m, 'SAVES', False) for m in parts)
    twin = tuple(x for m in parts for x in getattr(m, 'TWIN_RECORDS', ()))
    return dict(records=cat('RECORDS') + (twin if TWIN_COMPARE else ()), twin=() if TWIN_COMPARE else twin, shared=(Shared(site='nraw_budget'), Shared(site='nraw_copy')) + cat('SHARED'),
                stock_calls=calls, entries=cat('ENTRIES'), sanitise=SANITISE_10A + cat('SANITISE'),
                assets=cat('ASSETS'), derived=cat('DERIVED'), text_slots=cat('TEXT_SLOTS'), saves=saves)


_G = _gather()
_SAVE = {'stock_save_request': 0x41003BC0} if _G['saves'] else {}

GROUP = Group(
    id=10, name='open_gate', title='Open gate (full-height FX N-RAW 6K/4K)',
    source='native/arm_time/groups/open_gate.c',
    slot=(0x401DC000, 0x401EC000),
    records=_G['records'],
    shared=_G['shared'],
    requires=(2,),                 # the choice is saved through G2's save block (z63s_commit) and shown by G2/ENT
    stock_calls={**_G['stock_calls'], **_SAVE},
    save_calls=_SAVE,
    state=(0x8D0, 64),
    install_needs_q=True,          # Q-BL value sites (installed in photo live view)
    terms=(SAVE_TERM,) if _SAVE else (),
    entries=_G['entries'],
    assets=_G['assets'],
    derived=_G['derived'],
    sanitise=_G['sanitise'],
    text_slots=_G['text_slots'],
    hook_stores=True,              # budget hook: request +0xB8 and own state; copy hook: the 24-byte row
    restore=True,                  # z63r_g10: the global N-RAW rate row from an open-gate row back to stock
    publish=True,                  # z63p_g10: the photo-view pre-set of the plan latch (settled photo view only; always pending)
    restore_now=True,              # z63n_g10 (R-G10-1): at a break, tables and global row to stock at once (or at record stop)
    pass_cap='og_budget_cap' if PASS_BUDGET_CAP else '',   # the budget cap in every phase
    replay_tests=('tests/test_arm_time_s7_opengate_a.py',),
    movie_publish=False)           # README 6: geometry is read at live-view re-plan

# Option C as its own variant (same id, slot, source and bytes; one record more), for the emulator.
VARIANTS = (dataclasses.replace(GROUP, name='open_gate_twin', title='Open gate with the twin compare post-fix (option C)',
                                records=GROUP.records + _G['twin']),) if _G['twin'] else ()

# Part ENT's row label (open_gate_entries.ROW_LABEL; R-ICONS-2): GROUP uses ROW_LABEL ('slot'),
# the other value is its own variant and stage, for the camera emulator. Same id, slot, source and
# code bytes; only the six show records' adapter and ENT's derived asset differ. The 'mark' variant's
# marked icons need G3's 31-id readers, so its stage names G3's variant indicators_mark.
def _ent_variant():
    from . import open_gate_entries as ENT
    if not any(r.set == 'ent' for r in GROUP.records):
        return ()
    swap = {r.address: r for r in ENT.VARIANT_RECORDS}
    records = tuple(swap[r.address] if r.set == 'ent' and r.address in swap else r for r in GROUP.records)
    derived = tuple(d for d in GROUP.derived if d not in ENT.DERIVED) + ENT.VARIANT_DERIVED
    return (dataclasses.replace(GROUP, name=f'open_gate_{ENT.VARIANT_LABEL}', title=f'Open gate, ENT row label {ENT.VARIANT_LABEL!r}',
                                records=records, derived=derived),)


_ENT_VARIANT = _ent_variant()
VARIANTS += _ENT_VARIANT
for _v in _ENT_VARIANT:
    STAGES[f"s7-opengate-{_v.name[len('open_gate_'):]}"] = ('marker', 'menu', 'indicators_mark' if _v.name == 'open_gate_mark' else 'indicators', _v.name)
