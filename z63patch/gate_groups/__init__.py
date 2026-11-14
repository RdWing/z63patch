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
"""Feature-group declarations for the composed arm-time gate (stages S7 on).

One module per group in this package (``z63patch/gate_groups/<name>.py``, defining ``GROUP``)
and one module per shared site in ``z63patch/gate_groups/sites/<name>.py`` (defining ``SITE``).
The builder (z63patch/gate.py) composes any subset named by a recipe; the independent verifier
(z63patch/verify_gate.py) checks every record of a candidate against these declarations and
the stock image. How to add a group: README.md next to this file.

Nothing here touches an image; the classes only describe what a group installs.
"""
from dataclasses import dataclass, field
import importlib
import pkgutil
import struct

# Address plan. The gate core links at CORE_LO and
# must end below CORE_END; groups link in fixed slots inside POOL.
CORE_LO, CORE_END = 0x401A0000, 0x401A5000
# The core's second section (standby wake): card-detect code that only arming
# uses (cold and warm), section .text.z63card of native/arm_time/gate.c. The old layout's journal
# veneers sat at 0x401A5000 (excluded); no group slot, asset, derived asset or clone may use it
# (all lie inside POOL), and the stock bytes there are replaced like the rest of the patch range.
CARD = (0x401A5000, 0x401A6000)
CARD_SECTION = '.text.z63card'
POOL = (0x401A6000, 0x401F6000)       # assets 0x401A6000, feature code 0x401B6000, open-gate tables 0x401F0000
TEST_SLOT = (0x401EE000, 0x401F0000)  # test-only groups; never in a recipe
TABLE, TABLE_END = 0x401F6000, 0x401FFC00
MAX_RECORDS = 800
# Fixed state-page map whatever the composition.
FEATURE_STATE = (0x840, 0x9B0)        # 368 bytes, allocated per group at fixed offsets
GROUP_BITS = 16                       # gate word bits 16-31: broken groups; arm-file mask bits 0-15
INFRASTRUCTURE, MARKER = 0, 1
# Migration-plan numbering: ids 2-10.
PLAN_IDS = {2: 'shared settings and menu', 3: 'indicators', 4: 'ProRes', 5: 'H.265', 6: 'N-RAW',
            7: 'desqueeze', 8: 'log recording', 9: 'view assist and playback', 10: 'open gate',
            11: 'RAW pre-capture'}
# Reserved per group, so that groups written in parallel never collide (core-owned; a
# group author never edits these tables). A group's slot lies inside its reserved range and its
# feature state inside its reserved bytes. Ids 12-15 are for test-only groups (TEST_SLOT, any
# free state bytes). Id 11 (RAW pre-capture) takes its code slot from the top of the open-gate
# reservation (G10's code ends near 0x401E6790) and its state from the top of the core reserve.
# Changing a reservation is a core change, never part of a group.
PLAN_SLOTS = {MARKER: (0x401B6000, 0x401B7000), 2: (0x401B7000, 0x401BC000), 3: (0x401BC000, 0x401BF000),
              4: (0x401BF000, 0x401C5000), 5: (0x401C5000, 0x401C7000), 6: (0x401C7000, 0x401CB000),
              7: (0x401CB000, 0x401D0000), 8: (0x401D0000, 0x401D8000), 9: (0x401D8000, 0x401DC000),
              10: (0x401DC000, 0x401EC000), 11: (0x401EC000, 0x401EE000)}
# Feature-state registry: fixed offsets inside the 368 bytes at +0x840.
# G2 holds its save block (save_api.h, Z63_STATE_G2 + 0x30..0x44) and its menu state; the
# video-menu rows stay inside 0x000..0x840 (48 rows). G3 and G5 hold none; G6 and G8 hold
# only their anchor value word (the top 16 bytes of the former reserve); G11 holds its frame
# table in the top 128 bytes of the former reserve; 0x910..0x920 is the core reserve (test-only
# groups use it). A group reads another group's state only through z63g_peer_state().
PLAN_STATE = {2: (0x840, 112), 4: (0x8B0, 8), 6: (0x9A0, 8), 7: (0x8B8, 16), 8: (0x9A8, 8), 9: (0x8C8, 8), 10: (0x8D0, 64),
              11: (0x920, 128)}
STATE_RESERVE = (0x910, 0x920)
# Asset ranges (read-only data placed outside the code slot, sections ".z63asset.<lo hex>"): the
# old-layout addresses, so the existing asset builders port directly.
# G8's range holds its log tables (0x401B4000) and, from 0x401B5500, the four Log live-view PNGs that
# G3's extension table rows 0x7015..0x7018 point at (GATE_LOG_ICONS). G9's is
# the last 4 KiB of its reserved code slot (its curve tables decode as branches in a code slot).
PLAN_ASSETS = {2: ((0x401A6000, 0x401AA000), (0x401B0000, 0x401B4000)), 3: ((0x401AC000, 0x401B0000),),
               8: ((0x401B4000, 0x401B6000),), 9: ((0x401DB000, 0x401DC000),),
               10: ((0x401F0000, 0x401F6000),)}   # 0x401AA000..0x401AC000 reserve
GATE_LOG_ICONS = 0x401B5500                         # G8 derived asset: S-Log3 and Log3G10 fill/outline PNGs
# Text-slot registry (the stock TEXT_STORE pool has 300 slots, 0..299). A scan of the resolved
# slot constants of the 451 resolved TEXT_STORE 0x404066C8 callers found 189 as the highest and
# none at 240 or above (585 callers unresolved, so stock use above 239 is not established).
# Ranges inclusive. G2 draws the quality, desqueeze, log and reset rows for G4, G5, G7 and G8.
TEXT_SLOTS = {MARKER: (285, 285), 2: (252, 284), 10: (286, 291), 8: (292, 295)}   # 240..251, 296..299 reserve
# Ranks inside the video-menu set (set id 1). Rank 1 is P (core), 4 the video_refresh site; the
# others belong to G2 (events 2, count 3, entry 5, sub-page 6). 7 and above: core.
VIDEO_MENU_RANKS = {1: 0, 2: 2, 3: 2, 4: 'site video_refresh', 5: 2, 6: 2}
VIDEO_MENU_ROWS_MAX = 48              # 45 stock rows plus 3, all inside page 0x000..0x840
# Approved clone ranges (the two log producers only), with
# the SHA-256 of the stock bytes. Any other range is refused.
CLONE_APPROVED = {(0x4111CD60, 0x4111D480): 'e1cda0f085445bd4d1a66b8f0ea6afe1ebe459103d7d6f0599a846e5e7a56142',
                  (0x4111D618, 0x4111DF4C): '380ad0ac564ad841d4aa1e7a7c0f88a328734a978d10669491023433180ced51'}
WRAPPER_CLASSES = {'A': 1, 'C': 2, 'F': 3, 'S': 4, 'X': 5}
VIDEO_MENU_SET = 'video_menu'
# The patch never programs or erases flash and never touches anything outside MAIN. Stock
# flash-writing ranges a declaration may never name as a stock call. The verifier keeps its own
# copy, derives the veneers from the image and refuses any stock call that can reach these.
FLASH_ROUTINES = ((0xA02BDE000, 0xA02BE1000, 'flash-writing range'),
                  (0x455CB000, 0x455CE000, 'flash-writing range'),
                  (0x4090EB70, 0x4090EBE8, 'flash-writing range'),
                  (0x416690B8, 0x416692C8, 'flash-writing range'),
                  (0x40CA8220, 0x40CA8BDC, 'flash-writing range'),
                  (0x40D72E14, 0x40D72F6C, 'flash-writing range'),
                  (0x40D759B4, 0x40D76F08, 'flash-writing range'),
                  (0x40C07EB8, 0x40C080B4, 'flash-writing range'),
                  (0x410DEE08, 0x410DEE38, 'flash-writing range'),
                  (0x41013000, 0x4101E300, 'flash-writing range'))
# Stock entry points through which a group may ask stock firmware to persist settings, exactly as
# stock would issue the request. A group lists the one it calls in save_calls (and stock_calls),
# requires G2 (the save block lives in G2's state), and carries SAVE_TERM as an install term.
SAVE_ENTRIES = {0x41003BC0: 'stock settings-save request (str x30 prologue at 0x41003BC0); stock reaches it '
                            'only through the service-table word 0xA02958838 = 0x41004D34 (tbz w0,#0 -> ret, else '
                            'b 0x41003BC0); sets busy byte 0xA03D9C278, posts event bit 12 to the settings worker '
                            '0x4100397C and waits for it (synchronous, no timeout in stock). Static reach clean: '
                            'the flash write happens in the worker (0x41003538, which the observer record at '
                            '0x41003A7C replaces and never calls on its own account).'}
SAVE_BUSY = 0xA03D9C278


@dataclass(frozen=True)
class Code:
    """One aligned instruction word in the copied text: ``old`` (the stock word) becomes a B
    (``link=False``) or BL (``link=True``) to ``symbol`` in the group's own code.

    ``tx``: TX counters (bit c = TX c) that pin this record when stuck (spec 1.8): the record is
    not restored while a transaction of that class is open. ``lookup=False``: an S-class wrapper
    that never reads the gate (a pure map that passes everything else to the replaced target by a
    tail ``b``); only wrapper 'S'. ``continuation=True``: ``old`` is a non-branch word the adapter
    displaces; the adapter may leave by ``<old>; b address+4`` (exactly those two words) and the
    record restores only under Q."""
    address: int
    old: int
    symbol: str
    link: bool
    cls: str                  # record class letter (design classes; 'A'-'Z')
    wrapper: str              # wrapper class: 'A', 'C', 'F', 'S' or 'X'
    purpose: str
    set: str = ''             # coupling set, local to the group ('' = none), or VIDEO_MENU_SET (G2)
    rank: int = 0             # rank inside the set (1 first installed, last restored)
    needs_q: bool = True      # restore only under Q (and the group's terms)
    all_or_none: bool = False # member of an all-or-none set
    stores: bool = False      # the adapter may store outside its frame (declared, verified)
    tx: int = 0               # TX pin mask (record byte 10)
    lookup: bool = True       # the adapter's first access is the acquire load of P
    continuation: bool = False
    kind = 'code32'


@dataclass(frozen=True)
class Data:
    """One aligned 64-bit data word (``old`` = the stock value) that becomes the address of
    ``symbol`` in the group's own code. ``target='code'``: the symbol is an adapter, walked with
    the wrapper rules. ``target='data'``: the symbol is a read-only data array (in the group's
    code section or one of its assets), never walked as code; its callbacks are declared as
    ``Entry``."""
    address: int
    old: int
    symbol: str
    cls: str
    wrapper: str
    purpose: str
    set: str = ''
    rank: int = 0
    needs_q: bool = True
    all_or_none: bool = False
    stores: bool = False
    target: str = 'code'
    lookup: bool = True
    tx: int = 0
    kind = 'data64'


@dataclass(frozen=True)
class Value16:
    """One aligned 16-bit data word that takes a fixed value (``old`` stock, ``new`` patch),
    installed and restored by a bounded exclusive CAS. No code is reached through it."""
    address: int
    old: int
    new: int
    cls: str
    purpose: str
    set: str = ''
    rank: int = 0
    needs_q: bool = True
    all_or_none: bool = False
    wrapper: str = 'X'
    tx: int = 0
    stores = False
    symbol = None
    kind = 'data16'


def call_sites(addresses, callee, symbol, stock, **fields):
    """Code records for several BL call sites of one stock callee (one group): the old word of
    each is the BL to ``callee`` read from ``stock`` (an Image or bytes at 0x40160000) and
    checked. Use a Site instead when several groups need the same call sites."""
    out = []
    for at in addresses:
        word = stock.words(at, 1)[0] if hasattr(stock, 'words') else int.from_bytes(stock[at - 0x40160000:at - 0x40160000 + 4], 'little')
        _need(word >> 26 == 0x25 and at + 4 * ((word & 0x3FFFFFF) - ((word & 0x2000000) << 1)) == callee,
              f'{at:#x} is not a BL to {callee:#x}')
        out.append(Code(address=at, old=word, symbol=symbol, link=True, **fields))
    return tuple(out)


def bl_word(at, callee):
    return 0x94000000 | (((callee - at) >> 2) & 0x3FFFFFF)


def b_word(at, target):
    return 0x14000000 | (((target - at) >> 2) & 0x3FFFFFF)


@dataclass(frozen=True)
class Entry:
    """A wrapper entry reached only through the group's own data (an event-table callback, a
    pick handler): walked with the wrapper rules like a record adapter."""
    symbol: str
    wrapper: str = 'C'
    stores: bool = False
    lookup: bool = True


@dataclass(frozen=True)
class Asset:
    """Read-only data of the group placed at ``lo`` (section ``.z63asset.<lo hex>`` in the group's
    translation unit, at most hi - lo bytes), inside the asset ranges reserved for the group."""
    lo: int
    hi: int
    purpose: str


@dataclass(frozen=True)
class Derived:
    """Read-only data made at build time from the user's own MAIN (never stored in the repository
    or a recipe; the recipe keeps only its address, size, SHA-256 and the builder's name).
    ``builder`` is ``'module:function'``, a function ``f(stock: bytes) -> bytes`` that must be a pure
    function of the stock C2.01 image (and of files in the repository). Placed at ``lo`` (16-aligned,
    at most hi - lo bytes) inside the group's asset ranges, disjoint from its other assets. The
    group's code names it by the extern ``z63_derived_<lo hex>`` (= lo). The verifier recomputes it
    with the same builder and compares byte for byte, and scans it like any asset."""
    lo: int
    hi: int
    builder: str
    purpose: str


def derived_symbol(lo):
    return f'z63_derived_{lo:x}'


def call_builder(name, stock):
    """Run a 'module:function' builder (Derived.builder, Group.stock_header) on the stock bytes."""
    mod, _, fn = name.partition(':')
    _need(mod.startswith('z63patch.') and fn.isidentifier(), f'builder {name!r}')
    out = getattr(importlib.import_module(mod), fn)(stock)
    _need(isinstance(out, (bytes, bytearray, str)), f'builder {name} returned {type(out).__name__}')
    return out


def restore_symbol(gid):
    return f'z63r_g{gid}'


def publish_symbol(gid):
    return f'z63p_g{gid}'


def break_symbol(gid):
    return f'z63n_g{gid}'


PASS_CAPS = {10: 'og_budget_cap'}     # R-G10-2: the only pass cap a feature group may declare


@dataclass(frozen=True)
class Sanitise:
    """A run-time word the group's tables can leave behind (restoration sanitiser): during restoration, under
    Q and the group's terms, a value in [match_lo, match_hi) is put back to ``stock`` by one
    bounded CAS. ``when``: indexes into the group's sanitise list; the entry applies only when one
    of them matches in the same pass. Width 2, 4 or 8."""
    address: int
    width: int
    stock: int
    match_lo: int
    match_hi: int
    purpose: str
    when: tuple = ()


@dataclass(frozen=True)
class Patch:
    """One word of a clone that is not the stock word: at ``offset`` from the clone start, a B
    (``link=False``) or BL to ``symbol`` in the group's code. A frameless leaf takes a B and the
    group returns with ``b`` to the extern ``z63_clone_<name>_<offset+4 hex>``."""
    offset: int
    symbol: str
    link: bool


@dataclass(frozen=True)
class Clone:
    """A copy of an owner-approved stock range [lo, hi), made at build time from the user's own
    MAIN into the group's slot at ``at`` (never stored in the repository or a recipe). PC-relative
    branches are re-encoded to the same absolute targets; ``patches`` are the only other words
    that differ; ``literals`` lists offsets of literal loads the range may contain (none for the
    approved ranges). The group calls it through the extern ``z63_clone_<name>``."""
    name: str
    lo: int
    hi: int
    at: int
    patches: tuple = ()
    literals: tuple = ()


@dataclass(frozen=True)
class Shared:
    """A contribution to a shared site (sites/<site>.py): ``hook`` in the group's code is called
    by the site's merged adapter in the gate core. The hook name is fixed by the site:
    ``z63h_<site>_g<id>`` with the signature in native/arm_time/sites/<site>.h."""
    site: str
    hook: str = ''
    role: str = ''                      # test-only groups only: the site role to join (after the ordered members)


def hook_name(site, gid):
    return f'z63h_{site}_g{gid}'


@dataclass(frozen=True)
class Term:
    """A quiescence term beyond Q: (u8 or u32 at address) & mask must equal value (equal=True)
    or differ from it (equal=False). Checked at the arming poll for a group with
    ``install_needs_q`` and before every restore-under-Q record of the group."""
    address: int
    width: int
    mask: int
    value: int
    equal: bool
    purpose: str


SAVE_TERM = Term(address=SAVE_BUSY, width=1, mask=0xFF, value=0, equal=True,
                 purpose='no stock settings-save request waiting (busy byte of 0x41003BC0)')

# ---- arming in movie view ----
# QM, "settled idle movie view": pipeline state 0xA03D42100 == 2 and its class 0xA03D42108 one of
# QM_CLASSES, no capture or movie work, no update activity or update, the card terms, the GUI
# terms below, the record button not held. The classes accepted, each with where it was observed;
# any other class fails closed (arming waits for settled photo live view). Add a class only with
# its observation. The movie-recording readiness word 0xA048BB160 is NOT a term: it is 0 in idle
# movie view with no card, a full or failed card, or N-RAW / ProRes RAW on a card that is not exFAT
# (analysis z63-movie-word-20261112).
QM_CLASSES = {7: 'H.265 (file type 2), idle movie view: 2/1/7; 3/1/7 with the main menu open, 0/1/0 on a sub-page '
                 'and in playback (emulator, two repetitions); 2/1/7 also with no card '
                 '(readiness 0)',
              0x0A: 'N-RAW (file type 6), idle movie view: 2/1/10 (emulator; '
                    'readiness 0 on the FAT32 card); 0x410538FC returns 0x0A for file '
                    'types 5 and 6 on the movie path (0x410539C4..D4)'}
# The GUI terms (analysis z63-movie-word-20261112; static trace in SIGNALS-PROGRESS). The app
# controller object GUI_OBJECT: current app id at +0x6C8 (9 shooting screen, 0xD i-menu, 5 main
# menu, 6 playback; [E] stock-menusig-c, one boot), the i-menu hold bit 2 at +0x680, the self
# pointer at +0x660 (sanity: GUI_OBJECT once stock has initialised the object).
#  IMENU_CLOSED: the i-menu is closed: app id != 0xD and hold bit 2 clear. Every arming view (photo
#   and QM; cold, warm and stage 2: an open i-menu counts as a menu open, for G10 part IM's i-menu
#   table records) and the arming window's "delayed" rule. G3's install (and restore) term is the
#   hold bit alone (one term: core budget): stock sets it whenever the posted app id is 0xD or 0x11
#   (0x40D5D41C..0x40D5D448) and clears it for any other id (0x40D5B35C..0x40D5B374); G3 installs
#   only under view(), which tests both.
#  SHOOTING_SCREEN: stricter, arming views only: app id == 9.
# Either switch makes the arming views require the sane self pointer (else no arming).
GUI_OBJECT = 0xA0282E8C8
GUI_SELF, GUI_HOLD, GUI_APP = GUI_OBJECT + 0x660, GUI_OBJECT + 0x680, GUI_OBJECT + 0x6C8
GUI_SHOOTING, GUI_IMENU = 9, 0xD
IMENU_CLOSED = True
SHOOTING_SCREEN = True
IMENU_TERMS = (Term(address=GUI_HOLD, width=4, mask=4, value=0, equal=True,
                    purpose='i-menu closed: hold bit 2 of 0xA0282EF48 clear (gate_groups.IMENU_CLOSED)'),)
# Stock words that pin the GUI object and its fields (checked against the image by the builder,
# and again by the verifier from its own copy): getter 0x40D59224 (x0 = GUI_OBJECT; ret), the
# self-pointer store, the handler registration, the app-id store, the hold-bit set and clear.
# The words themselves are not stored. GUI_STOCK_SITES lists them (each 4 bytes, in this
# order) and GUI_STOCK_DIGEST is SHA-256(GUI_STOCK_TAG, then per site: address u64, 4 u32, word).
GUI_STOCK_SITES = (0x40D59224, 0x40D59228, 0x40D5922C, 0x40D59230, 0x40D59234, 0x40D5A694, 0x40D5A628, 0x40D5A630,
                   0x40D5A950, 0x40D5D444, 0x40D5D448, 0x40D5B370, 0x40D5B374)
GUI_STOCK_TAG = b'z63-gui-stock-v1\0'
GUI_STOCK_DIGEST = '0d05e11dff781987f65c7d562ad8bc346c8f82ead7282808d45faad672129379'
# The record button: bit 0x2000 (SW1) of the switch snapshot word 0xA0336BDB0 (single-bit test).
# QM requires it clear over the whole settle and at the last
# check (a ProRes/N-RAW record start racing G4's type front end).
SWITCHES, RECORD_BUTTON = 0xA0336BDB0, 0x2000


def imenu_terms():
    """G3's install terms: the i-menu hold-bit term while IMENU_CLOSED is on."""
    return IMENU_TERMS if IMENU_CLOSED else ()


def gui_stock_digest(read_word):
    """GUI_STOCK_DIGEST recomputed from an image (read_word(va) -> u32)."""
    import hashlib
    h = hashlib.sha256(GUI_STOCK_TAG)
    for va in GUI_STOCK_SITES:
        h.update(struct.pack('<QII', va, 4, read_word(va)))
    return h.hexdigest()


def check_gui_stock(read_word):
    """Raise ValueError unless the GUI stock words read as reviewed (one digest over all of them;
    the exact stock MAIN is required first, so a mismatch means a changed declaration)."""
    if gui_stock_digest(read_word) != GUI_STOCK_DIGEST:
        raise ValueError(f'stock GUI word changed (digest over {len(GUI_STOCK_SITES)} sites from {GUI_STOCK_SITES[0]:#x})')


def qm_class_mask():
    return sum(1 << c for c in QM_CLASSES)


@dataclass(frozen=True)
class QmEvidence:
    """Named evidence that the stock word of one of the group's non-branch code records is not
    executed in settled idle movie view (an exec-watch run, say), so that replacing it there is
    as safe as a branch swap. ``address`` is the record's word; ``evidence`` names the run or
    study and what it showed."""
    address: int
    evidence: str


def _is_branch(word):
    return word >> 26 in (0x05, 0x25)


def unproven_words(g):
    """The addresses of g's code records that replace a non-branch word without QmEvidence."""
    proven = {e.address for e in g.qm_evidence}
    return tuple(r.address for r in g.records if r.kind == 'code32' and not _is_branch(r.old) and r.address not in proven)


def qm_ok(g):
    """True when g may be installed at an arming in QM (it is otherwise deferred to stage 2):
    qm_install not False and no non-branch code record without evidence. A group's records
    changing (a non-branch word re-sited as a call-site branch) changes this by itself."""
    return g.qm_install is not False and not unproven_words(g)


def no_defer(g):
    """Groups that are never deferred to stage 2 (installed at the arming or dropped): the
    video-menu rows (copied into the page only at its preparation) and the video-menu set (its
    rank order is the memory-safety argument of the install)."""
    return bool(g.rows) or any(r.set == VIDEO_MENU_SET for r in g.records)


@dataclass(frozen=True)
class Group:
    id: int
    name: str
    title: str
    source: str                         # one C translation unit, relative to the repository
    slot: tuple                         # (lo, hi): the fixed link range of this group's code
    records: tuple = ()                 # Code / Data / Value16
    shared: tuple = ()                  # Shared
    requires: tuple = ()                # group ids (0 is implicit)
    stock_calls: dict = field(default_factory=dict)   # extern name -> stock address (direct BL)
    defines: tuple = ()                 # extra -D for the compiler
    state: tuple = ()                   # (offset, bytes) inside FEATURE_STATE, or () for none
    install_needs_q: bool = False       # the group is installed only when Q and its terms hold
    terms: tuple = ()                   # Term
    replay_tests: tuple = ()            # test modules that replay this group
    test_only: bool = False
    hook_stores: bool = False           # its shared-site hooks may store outside their frame
    save_calls: dict = field(default_factory=dict)    # extern name -> SAVE_ENTRIES address (persistence requests)
    entries: tuple = ()                 # Entry: wrappers reached through the group's own data
    rows: tuple = ()                    # (template symbol, row count): the video-menu rows copied at arming (G2)
    assets: tuple = ()                  # Asset
    sanitise: tuple = ()                # Sanitise
    text_slots: tuple = ()              # TEXT_STORE slots the group writes (inside TEXT_SLOTS[id])
    clones: tuple = ()                  # Clone
    # True when the published settings word may change while this group is selected outside
    # settled photo live view (native idle, no recording, no admitted hold). False (default) holds
    # movie-view publication for the whole build while the group is selected (G10 open gate).
    movie_publish: bool = False
    derived: tuple = ()                 # Derived: read-only data made at build time from the user's MAIN
    # 'module:function' f(stock bytes) -> C header text, force-included (-include) when the group is
    # compiled (G2's stock-derived tables); written only to the build's temporary directory.
    stock_header: str = ''
    # A restore callback ``u32 z63r_g<id>(u64 page)`` (restore_symbol): run once per restore pass
    # on the card-detect task, before the records, only under Q and the group's terms, for a group
    # selected at arming (every composed group when the session page is lost; page is then 0).
    # Returns 0 nothing (left) to do, 1 pending (try again next pass), 2 left (a value it does not
    # know: left alone and reported, the session ends PARTIAL). Rules (verifier): no stock call, no
    # call into another group, bounded, stack <= 256 B; it may store (its run-time words only).
    restore: bool = False
    # Cross-group restore order: the ids of groups whose records must all be finished (restored,
    # already stock or never installed) before any record of this group is restored. Readers
    # declare the producers they must outlive ("readers last"). Each id must be larger than this
    # group's (the table is in id order and restored in reverse, so this only adds the waiting).
    restore_after: tuple = ()
    # A publication callback ``u32 z63p_g<id>(u64 page, u32 word)`` (publish_symbol): run by
    # the armed poll (card-detect task) right after a publication changed the published word, and
    # again on later polls while it reports pending, only while the publication conditions hold
    # (LIVE, the fence, no TX or ACT count, the anchor hold not held). It brings a stock cache that an
    # earlier read filled into line with `word`. Returns 0 done (or nothing it knows), 1 pending.
    # Rules as for the restore callback (no external call, bounded, stack <= 256 B; its stores are
    # CAS of its own run-time words).
    publish: bool = False
    # The offset of the group's anchor value word (an aligned u32 inside its own state), or 0
    # for a group with no anchor read. Its anchor reads (gate_api.h z63g_hold_anchor) take the
    # composition's hold word (page +0xC44; the core does not publish while it is held and releases
    # it) and record the value here for its followers (z63g_hold_last).
    hold: int = 0
    # R-G10-1: a break callback ``u32 z63n_g<id>(u64 page)`` (break_symbol): run by the armed poll
    # once this group is broken (integrity or raised) in LIVE or DRAINING, when nothing is
    # recording and no TX or ACT count is held; retried until it returns 0. Rules as for the
    # restore callback. It puts the group's run-time words back to stock now.
    restore_now: bool = False
    # R-G10-4: the group may raise its own integrity break (gate_api.h z63g_raise).
    raises: bool = False
    # R-G10-2: a function the nraw_budget site adapter calls in every phase, also with no
    # page (PASS_CAPS: only G10's og_budget_cap; a test-only group may declare one for the tests).
    pass_cap: str = ''
    # Movie-view arming: whether the group may be installed at an arming in QM (settled idle
    # movie view). None (default): by the rule, i.e. exactly when no code record replaces a
    # non-branch word without a QmEvidence entry (qm_ok()); False: never (deferred to stage 2,
    # installed in settled photo live view); True: asserted, refused by the checks unless the rule
    # holds. A group that may not install in QM, and every group that requires one, is deferred
    # at a QM arming: its records stay stock and its adapters pass until stage 2 installs it.
    qm_install: object = None
    qm_evidence: tuple = ()             # QmEvidence, one per non-branch code record shown idle in QM


@dataclass(frozen=True)
class Site:
    """A word, or several call sites of one callee, that more than one group may need: one record
    per word, one merged adapter in the core. ``addresses``: the call sites of ``callee`` (code32,
    each old word the BL to callee, checked against stock); otherwise the one ``address``/``old``.
    ``order``: (role, group id) pairs in call order, defined once here; the builder generates
    Z63_SITE_<NAME>_<ROLE>(X) with the composed contributors of each role in that order, and the
    hook signatures are fixed in native/arm_time/sites/<name>.h. Works with any subset."""
    name: str
    kind: str                           # 'data64' or 'code32'
    address: int
    old: int
    adapter: str                        # the merged adapter's symbol in the core
    source: str                         # native/arm_time/sites/<name>.c
    cls: str
    wrapper: str
    set: str                            # VIDEO_MENU_SET or '<site>.<set>'
    rank: int
    needs_q: bool
    stock_calls: dict                   # stock functions the adapter itself calls (core veneers)
    purpose: str
    addresses: tuple = ()
    callee: int = 0
    link: bool = True                   # code32: the new word is a BL (False: B, for a tail-branch site)
    order: tuple = ()                   # ((role, group id), ...)

    def words(self):
        """((address, old), ...) of every record of the site."""
        if self.addresses:
            return tuple((a, bl_word(a, self.callee)) for a in self.addresses)
        return ((self.address, self.old),)

    def roles(self):
        out = []
        for role, _ in self.order:
            if role not in out:
                out.append(role)
        return tuple(out)


def _modules(package):
    pkg = importlib.import_module(package)
    for info in sorted(pkgutil.iter_modules(pkg.__path__), key=lambda m: m.name):
        if info.ispkg or info.name.startswith('_'):
            continue
        yield importlib.import_module(f'{package}.{info.name}')


def discover():
    """{name: Group} of every group module in this package."""
    out = {}
    for m in _modules(__name__):
        g = getattr(m, 'GROUP', None)
        if isinstance(g, Group):
            if g.name in out:
                raise ValueError(f'group name {g.name} declared twice')
            out[g.name] = g
    return out


def variants():
    """{name: Group} of the declared variants: a module's optional ``VARIANTS = (Group, ...)``,
    each the same group (same id, slot and registries) under another name, with records added
    or dropped, for a stage that names it instead of the group. A variant is never checked
    together with its group: compose() puts it in the group's place. A variant whose group
    discover() does not return (a test's narrowed declarations) is left out with it."""
    found = discover()
    groups = {g.id: g for g in found.values()}
    out = {}
    for m in _modules(__name__):
        base = getattr(m, 'GROUP', None)
        for v in getattr(m, 'VARIANTS', ()):
            _need(isinstance(v, Group) and isinstance(base, Group) and v.id == base.id and v.name not in out and v.name != base.name,
                  f'{m.__name__}: variant {getattr(v, "name", v)!r}')
            if base.name not in found:
                continue
            _need(v.slot == groups[v.id].slot and v.source == groups[v.id].source, f'{v.name}: a variant keeps its group\'s slot and source')
            out[v.name] = v
    return out


def compose(names, declared):
    """`declared` ({name: Group}) with each variant named in `names` in the place of its group."""
    out = dict(declared)
    for n, v in variants().items():
        if n in names:
            base = [k for k, g in out.items() if g.id == v.id]
            _need(len(base) == 1 and base[0] not in names, f'variant {n} together with its group')
            del out[base[0]]
            out[n] = v
    return out


def compositions():
    """{stage: group names} of every reviewed composed stage, gathered from the optional
    ``STAGES = {'s7-<name>': (group names...)}`` of the group modules (so adding a stage edits
    no shared file). A stage name begins with 's7-' and is declared once."""
    out = {}
    for m in _modules(__name__):
        for stage, names in sorted(getattr(m, 'STAGES', {}).items()):
            _need(isinstance(stage, str) and stage.startswith('s7-') and stage not in out, f'{m.__name__}: stage {stage!r}')
            _need(isinstance(names, tuple) and names and all(isinstance(n, str) for n in names), f'{m.__name__}: stage {stage} groups')
            out[stage] = names
    return out


def shared_sites():
    """{name: Site} of every shared-site module."""
    out = {}
    for m in _modules(__name__ + '.sites'):
        s = getattr(m, 'SITE', None)
        if isinstance(s, Site):
            if s.name in out:
                raise ValueError(f'site {s.name} declared twice')
            out[s.name] = s
    return out


def _need(ok, why):
    if not ok:
        raise ValueError(why)


DATA_RANGE = (0xA02808000, 0xA05000000)
TEST_ASSETS = (0x401AA000, 0x401AC000)    # assets of test-only groups (the reserve between G2 and G3 assets)
TEST_TEXT_SLOTS = (296, 299)
_PCREL = ((0x9F000000, 0x10000000), (0x9F000000, 0x90000000), (0x3B000000, 0x18000000),   # adr, adrp, ldr literal
          (0x7C000000, 0x14000000), (0xFF000010, 0x54000000), (0x7E000000, 0x34000000), (0x7E000000, 0x36000000))


def pc_relative(word):
    """True for B/BL, b.cond, cbz/cbnz, tbz/tbnz, adr, adrp and literal loads."""
    return any(word & m == v for m, v in _PCREL)


def _size(r):
    return {'code32': 4, 'data64': 8, 'data16': 2}[r.kind]


def check(groups, site_map):
    """Rules every declaration must meet, across all the groups given (all discovered modules
    plus any test-only groups): unique ids and names, slots aligned, inside the pool and
    disjoint, state ranges disjoint inside the 368-byte feature state, one record per word
    across groups (a shared word is a site), dependencies on declared groups, set rules, and the
    reservations (slots, state, assets, text slots, video-menu ranks, clone ranges)."""
    ids = {}
    words = []

    def claim(lo, n, who):
        for a, m, other in words:
            _need(lo + n <= a or a + m <= lo, f'{who}: word {lo:#x} already declared by {other} (shared words are sites)')
        words.append((lo, n, who))
    for s in site_map.values():
        _need(s.kind in ('data64', 'code32') and s.cls.isupper() and len(s.cls) == 1, f'site {s.name}: kind or class')
        _need(s.wrapper in WRAPPER_CLASSES and bool(s.set) == (s.rank >= 1), f'site {s.name}: wrapper, set or rank')
        _need(not s.addresses or (s.kind == 'code32' and s.callee and s.link and len(set(s.addresses)) == len(s.addresses)),
              f'site {s.name}: several addresses need a code32 BL site of one callee')
        _need(s.set != VIDEO_MENU_SET or VIDEO_MENU_RANKS.get(s.rank) == f'site {s.name}', f'site {s.name}: video-menu rank {s.rank} is not reserved for it')
        _need(len(set(s.order)) == len(s.order) and all(isinstance(r, str) and r.isidentifier() and 1 <= gid < GROUP_BITS for r, gid in s.order),
              f'site {s.name}: order')
        for at, old in s.words():
            _need(at % (8 if s.kind == 'data64' else 4) == 0, f'site {s.name}: alignment at {at:#x}')
            claim(at, 8 if s.kind == 'data64' else 4, f'site {s.name}')
    slots = []
    states = []
    assets = []
    rows = []
    for g in groups:
        _need(1 <= g.id < GROUP_BITS, f'{g.name}: group id {g.id} outside 1..15')
        _need(g.id not in ids, f'{g.name}: group id {g.id} already used by {ids.get(g.id)}')
        ids[g.id] = g.name
        _need(g.id in PLAN_IDS or g.id == MARKER or g.test_only, f'{g.name}: id {g.id} is not a migration-plan id')
        lo, hi = g.slot
        _need(lo % 0x1000 == 0 and hi % 0x1000 == 0 and POOL[0] <= lo < hi <= POOL[1], f'{g.name}: slot {lo:#x}..{hi:#x}')
        _need(g.test_only == (TEST_SLOT[0] <= lo < TEST_SLOT[1]), f'{g.name}: only test-only groups use the test slot')
        if g.test_only:
            _need(hi <= TEST_SLOT[1] and g.id not in PLAN_SLOTS, f'{g.name}: a test-only group uses ids 12-15 and the test slot')
            asset_ranges = (TEST_ASSETS,)
            text_range = TEST_TEXT_SLOTS
        else:
            a, b = PLAN_SLOTS[g.id] if g.id in PLAN_SLOTS else (0, 0)
            _need(a <= lo < hi <= b, f'{g.name}: slot {lo:#x}..{hi:#x} is not inside the slot reserved for group {g.id}')
            if g.state:
                a, n = PLAN_STATE.get(g.id, (0, 0))
                _need(a <= g.state[0] and g.state[0] + g.state[1] <= a + n, f'{g.name}: state {g.state} is not inside the bytes reserved for group {g.id}')
            asset_ranges = PLAN_ASSETS.get(g.id, ())
            text_range = TEXT_SLOTS.get(g.id)
        for other, (a, b) in slots:
            _need(hi <= a or b <= lo, f'{g.name}: slot overlaps {other}')
        slots.append((g.name, g.slot))
        if g.state:
            off, n = g.state
            _need(n > 0 and off % 8 == 0 and FEATURE_STATE[0] <= off and off + n <= FEATURE_STATE[1], f'{g.name}: state {g.state}')
            for other, (a, m) in states:
                _need(off + n <= a or a + m <= off, f'{g.name}: state overlaps {other}')
            states.append((g.name, g.state))
        _need(0 not in g.requires and g.id not in g.requires, f'{g.name}: requires')
        _need(not g.terms or g.install_needs_q or any(r.needs_q for r in g.records), f'{g.name}: terms without a Q use')
        for t in g.terms:
            _need(t.width in (1, 4) and t.address % t.width == 0 and 0 <= t.mask < 1 << (8 * t.width) and not t.value & ~t.mask,
                  f'{g.name}: term at {t.address:#x}')
        for c in g.shared:
            _need(c.site in site_map, f'{g.name}: unknown site {c.site}')
            s = site_map[c.site]
            _need(c.hook in ('', hook_name(c.site, g.id)), f'{g.name}: the hook of site {c.site} is named {hook_name(c.site, g.id)}')
            _need(not s.order or g.test_only or any(gid == g.id for _, gid in s.order), f'{g.name}: not a contributor of site {c.site} (its order)')
            _need(not c.role or (g.test_only and c.role in s.roles()), f'{g.name}: only a test-only group names a role, one of the site\'s')
        _need(len({c.site for c in g.shared}) == len(g.shared), f'{g.name}: one contribution per site')
        for name, va in g.stock_calls.items():
            hit = [w for a, b, w in FLASH_ROUTINES if a <= va < b]
            _need(not hit, f'{g.name}: stock call {name} {va:#x} is a flash routine ({hit})')
            _need(not (0x401A0000 <= va < 0x40200000), f'{g.name}: stock call {name} inside the patch range')
        for name, va in g.save_calls.items():
            _need(SAVE_ENTRIES.get(va) and g.stock_calls.get(name) == va, f'{g.name}: save call {name} {va:#x} is not an allowed settings-save entry')
        if g.save_calls:
            _need(g.id == 2 or 2 in g.requires or g.test_only, f'{g.name}: a group that saves requires G2 (the save block)')
            _need(SAVE_TERM in g.terms and g.install_needs_q, f'{g.name}: a group that saves installs only under Q with SAVE_TERM')
        # Assets.
        for x in tuple(g.assets) + tuple(g.derived):
            _need(isinstance(x, (Asset, Derived)) and x.lo % 16 == 0 and x.lo < x.hi and any(a <= x.lo and x.hi <= b for a, b in asset_ranges),
                  f'{g.name}: asset {x.lo:#x}..{x.hi:#x} is not inside the asset ranges reserved for group {g.id}')
            if isinstance(x, Derived):
                mod, _, fn = x.builder.partition(':')
                _need(mod.startswith('z63patch.') and fn.isidentifier(), f'{g.name}: derived builder {x.builder!r}')
            for other, (a, b) in assets:
                _need(x.hi <= a or b <= x.lo, f'{g.name}: asset overlaps {other}')
            assets.append((g.name, (x.lo, x.hi)))
        # Rows template (G2): 45 stock rows grow to `count`; bytes past 0x840 lie in the group's own state.
        if g.rows:
            sym, count = g.rows
            _need(g.id == 2 or g.test_only, f'{g.name}: only G2 declares the video-menu rows')
            end = 44 * count
            _need(isinstance(sym, str) and 45 < count <= 64 and (end <= FEATURE_STATE[0] or (g.state and g.state[0] == FEATURE_STATE[0] and end <= g.state[0] + g.state[1])),
                  f'{g.name}: rows {count} do not fit 0x840 or the group state that starts there')
            # The video menu copy stays at 48 rows; "reset patch
            # settings" lives on the quality sub-page, not in a 49th row.
            _need(g.test_only or count <= VIDEO_MENU_ROWS_MAX, f'{g.name}: rows {count} above {VIDEO_MENU_ROWS_MAX}')
            rows.append(g.name)
        # Text slots.
        for slot in g.text_slots:
            _need(text_range is not None and text_range[0] <= slot <= text_range[1], f'{g.name}: text slot {slot} is not reserved for group {g.id}')
        # Clones.
        names = set()
        for c in g.clones:
            _need(isinstance(c, Clone) and c.name.isidentifier() and c.name not in names, f'{g.name}: clone name')
            names.add(c.name)
            _need((c.lo, c.hi) in CLONE_APPROVED, f'{g.name}: clone {c.lo:#x}..{c.hi:#x} is not an owner-approved range')
            _need(c.at % 16 == 0 and lo <= c.at and c.at + (c.hi - c.lo) <= hi, f'{g.name}: clone {c.name} does not fit the slot at {c.at:#x}')
            offs = [p.offset for p in c.patches]
            _need(len(set(offs)) == len(offs) and all(o % 4 == 0 and 0 <= o < c.hi - c.lo for o in offs), f'{g.name}: clone {c.name} patch offsets')
            _need(all(o % 4 == 0 and 0 <= o < c.hi - c.lo and o not in offs for o in c.literals), f'{g.name}: clone {c.name} literal offsets')
        # Entries.
        _need(len({e.symbol for e in g.entries}) == len(g.entries) and all(e.wrapper in WRAPPER_CLASSES for e in g.entries), f'{g.name}: entries')
        _need(all(e.lookup or e.wrapper == 'S' for e in g.entries), f'{g.name}: only an S-class entry skips the gate lookup')
        # Sanitiser.
        for k, t in enumerate(g.sanitise):
            where = f'{g.name}: sanitise {k} at {t.address:#x}'
            _need(t.width in (2, 4, 8) and t.address % t.width == 0 and DATA_RANGE[0] <= t.address < DATA_RANGE[1], f'{where}: width or address')
            _need(t.match_lo < t.match_hi <= 1 << (8 * t.width) and not t.match_lo <= t.stock < t.match_hi and t.stock < 1 << (8 * t.width), f'{where}: match range')
            if t.width == 8:
                own = [g.slot] + [(x.lo, x.hi) for x in g.assets]
                _need(any(a <= t.match_lo and t.match_hi <= b for a, b in own), f'{where}: a pointer match must lie inside the group\'s slot or assets')
            _need(all(isinstance(i, int) and 0 <= i < len(g.sanitise) and i != k for i in t.when), f'{where}: when')
            _need(g.install_needs_q or any(r.needs_q for r in g.records), f'{where}: a sanitiser needs a Q use')
        sets = {}
        for r in g.records:
            where = f'{g.name}: record at {r.address:#x}'
            _need(isinstance(r, (Code, Data, Value16)), f'{where}: record type')
            _need(r.cls.isupper() and len(r.cls) == 1 and r.wrapper in WRAPPER_CLASSES, f'{where}: class or wrapper')
            claim(r.address, _size(r), g.name)
            _need(r.address % _size(r) == 0, f'{where}: alignment')
            _need(bool(r.set) == (r.rank >= 1), f'{where}: set and rank')
            _need(not r.all_or_none or (r.set and r.needs_q), f'{where}: an all-or-none member needs a set and Q')
            _need(0 <= r.tx <= 15, f'{where}: TX pin mask')
            if r.set == VIDEO_MENU_SET:
                owner = VIDEO_MENU_RANKS.get(r.rank, 'coordinator' if r.rank < 7 else None)
                _need(not r.all_or_none and (owner == g.id or (g.test_only and owner is None)),
                      f'{where}: video-menu rank {r.rank} is not reserved for group {g.id}')
            if r.kind == 'data16':
                _need(0 <= r.old < 0x10000 and 0 <= r.new < 0x10000 and r.old != r.new and DATA_RANGE[0] <= r.address < DATA_RANGE[1], f'{where}: u16 value')
            elif r.kind == 'data64':
                _need(r.target in ('code', 'data'), f'{where}: target')
                _need(r.target == 'code' or not r.stores, f'{where}: a data target has no code')
                _need(r.lookup or (r.wrapper == 'S' and r.target == 'code'), f'{where}: only an S-class wrapper skips the gate lookup')
            else:
                branch = r.old >> 26 in (0x05, 0x25)
                _need(branch or r.needs_q, f'{where}: a non-branch stock word restores only under Q')
                _need(r.lookup or r.wrapper == 'S', f'{where}: only an S-class wrapper skips the gate lookup')
                _need(not r.continuation or (not branch and r.needs_q and not pc_relative(r.old)), f'{where}: a continuation displaces one non-branch, position-independent word, under Q')
            if r.set:
                sets.setdefault(r.set, []).append(r)
        for name, members in sets.items():
            _need(len({m.all_or_none for m in members}) == 1, f'{g.name}: set {name} mixes all-or-none and plain members')
            _need(len({m.rank for m in members}) == len(members), f'{g.name}: set {name} repeats a rank')
            if members[0].all_or_none:
                _need(len(members) <= 64, f'{g.name}: all-or-none set {name} above 64 members')
    _need(len([g for g in groups if g.rows and not g.test_only]) <= 1, 'more than one group declares the video-menu rows')
    for g in groups:
        for d in g.requires:
            _need(d in ids, f'{g.name}: requires undeclared group {d}')
        _need(isinstance(g.restore_after, tuple) and all(isinstance(d, int) and g.id < d < GROUP_BITS for d in g.restore_after),
              f'{g.name}: restore_after names only groups with a larger id (restored earlier, the table is in id order)')
        _need(isinstance(g.restore, bool) and isinstance(g.stock_header, str), f'{g.name}: restore or stock_header')
        _need(isinstance(g.publish, bool) and isinstance(g.hold, int), f'{g.name}: publish or hold')
        _need(isinstance(g.restore_now, bool) and isinstance(g.raises, bool) and isinstance(g.pass_cap, str), f'{g.name}: restore_now, raises or pass_cap')
        _need(not g.pass_cap or ((PASS_CAPS.get(g.id) == g.pass_cap or g.test_only) and any(c.site == 'nraw_budget' for c in g.shared)),
              f'{g.name}: pass_cap {g.pass_cap!r} is not the declared cap of a nraw_budget contributor')
        _need(not g.hold or (g.state and g.hold % 4 == 0 and g.state[0] <= g.hold and g.hold + 4 <= g.state[0] + g.state[1]),
              f'{g.name}: the anchor value word {g.hold:#x} is not an aligned word of its own state')
        if g.stock_header:
            mod, _, fn = g.stock_header.partition(':')
            _need(mod.startswith('z63patch.') and fn.isidentifier(), f'{g.name}: stock_header {g.stock_header!r}')
        # Movie-view arming: qm_install True only when no non-branch word lacks evidence;
        # each evidence entry names one non-branch code record of the group.
        _need(any(g.qm_install is v for v in (None, True, False)), f'{g.name}: qm_install is None, True or False')
        nonbranch = {r.address for r in g.records if r.kind == 'code32' and not _is_branch(r.old)}
        _need(isinstance(g.qm_evidence, tuple) and all(isinstance(e, QmEvidence) and e.address in nonbranch and
                                                       isinstance(e.evidence, str) and e.evidence.strip() for e in g.qm_evidence)
              and len({e.address for e in g.qm_evidence}) == len(g.qm_evidence),
              f'{g.name}: qm_evidence names a word that is not one of its non-branch code records, or has no evidence text')
        _need(g.qm_install is not True or not unproven_words(g),
              f'{g.name}: qm_install=True with non-branch words without evidence: {[hex(a) for a in unproven_words(g)]}')
    return True


def composition_mask(groups):
    return 1 | sum(1 << g.id for g in groups)
