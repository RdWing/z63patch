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
"""Independent verification of a composed arm-time gate candidate (stages S7 on).

Written separately from z63patch/gate.py and z63patch/arm_time.py and imports neither. It uses
the S1-S6 verifier's decoders (z63patch/verify_arm_time.py) and the group declarations
(z63patch/gate_groups), and checks:

 1. stock is exact C2.01, same length; the reviewed recipe's output hash and edits (pinned);
 2. the header recomputed (magic, version, build ID, CRC-32 of [0x401A0000, 0x401FFF00), entry
    0x401A0000, table 0x401F6000, count); the guard (version 2: the 16 s
    counter test replaced by the update-client test) rebuilt byte for byte, its branch targets
    and loads; the hook word;
 3. the composition: every group named exists in the declarations; the declarations meet their
    rules (ids, slots, state ranges, one record per word, sets); every group's dependencies are
    in the composition; the build mask is bit 0 plus the composed ids; the state page map is the
    fixed one and the composed groups' feature state fits in 368 bytes at +0x840;
 4. layout: the core at 0x401A0000 ends below 0x401A5000 and its card-detect arming section
    (.text.z63card) lies at 0x401A5000 and ends at or below 0x401A6000; each
    group's code lies in its declared
    slot (recipe slot equal to the declaration); the install table at 0x401F6000 ends at or
    below 0x401FFC00 with at most 800 records; zero bytes everywhere else in the patch range;
    changed bytes confined to header, guard, hook word, core, group code and table;
 5. every record against its source: the infrastructure records as S6 (page record first, the
    exact stock callers of the two pipeline entries through thunks of the reviewed shape, the
    two dispatcher callbacks); each shared site used by the composition once, contributor mask =
    the composed contributors, its merged adapter in the core; each composed group's declared
    records exactly once (stock old value, not pre-installed, B/BL per declaration to the
    declared symbol inside the group's code, class, wrapper class, restore-under-Q, all-or-none,
    set and rank); no other record; one record per word; ranks rise inside a set; an
    all-or-none set contiguous, one group, restore under Q, at most 64 members;
 6. code rules on the core and on every group: no system-register
    write, exception call, wait, TLB or cache maintenance; every exclusive store in a bounded
    loop; the core's veneer calls only to the allowed stock functions; a group's code has no
    indirect branch and calls only its declared stock functions; from every wrapper entry (the
    core API, the site adapters, every group adapter and hook) the reachable code has only
    bounded loops and a stack bound of 256 bytes; the core API, the site adapters and every
    group adapter make the acquire load of the publication word their first access; stores
    outside the frame only where declared (plain stores never in the core API or site adapters);
 7. no flash programming: the stock flash-writing routines (FLASH_FIXED, a pinned list of
    ranges, plus every text veneer into the RAM-copied ones, found here in the image by its
    four-word shape). No call or branch of the core or of a group goes into them, no
    constant the code builds (movz/movk, adrp, literal words) names them, and every stock
    function the code calls on its own account cannot reach them by direct branches and veneers
    (bounded walk; calls through registers inside stock code are counted, not followed). A group
    may call a reaching function only when it is a declared settings-save entry
    (gate_groups.SAVE_ENTRIES, empty today). Calls that only stand in for the call the record
    replaces (thunks, the poll, a site's stock handler, a group's replaced branch target) are
    walked and reported;
 8. MAIN only: the candidate is a MAIN image of the stock length and differs from stock only in
    the flashed set (guard, hook word, the patch range with its header);
 9. build-time derived assets (Derived): recomputed here from the stock image with the declared
    builder and compared byte for byte (and with the recipe's SHA-256), scanned like any asset;
    restore callbacks (Group.restore): in the group's code, generated into the core exactly as
    declared, walked with the wrapper rules with no external call at all (no stock function, no
    other group), bounded, stack <= 256 B; the cross-group restore order (restore_after) is the
    core's generated Z63_AFTER;
10. update protection: the last two
    records, and only they, are the pinned deny records (flag 4, class X, group 0, wrapper X, no
    set): exactly 0x410143EC `b 0x41014ED4` and 0x4101442C `b 0x4101557C`, each replaced by
    `b z63_update_denied`, which is exactly `mov w0, #2; ret` in the core; no other record (code
    or data) touches the protected update ranges (UPDATE_DENY_LIST, a pinned list).
11. the card-detect arming section: checked with the core as one code range (code rules,
    allow-lists, flash rule, the card-detect stack bound of z63_entry and z63_armed, at most
    CARD_STACK bytes); no wrapper, thunk, core API function or site adapter reaches it.
"""
import hashlib
import json
import struct
import zlib
from pathlib import Path

from . import verify_arm_time as V
from . import gate_groups as GG

UPDATE_CLIENT = 0xA04073C70   # guard v2 reads it: nonzero -> stock poll

ROOT = Path(__file__).resolve().parents[1]
need, off, word, sx, Reject = V.need, V.off, V.word, V.sx, V.Reject
LO, HDR, HI = V.LO, V.HDR, V.HI
TABLE, TABLE_END, REC, FMT = 0x401F6000, 0x401FFC00, 40, '<BBHHBBBBBBIQQQ'
STACK = 256
# The core's card-detect arming section, held here independently of the builder, and the
# bound on the patch frames of the card-detect paths (the card-detect task's stack is 8 KiB).
CARD_LO, CARD_HI, CARD_STACK = 0x401A5000, 0x401A6000, 2048
CORE_API = ('z63_enter', 'z63_leave', 'z63_admit', 'z63_exit', 'z63_effective')
PAGE_MAP_KEYS = ('0x000', '0x840', '0x9b0', '0x9f0', '0xc40', '0xc60', '0xf80')
FEATURE_LO, FEATURE_HI, STATUS_OFF, STATUS_MAX, GATE_OFF = 48 * 44, 0x9B0, 0xC60, 800, 0xF80
assert FEATURE_LO == 0x840 and FEATURE_HI - FEATURE_LO == 368 and STATUS_OFF + STATUS_MAX == GATE_OFF
WC = {'A': 1, 'C': 2, 'F': 3, 'S': 4, 'X': 5}
KIND = {'data64': 1, 'code32': 2, 'data16': 4}
LDAR_X = 0xC8DFFC00
# Held here independently of the declarations: the per-group
# feature-state offsets, the owner-approved clone ranges with their stock hashes, the text-slot
# entry points whose slot argument (w1) is checked.
STATE_REGISTRY = {2: (0x840, 112), 4: (0x8B0, 8), 6: (0x9A0, 8), 7: (0x8B8, 16), 8: (0x9A8, 8), 9: (0x8C8, 8), 10: (0x8D0, 64),
                  11: (0x920, 128)}
CLONES = {(0x4111CD60, 0x4111D480): 'e1cda0f085445bd4d1a66b8f0ea6afe1ebe459103d7d6f0599a846e5e7a56142',
          (0x4111D618, 0x4111DF4C): '380ad0ac564ad841d4aa1e7a7c0f88a328734a978d10669491023433180ced51'}
TEXT_CALLS = {0x404066C8: 'TEXT_STORE(text, slot, style)', 0x4040A1AC: 'TEXT_BIND(widget, slot)'}
# The two pinned deny words (stock B word -> its target), held here independently of the
# builder; the deny routine's status and code.
UPDATE_DENY = {0x410143EC: 0x41014ED4, 0x4101442C: 0x4101557C}
UPDATE_DENIED = 2
DENY_CODE = (0x52800000 | UPDATE_DENIED << 5, 0xD65F03C0)          # mov w0, #2; ret
F_PIN = 4
# No record (code or data, any width) may touch these, the two deny words excepted.
UPDATE_DENY_LIST = ((0x41013000, 0x4101E300, 'protected range'),
                    (0x40CA8000, 0x40CA9000, 'protected range'),
                    (0xA02BDE000, 0xA02BE1028, 'protected range'),
                    (0xA0298B870, 0xA0298B878, 'protected range'),
                    (0xA02E766A0, 0xA02E766B0, 'protected range'),
                    (0xA03054D40, 0xA03054E40, 'protected range'),
                    (0xA0343BC18, 0xA0343BC40, 'protected range'),
                    (0xA040739C8, 0xA04076C00, 'protected range'),
                    (0xA02FB47F8, 0xA02FB4800, 'protected range'),
                    (0xA0305521C, 0xA03055220, 'protected range'),
                    (0xA04F10008, 0xA04F10010, 'protected range'))


# Pipeline classes (0xA03D42108) a QM arming may accept, each observed (gate_groups.QM_CLASSES):
# both are movie-path results of the class function 0x410538FC (7 ordinary, 0x0A N-RAW/ProRes RAW).
QM_CLASSES_VERIFIED = {7, 0x0A}
# The GUI app controller the arming views read (gate_groups GUI_*), kept here independently of the
# declarations: its address, the fields the core reads, and the stock words that tie them to stock
# code (getter literal, self-pointer store, handler registration, app-id store, hold-bit set/clear).
GUI_VERIFIED = dict(object=0xA0282E8C8, self=0xA0282EF28, hold=0xA0282EF48, app=0xA0282EF90)
# The words are not stored; the verifier keeps its own site list and digest
# (SHA-256 of the tag, then per site: address u64, 4 u32, stock word) and recomputes it from stock.
GUI_SITES_VERIFIED = (0x40D59224, 0x40D59228, 0x40D5922C, 0x40D59230, 0x40D59234, 0x40D5A694, 0x40D5A628, 0x40D5A630,
                      0x40D5A950, 0x40D5D444, 0x40D5D448, 0x40D5B370, 0x40D5B374)
GUI_DIGEST_VERIFIED = '0d05e11dff781987f65c7d562ad8bc346c8f82ead7282808d45faad672129379'


def recipe(stage):
    return json.loads((ROOT / 'profiles' / f'z6iii-c201-arm-time-{stage}-recipe.json').read_text())


FLASH_FIXED = ((0xA02BDE000, 0xA02BE1000, 'protected range'),
               (0x455CB000, 0x455CE000, 'protected range'),
               (0x40CA8220, 0x40CA8BDC, 'protected range'),
               (0x40D72E14, 0x40D72F6C, 'protected range'),
               (0x40D759B4, 0x40D76F08, 'protected range'),
               (0x40C07EB8, 0x40C080B4, 'protected range'),
               (0x410DEE08, 0x410DEE38, 'protected range'),
               (0x41013000, 0x4101E300, 'protected range'))
VENEER = struct.pack('<IIII', 0x58000090, 0x10000011, 0x8B110210, 0xD61F0200)   # ldr x16,lit; adr x17; add; br x16
REACH_LIMIT = 300000
_FLASH = {}


def flash_routines(stock):
    """(ranges, {veneer: helper}) of the stock flash routines."""
    if 'v' not in _FLASH:
        ven = {}
        i = stock.find(VENEER)
        while i >= 0:
            t = (i + 4 + V.BASE + struct.unpack_from('<Q', stock, i + 16)[0]) & (1 << 64) - 1
            if 0xA02BDE000 <= t < 0xA02BE1000:
                ven[i + V.BASE] = t
            i = stock.find(VENEER, i + 4)
        _FLASH['v'] = ven
        # Every byte of a veneer (four words and its 8-byte literal) counts, not only its entry.
        _FLASH['vb'] = {v + k for v in ven for k in range(0, 24)}
    return FLASH_FIXED, _FLASH['v']


def is_flash(stock, va):
    ranges, ven = flash_routines(stock)
    return va in _FLASH['vb'] or any(a <= va < b for a, b, _ in ranges)


def _built_constants(code, base):
    """Every value the code builds in a register (movz/movk, adrp, adr, add immediate), with no
    address-range filter (verify_arm_time.scan_code keeps only text addresses, which would miss
    the protected RAM ranges)."""
    regs = {}
    out = set()
    for i in range(0, len(code) - 3, 4):
        w = struct.unpack_from('<I', code, i)[0]
        pc = base + i
        rd = w & 31
        if w & 0x7F800000 == 0x52800000:
            regs[rd] = ((w >> 5) & 0xFFFF) << (16 * ((w >> 21) & 3))
        elif w & 0x7F800000 == 0x72800000:
            hw = (w >> 21) & 3
            regs[rd] = (regs.get(rd, 0) & ~(0xFFFF << (16 * hw))) | (((w >> 5) & 0xFFFF) << (16 * hw))
        elif w & 0x9F000000 == 0x90000000:
            regs[rd] = (pc & ~0xFFF) + (sx(((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3), 21) << 12)
        elif w & 0x9F000000 == 0x10000000:
            regs[rd] = pc + sx(((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3), 21)
        elif w & 0x7FC00000 == 0x11000000 and ((w >> 5) & 31) in regs:
            regs[rd] = regs[(w >> 5) & 31] + ((w >> 10) & 0xFFF)
        else:
            regs.pop(rd, None)
            continue
        out.add(regs[rd] & (1 << 64) - 1)
    return out


def _direct_branches(code, base):
    """(pc, kind, target) of every word that decodes as a direct branch (b, bl, b.cond, cbz/cbnz,
    tbz/tbnz), reachable or not, data words included."""
    out = []
    for i in range(0, len(code) - 3, 4):
        w = struct.unpack_from('<I', code, i)[0]
        b = V._branch(w, base + i)
        if b and b[1] is not None:
            out.append((base + i, b[0], b[1]))
    return out


def stock_reach(stock, target):
    """('clean'|'flash'|'limit', detail, instructions walked, register calls not followed)."""
    ranges, ven = flash_routines(stock)
    seen = set()
    work = [target]
    indirect = 0
    while work:
        pc = work.pop()
        if pc in seen:
            continue
        if is_flash(stock, pc):
            return 'flash', hex(pc), len(seen), indirect
        if not V.BASE <= pc < V.BASE + V.DATA_OFFSET:
            indirect += 1
            continue
        seen.add(pc)
        if len(seen) > REACH_LIMIT:
            return 'limit', None, len(seen), indirect
        w = struct.unpack_from('<I', stock, pc - V.BASE)[0]
        if w & 0x7C000000 == 0x14000000:
            work.append(pc + 4 * sx(w & 0x3FFFFFF, 26))
            if w >> 31:
                work.append(pc + 4)
        elif w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000:
            work += [pc + 4 * sx((w >> 5) & 0x7FFFF, 19), pc + 4]
        elif w & 0x7E000000 == 0x36000000:
            work += [pc + 4 * sx((w >> 5) & 0x3FFF, 14), pc + 4]
        elif w & 0xFFFFFC1F == 0xD65F0000 or w == 0 or w & 0xFFE0001F == 0xD4200000:
            pass
        elif w & 0xFFFFFC1F == 0xD61F0000:
            indirect += 1
        elif w & 0xFFFFFC1F == 0xD63F0000:
            indirect += 1
            work.append(pc + 4)
        else:
            work.append(pc + 4)
    return 'clean', None, len(seen), indirect


def _flash_rules(stock, code, base, name, calls, own, save=()):
    """calls: external targets of the code; own: those it makes on its own account."""
    constants = _built_constants(code, base)
    lits = {struct.unpack_from('<Q', code, i)[0] for i in range(0, len(code) - 7, 4)}
    lits |= {struct.unpack_from('<I', code, i)[0] for i in range(0, len(code) - 3, 4)}   # 32-bit literals
    named = sorted(hex(c) for c in constants | lits if is_flash(stock, c))
    need(not named, f'{name}: builds the address of a flash routine {named}')
    # Every direct branch word, reached from a wrapper entry or not (tail branches and
    # conditional branches included, not only BL).
    into = sorted(hex(pc) for pc, _, t in _direct_branches(code, base) if is_flash(stock, t))
    need(not into, f'{name}: branches into a flash routine at {into}')
    out = {}
    for t in sorted(calls):
        need(t is not None and not is_flash(stock, t), f'{name}: calls a flash routine at {t:#x}' if t else f'{name}: call without its address')
        r = stock_reach(stock, t)
        out[hex(t)] = dict(reach=r[0], via=r[1], walked=r[2], register_calls_not_followed=r[3], own=t in own)
        if t in own and t not in save:
            need(r[0] == 'clean', f'{name}: stock call {t:#x} can reach a flash routine ({r[0]} {r[1]})')
    return out


def _code_rules(code, base, name):
    constants, problems, calls = V.scan_code(code, base)
    need(not problems, f'{name}: forbidden instructions {problems}')
    n = len(code)
    for i in range(0, n - 3, 4):
        w = struct.unpack_from('<I', code, i)[0]
        if V._exclusive_store(w):
            need(i + 12 < n and struct.unpack_from('<I', code, i + 4)[0] & 0xFF00001F == 0x34000000 | (w >> 16) & 31
                 and V._bounded_loop(code, base, base + i + 12), f'{name}: exclusive store outside a bounded loop at {base + i:#x}')
    return constants, calls


def _fp_frame(code, base, pcs):
    """The offset of x29 above sp when every write of x29 in the function is "add x29, sp, #imm"
    (one value) or a load of x29 immediately followed by ret (the epilogue); else None."""
    imm = None
    for pc in pcs:
        w = struct.unpack_from('<I', code, pc - base)[0]
        if w & 0xFFC003FF == 0x910003FD:                       # add x29, sp, #imm (mov x29, sp)
            v = (w >> 10) & 0xFFF
            if imm not in (None, v):
                return None
            imm = v
            continue
        pair_load = w & 0x3A400000 == 0x28400000 and 29 in (w & 31, (w >> 10) & 31)
        load = w & 0x0A400000 == 0x08400000 and w & 31 == 29 and w & 0x3B000000 != 0x18000000
        other = (w & 31 == 29 and not V._is_store(w) and not V._branch(w, pc) and w & 0x0A000000 != 0x08000000)
        if pair_load or load:
            # the epilogue: only sp-based loads and "add sp, sp, #imm" until the ret
            nxt = pc + 4
            for _ in range(16):
                if nxt - base + 4 > len(code):
                    return None
                x = struct.unpack_from('<I', code, nxt - base)[0]
                if x == 0xD65F03C0:
                    break
                sp_load = x & 0x0A400000 == 0x08400000 and (x >> 5) & 31 == 31 and x & 0x3B000000 != 0x18000000
                if not (sp_load or x & 0xFFC003FF == 0x910003FF):
                    return None
                nxt += 4
            else:
                return None
        elif other:
            return None
    return imm


def _plain_stores(code, base, funcs):
    """Stores outside the frame: any store whose base is not sp, except a store through x29 that
    lands inside the function's own frame (x29 = sp + imm set once, offset inside the frame):
    str/stur, or stp of two general registers with a signed offset, both registers inside."""
    plain = []
    for pcs, _, _ in funcs.values():
        fp = _fp_frame(code, base, pcs)
        frame = V._frame(code, base, pcs)
        for pc in pcs:
            w = struct.unpack_from('<I', code, pc - base)[0]
            if not V._is_store(w) or (w >> 5) & 31 == 31 or V._exclusive_store(w):
                continue
            if (w >> 5) & 31 == 29 and fp is not None:
                size = 1 << (w >> 30)
                if w & 0x3B200C00 == 0x38000000:               # stur (unscaled, signed 9-bit)
                    o = sx((w >> 12) & 0x1FF, 9)
                elif w & 0x3B000000 == 0x39000000:             # str (unsigned scaled 12-bit)
                    o = ((w >> 10) & 0xFFF) * size
                elif w & 0x3FC00000 == 0x29000000 and w >> 30 in (0, 2):
                    # stp of two general registers, signed offset (no writeback, so x29 is
                    # unchanged): the same in-frame rule over both registers (G7-REQUESTS R4)
                    size = 8 if w >> 31 else 4
                    o = sx((w >> 15) & 0x7F, 7) * size
                    size *= 2
                else:
                    o = None
                if o is not None and 0 <= fp + o and fp + o + size <= frame:
                    continue
            plain.append(hex(pc))
    return plain


def _wrapper(code, base, entry, allowed, name, stores, first_access):
    funcs, ext, probs, bound = V._walk_wrapper(code, base, entry)
    need(not probs, f'{name}: {probs}')
    need(ext <= allowed, f'{name}: calls {sorted(hex(x) for x in ext - allowed)} outside its allowed calls')
    bad = V._loops(code, base, funcs)
    need(not bad, f'{name}: unbounded loop at {bad}')
    need(bound <= STACK, f'{name}: stack bound {bound} above {STACK}')
    plain = _plain_stores(code, base, funcs)
    need(stores or not plain, f'{name}: stores outside its frame at {sorted(plain)}')
    if first_access:
        pc, w, value = V._first_access(code, base, entry)
        need(w & 0xFFFFFC00 == LDAR_X and value == V.ITEMS_POINTER,
             f'{name}: the first access at {pc:#x} is not the acquire load of the publication word')
    return dict(entry=hex(entry), functions=len(funcs), stack_bound=bound, external_calls=sorted(hex(x) for x in ext),
                plain_stores=len(plain))


def _explore(code, base, entry, tails):
    """verify_arm_time._explore with allowed tail branches: an unconditional b to an address in
    `tails` leaves the code (recorded), any other branch out is a problem."""
    n = len(code)
    seen = set()
    work = [entry]
    calls = set()
    external = set()
    used = set()
    problems = []
    edges = []
    while work:
        pc = work.pop()
        if pc in seen:
            continue
        if not base <= pc < base + n:
            problems.append(f'{pc:#x}: control leaves the code')
            continue
        seen.add(pc)
        w = struct.unpack_from('<I', code, pc - base)[0]
        b = V._branch(w, pc)
        if b is None:
            work.append(pc + 4)
            continue
        kind, t = b
        if kind == 'ret':
            continue
        if kind == 'br':
            problems.append(f'{pc:#x}: indirect jump')
            continue
        if kind == 'blr':
            problems.append(f'{pc:#x}: indirect call')
            work.append(pc + 4)
            continue
        if kind == 'bl':
            (calls if base <= t < base + n else external).add(t)
            work.append(pc + 4)
            continue
        if not base <= t < base + n:
            if kind == 'b' and t in tails:
                used.add((pc, t))
            else:
                problems.append(f'{pc:#x}: branch out of the code to {t:#x}')
            continue
        edges.append((pc, t))
        work.append(t)
        if kind == 'cond':
            work.append(pc + 4)
    return seen, calls, external, used, problems, edges


def _walk(code, base, entry, tails):
    funcs = {}
    external = set()
    used = set()
    problems = []
    stack = [entry]
    while stack:
        f = stack.pop()
        if f in funcs:
            continue
        pcs, calls, ext, u, probs, edges = _explore(code, base, f, tails)
        funcs[f] = (pcs, calls, edges)
        external |= ext
        used |= u
        problems += probs
        stack += list(calls)
    depth = {}

    def d(f, trail=()):
        if f in trail:
            problems.append(f'{f:#x}: recursion')
            return 0
        if f not in depth:
            depth[f] = V._frame(code, base, funcs[f][0]) + max((d(c, trail + (f,)) for c in funcs[f][1]), default=0)
        return depth[f]
    return funcs, external, used, problems, d(entry)


def _group_wrapper(code, base, entry, allowed, tails, name, stores, first_access, displaced=None):
    """Wrapper rules for a group entry (record adapter, data-reached entry, hook, clone patch
    adapter): the walk with its allowed tails, calls only to `allowed`, bounded loops, stack bound,
    stores, the first access. displaced: {tail target: word} that must precede the tail b."""
    funcs, ext, used, probs, bound = _walk(code, base, entry, tails)
    need(not probs, f'{name}: {probs}')
    need(ext <= allowed, f'{name}: calls {sorted(hex(x) for x in ext - allowed)} outside its allowed calls')
    bad = V._loops(code, base, funcs)
    need(not bad, f'{name}: unbounded loop at {bad}')
    need(bound <= STACK, f'{name}: stack bound {bound} above {STACK}')
    pcs_all = set()
    for pcs, _, _ in funcs.values():
        pcs_all |= pcs
    plain = _plain_stores(code, base, funcs)
    need(stores or not plain, f'{name}: stores outside its frame at {sorted(plain)}')
    for pc, t in used:
        if displaced and t in displaced:
            need(pc - 4 >= base and struct.unpack_from('<I', code, pc - 4 - base)[0] == displaced[t] and pc - 4 in pcs_all,
                 f'{name}: the continuation b {t:#x} at {pc:#x} is not preceded by the displaced word {displaced[t]:#010x}')
    if first_access:
        pc, w, value = V._first_access(code, base, entry)
        need(w & 0xFFFFFC00 == LDAR_X and value == V.ITEMS_POINTER,
             f'{name}: the first access at {pc:#x} is not the acquire load of the publication word')
    return dict(entry=hex(entry), functions=len(funcs), stack_bound=bound, external_calls=sorted(hex(x) for x in ext),
                tails=sorted(hex(t) for _, t in used), plain_stores=len(plain)), pcs_all, ext | {t for _, t in used}


PASS_CAP_TABLES = {'og_budget_cap': (0xA02B1453C, 0x24)}     # R-G10-2: the stock factor table (9 words)


def _pass_cap(code, base, va, name, table):
    """R-G10-2: the nraw_budget pass cap, called by the core adapter in every phase, also
    with no page. No call or branch out, bounded, stack <= 64 B; no exclusive or acquire/release
    access (so no gate CAS and no acquire load of P); every address-like constant it builds
    (text, NOR or RAM) inside its declared table; at most one store outside its frame, and that one
    `str w, [x, #0xb8]`. Loads relative to the request are not bounded here (the group's replay
    tests bound them)."""
    rule, pcs, ext = _group_wrapper(code, base, va, set(), set(), name, True, False)
    need(not ext, f'{name}: the pass cap calls or branches out of the group ({sorted(hex(x) for x in ext)})')
    need(rule['stack_bound'] <= 64, f'{name}: stack bound {rule["stack_bound"]} above 64')
    funcs, _, _, _, _ = _walk(code, base, va, set())
    regs = {}
    for pc in sorted(pcs):
        w = struct.unpack_from('<I', code, pc - base)[0]
        need(w & 0x3F000000 != 0x08000000, f'{name}: exclusive or acquire/release access at {pc:#x}')
        rd = w & 31
        if w & 0x7F800000 == 0x52800000:
            regs[rd] = ((w >> 5) & 0xFFFF) << (16 * ((w >> 21) & 3))
        elif w & 0x7F800000 == 0x72800000 and rd in regs:
            hw = (w >> 21) & 3
            regs[rd] = (regs[rd] & ~(0xFFFF << (16 * hw))) | (((w >> 5) & 0xFFFF) << (16 * hw))
        elif w & 0x1F000000 == 0x10000000:
            need(False, f'{name}: pc-relative address at {pc:#x}')
        else:
            regs.pop(rd, None)
            continue
        v = regs[rd]
        if 0x40000000 <= v < 0x48000000 or 0xA00000000 <= v < 0xA40000000:
            need(table[0] <= v < table[0] + table[1], f'{name}: address constant {v:#x} at {pc:#x} outside its table')
    plain = _plain_stores(code, base, funcs)
    need(len(plain) <= 1, f'{name}: stores outside its frame at {plain}')
    for pc in plain:
        w = struct.unpack_from('<I', code, int(pc, 16) - base)[0]
        need(w & 0xFFFFFC00 == 0xB900B800, f'{name}: the store at {pc} is not str w, [x, #0xb8]')
    return dict(rule, stores=plain)


def _text_slots(code, base, name, declared):
    """Constant slot arguments (w1 set by movz just before the call) of the code's direct calls to
    TEXT_STORE and TEXT_BIND: each must be a slot the group declares. Calls whose w1 is not a
    constant set in the 8 words before the call are reported, not refused."""
    unresolved = []
    for i in range(0, len(code) - 3, 4):
        w = struct.unpack_from('<I', code, i)[0]
        if w & 0xFC000000 != 0x94000000:
            continue
        t = base + i + 4 * sx(w & 0x3FFFFFF, 26)
        if t not in TEXT_CALLS:
            continue
        slot = None
        for k in range(i - 4, max(-4, i - 36), -4):
            x = struct.unpack_from('<I', code, k)[0]
            if x & 31 == 1 and x & 0x7FE00000 == 0x52800000 and not x >> 31:
                slot = (x >> 5) & 0xFFFF
                break
            if x & 31 == 1 or V._branch(x, base + k):
                break
        if slot is None:
            unresolved.append(hex(base + i))
        else:
            need(slot in declared, f'{name}: text slot {slot} at {base + i:#x} is not declared (text_slots {sorted(declared)})')
    return unresolved


def _clone_rules(stock, candidate, g, c, gsym):
    """A clone equals the approved stock bytes except: its declared patch words (B/BL to the
    declared adapter), and PC-relative words re-encoded to the same absolute target outside the
    range (inside it they are unchanged). No literal or address load unless declared. Returns the
    clone's external targets."""
    need((c.lo, c.hi) in CLONES and hashlib.sha256(stock[off(c.lo):off(c.hi)]).hexdigest() == CLONES[(c.lo, c.hi)],
         f'{g.name}: clone {c.name} is not an owner-approved range with its stock bytes')
    size = c.hi - c.lo
    patches = {pt.offset: pt for pt in c.patches}
    ext = set()
    for k in range(0, size, 4):
        sw = word(stock, c.lo + k)
        cw = word(candidate, c.at + k)
        where = f'{g.name}: clone {c.name} +{k:#x}'
        if k in patches:
            pt = patches[k]
            target = gsym.get(pt.symbol)
            need(target is not None and cw == (0x94000000 if pt.link else 0x14000000) | (((target - c.at - k) >> 2) & 0x3FFFFFF),
                 f'{where}: patch word is not {"bl" if pt.link else "b"} {pt.symbol}')
            continue
        sb = V._branch(sw, c.lo + k)
        if sw & 0x3B000000 == 0x18000000 or sw & 0x1F000000 == 0x10000000:
            # Literal and address loads: only declared ones, and only unchanged (position
            # independent: the target lies inside the clone).
            need(k in c.literals, f'{where}: literal or address load not declared')
            need(cw == sw, f'{where}: literal or address load changed')
            continue
        if sb and sb[1] is not None and not c.lo <= sb[1] < c.hi:
            d = (sb[1] - c.at - k) >> 2
            if sw & 0x7C000000 == 0x14000000:
                want = (sw & 0xFC000000) | (d & 0x3FFFFFF)
                ok = -(1 << 25) <= d < 1 << 25
            elif sw & 0x7E000000 == 0x36000000:
                want = (sw & ~(0x3FFF << 5)) | (d & 0x3FFF) << 5
                ok = -(1 << 13) <= d < 1 << 13
            else:
                want = (sw & ~(0x7FFFF << 5)) | (d & 0x7FFFF) << 5
                ok = -(1 << 18) <= d < 1 << 18
            need(ok and cw == want, f'{where}: external branch not relocated to {sb[1]:#x}')
            ext.add(sb[1])
            continue
        need(cw == sw, f'{where}: differs from stock')
    return ext


def verify(stock, candidate, stage, *, recipe_data=None, pinned=True, extra=()):
    """extra: test-only group declarations of a test composition (never in a reviewed recipe)."""
    need(hashlib.sha256(stock).hexdigest() == V.STOCK_SHA256, 'stock C2.01 MAIN required')
    need(len(candidate) == len(stock), 'candidate length differs')
    r = recipe_data if recipe_data is not None else recipe(stage)
    need(r.get('status') == 'DO_NOT_FLASH' and r.get('stage') == stage, 'recipe stage or status')
    out = dict(stage=stage, candidate_sha256=hashlib.sha256(candidate).hexdigest())
    need(not pinned or out['candidate_sha256'] == r['output_sha256'], 'candidate is not the reviewed recipe output')
    # ---- 3. composition ----
    try:
        declared = GG.compose(r['composition'], dict(GG.discover()))
    except ValueError as e:
        raise Reject(f'declarations: {e}')
    for g in extra:
        need(g.test_only and g.name not in declared, f'extra group {g.name}')
        declared[g.name] = g
    site_map = GG.shared_sites()
    try:
        GG.check(list(declared.values()), site_map)
    except ValueError as e:
        raise Reject(f'declarations: {e}')
    names = r['composition']
    need(len(set(names)) == len(names) and all(n in declared for n in names), f'composition {names} names undeclared groups')
    groups = sorted((declared[n] for n in names), key=lambda g: g.id)
    need(not any(g.test_only for g in groups) or extra, 'a test-only group in a recipe')
    ids = {g.id for g in groups}
    for g in groups:
        need(set(g.requires) <= ids, f'{g.name}: dependencies {sorted(set(g.requires) - ids)} not composed')
    mask = 1 | sum(1 << g.id for g in groups)
    need(r['groups'] == mask, f'build mask {r["groups"]:#x} is not {mask:#x}')
    need(sorted(r['state_page']) == sorted(PAGE_MAP_KEYS), 'state page map is not the fixed one')
    # The fixed map, held here independently of the
    # declarations: rows 48 x 44 = 2,112, feature state 368 B at +0x840, counters +0x9B0,
    # statuses +0xC60 (800), gate block +0xF80.
    need(GG.FEATURE_STATE == (FEATURE_LO, FEATURE_HI) and GG.MAX_RECORDS == STATUS_MAX, 'declared state-page map is not the fixed one')
    need(GG.PLAN_STATE == STATE_REGISTRY, 'declared feature-state registry is not the fixed one')
    need(GG.CLONE_APPROVED == CLONES, 'declared clone approvals differ from the owner-approved list')
    for g in groups:
        need(not g.state or (FEATURE_LO <= g.state[0] and g.state[0] + g.state[1] <= FEATURE_HI), f'{g.name}: state outside +0x840..+0x9B0')
        if g.state and not g.test_only:
            a, m = STATE_REGISTRY.get(g.id, (0, 0))
            need(a <= g.state[0] and g.state[0] + g.state[1] <= a + m, f'{g.name}: state outside the bytes registered for group {g.id}')
    need(sum(g.state[1] for g in groups if g.state) <= FEATURE_HI - FEATURE_LO, 'feature state above 368 B')
    # ---- 2. header, guard, hook ----
    span = candidate[off(LO):off(HDR)]
    h = candidate[off(HDR):off(HDR) + 64]
    build_id = struct.unpack('<Q', hashlib.sha256(span).digest()[:8])[0]
    crc = zlib.crc32(span) & 0xFFFFFFFF
    magic, bid, version, hcrc, entry, table, count = struct.unpack_from('<8sQIIQII', h)
    need(magic == b'Z63GATE1' and version == 1 and not any(h[40:]), 'header magic, version or reserved bytes')
    need(bid == build_id and hcrc == crc, 'header build ID or CRC does not match the patch range')
    need((entry, table) == (LO, TABLE), 'header entry or table')
    need(not any(candidate[off(HDR) + 64:off(HI)]), 'bytes after the header')
    g_ = candidate[off(V.GUARD):off(V.GUARD_END)]
    need(g_ == V.expected_guard_v2(entry, build_id), 'guard differs from the independently rebuilt guard (v2)')
    targets, loads = V.decode_guard(g_)
    need(targets <= {V.POLL, entry, V.GUARD + 0x58}, 'guard branch target outside {poll, entry, guard}')
    need({a for _, a in loads} <= {V.DETECTOR, HDR, HDR + 8, V.GUARD + 0x60, V.GUARD + 0x68, UPDATE_CLIENT},
         'guard load outside detector, header, literals, update client')
    need(word(candidate, V.HOOK) == 0x94000000 | (((V.GUARD - V.HOOK) >> 2) & 0x3FFFFFF) and word(stock, V.HOOK) == 0x9400004D, 'hook word')
    need(not any(candidate[off(V.GUARD_END):off(0x40195000)]), 'anchor not zero in the image')
    allowed = [(off(HDR), off(HDR) + 64), (off(V.GUARD), off(V.GUARD_END)), (off(V.HOOK), off(V.HOOK) + 4)]
    # ---- 4. layout ----
    n = r['code']['bytes']
    need(0 < n and LO + n <= GG.CORE_END, f'gate core ends at {LO + n:#x}, not below {GG.CORE_END:#x}')
    core = candidate[off(LO):off(LO) + n]
    need(not pinned or hashlib.sha256(core).hexdigest() == r['code']['sha256'], 'core differs from the reviewed recipe')
    need(count == len(r['install_table']['records']) and 0 < count <= GG.MAX_RECORDS and TABLE + REC * count <= TABLE_END,
         'install table count or bound')
    regions = [(LO, LO + n, 'core'), (TABLE, TABLE + REC * count, 'table')]
    cs = r['code'].get('card') or {}
    cn = cs.get('bytes', 0)
    need(GG.CARD == (CARD_LO, CARD_HI) and cs.get('address') == hex(CARD_LO) and 0 < cn <= CARD_HI - CARD_LO and LO + n <= CARD_LO,
         'card-detect arming section: address or size')
    need(not pinned or hashlib.sha256(candidate[off(CARD_LO):off(CARD_LO) + cn]).hexdigest() == cs.get('sha256'), 'card section differs from the reviewed recipe')
    regions.append((CARD_LO, CARD_LO + cn, 'card section'))
    gcode = {}
    for g in groups:
        e = r['group_code'][g.name]
        need([int(e['slot'][0], 16), int(e['slot'][1], 16)] == list(g.slot) and e['id'] == g.id, f'{g.name}: slot or id differs from its declaration')
        b = e['bytes']
        need(0 < b <= g.slot[1] - g.slot[0], f'{g.name}: code size')
        code = candidate[off(g.slot[0]):off(g.slot[0]) + b]
        need(not pinned or hashlib.sha256(code).hexdigest() == e['sha256'], f'{g.name}: code differs from the reviewed recipe')
        gcode[g.name] = code
        regions.append((g.slot[0], g.slot[0] + b, g.name))
        declared_assets = {x.lo: x for x in g.assets}
        got = {int(x['address'], 16): x for x in e.get('assets', [])}
        need(set(got) == set(declared_assets), f'{g.name}: assets differ from the declaration')
        for va, x in got.items():
            need(0 < x['bytes'] <= declared_assets[va].hi - va, f'{g.name}: asset at {va:#x} larger than declared')
            need(not pinned or hashlib.sha256(candidate[off(va):off(va) + x['bytes']]).hexdigest() == x['sha256'], f'{g.name}: asset at {va:#x} differs from the reviewed recipe')
            regions.append((va, va + x['bytes'], f'{g.name} asset {va:#x}'))
        got_d = {int(x['address'], 16): x for x in e.get('derived', [])}
        need(set(got_d) == {x.lo for x in g.derived}, f'{g.name}: derived assets differ from the declaration')
        for x in g.derived:
            blob = GG.call_builder(x.builder, bytes(stock))
            rec = got_d[x.lo]
            need(isinstance(blob, (bytes, bytearray)) and 0 < len(blob) <= x.hi - x.lo and rec['bytes'] == len(blob) and rec.get('builder') == x.builder,
                 f'{g.name}: derived asset at {x.lo:#x}: size or builder')
            need(candidate[off(x.lo):off(x.lo) + len(blob)] == bytes(blob), f'{g.name}: derived asset at {x.lo:#x} differs from its recomputation from the stock image')
            need(hashlib.sha256(blob).hexdigest() == rec['sha256'], f'{g.name}: derived asset at {x.lo:#x} differs from the recipe hash')
            regions.append((x.lo, x.lo + len(blob), f'{g.name} derived {x.lo:#x}'))
        need([x['name'] for x in e.get('clones', [])] == [c.name for c in g.clones], f'{g.name}: clones differ from the declaration')
        for c in g.clones:
            need(b <= c.at - g.slot[0], f'{g.name}: code runs into clone {c.name}')
            regions.append((c.at, c.at + c.hi - c.lo, f'{g.name} clone {c.name}'))
    regions.sort()
    need(regions[0][0] == LO, 'layout')
    at = LO
    for a, b, what in regions:
        need(a >= at, f'{what} overlaps the region before it')
        need(not any(candidate[off(at):off(a)]), f'nonzero bytes before {what} at {at:#x}')
        at = b
    need(not any(candidate[off(at):off(HDR)]), 'nonzero bytes after the last region')
    allowed += [(off(a), off(b)) for a, b, _ in regions]
    # ---- 5. records ----
    core_sym = {k: int(v['va'], 16) for k, v in r['code']['symbols'].items()}
    gsym = {g.name: {k: int(v['va'], 16) for k, v in r['group_code'][g.name]['symbols'].items()} for g in groups}
    contributors = {}
    for g in groups:
        for c in g.shared:
            h = c.hook or GG.hook_name(c.site, g.id)
            need(h == GG.hook_name(c.site, g.id), f'{g.name}: the hook of site {c.site} is not {GG.hook_name(c.site, g.id)}')
            contributors.setdefault(c.site, []).append((g.id, gsym[g.name].get(h), h))
    registrations = V.stock_registrations(stock)
    recs = [struct.unpack_from(FMT, candidate, off(TABLE) + REC * i) for i in range(count)]
    seen = {}
    thunks = {}
    helpers = set()
    sites_found = {}
    sets = {}
    expected_group = {(g.name, d.address): (g, d) for g in groups for d in g.records}
    site_words = {at: (sm, old) for sm in site_map.values() if sm.name in contributors for at, old in sm.words()}
    for i, (kind, cls, order, gm, cset, rank, wclass, flags, tx, res8, res32, address, old, new) in enumerate(recs):
        cls = chr(cls)
        where = f'record {i} at {address:#x}'
        need(order == i and not res8 and not res32 and tx <= 15, f'{where}: order, reserved bytes or TX pins')
        need(not flags & ~(7 if flags & F_PIN else 3) and (rank >= 1 if cset else rank == 0), f'{where}: flags, set or rank')
        width = {1: 8, 2: 4, 3: 8, 4: 2}.get(kind, 8)
        for a, b, what in UPDATE_DENY_LIST:
            need(address + width <= a or address >= b or (flags & F_PIN and address in UPDATE_DENY), f'{where}: touches the {what} {a:#x}-{b:#x}')
        need(address not in seen, f'{where}: a word appears twice')
        seen[address] = i
        is_code = kind == 2
        fmt = '<H' if kind == 4 else '<Q'
        if flags & F_PIN:
            pass                       # checked below (the deny records)
        elif kind == 1 and gm == 1:
            pass                       # dispatcher callback words are run-time data: checked by registration below
        else:
            stock_word = word(stock, address) if is_code else struct.unpack_from(fmt, stock, off(address))[0]
            live = word(candidate, address) if is_code else struct.unpack_from(fmt, candidate, off(address))[0]
            need(live == stock_word, f'{where}: pre-installed in the flashed image')
            need(old == stock_word, f'{where}: old value is not the stock word')
        need(bool(flags & F_PIN) == (i >= count - 2), f'{where}: the last two records, and only they, are the pinned deny records')
        if flags & F_PIN:
            target = UPDATE_DENY.get(address)
            denied = core_sym.get('z63_update_denied')
            need(target is not None and kind == 2 and cls == 'X' and gm == 1 and (cset, rank, wclass, flags, tx) == (0, 0, 5, F_PIN, 0),
                 f'{where}: a pinned record that is not an update-deny record')
            need(old == word(stock, address) == 0x14000000 | (((target - address) >> 2) & 0x3FFFFFF) and word(candidate, address) == old,
                 f'{where}: deny record old value is not the stock b {target:#x}')
            need(denied is not None and LO <= denied < LO + n and new == 0x14000000 | (((denied - address) >> 2) & 0x3FFFFFF),
                 f'{where}: deny record new value is not b z63_update_denied')
            owner = ('deny', address)
        elif kind == 3:
            need(i == 0 and cls == 'D' and address == V.ITEMS_POINTER and old == 0x43CB3868 and new == 0 and gm == 1, f'{where}: page record')
            need((cset, rank, wclass, flags, tx) == (1, 1, 0, 1, 0), f'{where}: the page record is the video-menu set, rank 1, under Q')
            owner = 'page'
        elif gm == 1:
            need((cset, rank, wclass, flags, tx) == (0, 0, 3, 0, 0), f'{where}: infrastructure records are F class, no set')
            if kind == 2:
                need(cls == 'S' and V.in_text(address) and old >> 26 == 0x25 and new >> 26 == 0x25, f'{where}: class S call site')
                for a, b, what in V.S5_DENY:
                    need(not a <= address < b, f'{where}: inside {what}')
                target = address + 4 * sx(old & 0x3FFFFFF, 26)
                need(target in V.S5_SITE_TARGETS, f'{where}: the stock call does not go to a wrapped function')
                thunk = address + 4 * sx(new & 0x3FFFFFF, 26)
                callee, index, hh = V.decode_thunk(core, LO, thunk)
                need(callee == target, f'{where}: the thunk calls {callee:#x}, not {target:#x}')
                helpers.add(hh)
                thunks[thunk] = (callee, index)
                owner = ('S', target)
            else:
                need(kind == 1 and cls == 'R' and address in V.S5_CALLBACK_WORDS and registrations.get(address) == old, f'{where}: class R callback')
                callee, index, hh = V.decode_thunk(core, LO, new)
                need(callee == old, f'{where}: the thunk calls {callee:#x}, not {old:#x}')
                helpers.add(hh)
                thunks[new] = (callee, index)
                owner = ('R', address)
        else:
            site, site_old = site_words.get(address, (None, None))
            if site is None:
                need(not any(address in dict(sm.words()) for sm in site_map.values()), f'{where}: a word of a site with no contributor in the composition')
            if site is not None:
                want = sum(1 << gid for gid, _, _ in contributors[site.name])
                need(gm == want, f'{where}: contributor mask {gm:#x}, not {want:#x}')
                need(kind == KIND[site.kind] and cls == site.cls and wclass == WC[site.wrapper] and old == site_old and not tx
                     and flags == (1 if site.needs_q else 0) and rank == site.rank, f'{where}: site {site.name} fields')
                adapter = core_sym.get(site.adapter)
                need(adapter is not None and LO <= adapter < LO + n, f'{where}: adapter {site.adapter} not in the core')
                op = 0x94000000 if site.link else 0x14000000
                need(new == (adapter if site.kind == 'data64' else op | (((adapter - address) >> 2) & 0x3FFFFFF)), f'{where}: new value is not the site adapter')
                sites_found.setdefault(site.name, []).append(address)
                owner = ('site', site.name)
                setname = site.set
            else:
                g = next((x for x in groups if gm == 1 << x.id), None)
                need(g is not None, f'{where}: group mask {gm:#x} names no single composed group')
                need((g.name, address) in expected_group, f'{where}: not a record {g.name} declares')
                _, d = expected_group.pop((g.name, address))
                need(d.old == old and kind == KIND[d.kind] and cls == d.cls and wclass == WC[d.wrapper],
                     f'{where}: kind, class, old value or wrapper class differ from the declaration')
                need(flags == (1 if d.needs_q else 0) | (2 if d.all_or_none else 0) and rank == d.rank and tx == d.tx,
                     f'{where}: flags, rank or TX pins differ from the declaration')
                if d.kind == 'data16':
                    need(new == d.new and new < 0x10000, f'{where}: new value is not the declared u16 {d.new:#x}')
                    target = None
                else:
                    target = gsym[g.name].get(d.symbol)
                    inside = target is not None and g.slot[0] <= target < g.slot[0] + len(gcode[g.name])
                    if d.kind == 'data64' and d.target == 'data':
                        inside = inside or any(int(x['address'], 16) <= (target or 0) < int(x['address'], 16) + x['bytes']
                                               for x in r['group_code'][g.name].get('assets', []))
                    need(inside, f'{where}: {d.symbol} not in the code{" or assets" if d.kind == "data64" and d.target == "data" else ""} of {g.name}')
                if d.kind == 'data64':
                    need(new == target, f'{where}: new value is not {d.symbol}')
                elif d.kind == 'data16':
                    pass
                else:
                    need(V.in_text(address), f'{where}: outside the copied text windows')
                    for a, b, what in V.S5_DENY:
                        need(not a <= address < b, f'{where}: inside {what}')
                    op = 0x94000000 if d.link else 0x14000000
                    need(new == op | (((target - address) >> 2) & 0x3FFFFFF), f'{where}: new value is not {"bl" if d.link else "b"} {d.symbol}')
                owner = ('group', g.name)
                setname = d.set if d.set == GG.VIDEO_MENU_SET else f'{g.name}.{d.set}' if d.set else ''
            if cset or setname:
                need(bool(cset) == bool(setname), f'{where}: set id {cset} for set {setname!r}')
                need((cset == 1) == (setname == GG.VIDEO_MENU_SET), f'{where}: set 1 is the video-menu set only')
                need(sets.setdefault(cset, setname) == setname, f'{where}: set id {cset} shared by two declared sets')
        for j in range(i):
            if cset and recs[j][4] == cset:
                need(recs[j][5] < rank, f'{where}: rank does not rise inside set {cset}')
                need(not (recs[j][7] ^ flags) & 2, f'{where}: set {cset} mixes all-or-none and plain members')
        if flags & 2:
            need(flags & 1 and cset and gm & (gm - 1) == 0 and gm != 1, f'{where}: all-or-none needs Q, a set and one feature group')
    need(not expected_group, f'declared records missing from the table: {sorted((n, hex(a)) for n, a in expected_group)}')
    need(sorted(x[11] for x in recs if x[7] & F_PIN) == sorted(UPDATE_DENY), 'the two update-deny records')
    denied = core_sym['z63_update_denied']
    need(struct.unpack_from('<2I', core, denied - LO) == DENY_CODE, 'z63_update_denied is not exactly mov w0, #2; ret')
    need(set(sites_found) == set(contributors), f'shared sites missing: {sorted(set(contributors) - set(sites_found))}')
    for name, at in sites_found.items():
        need(sorted(at) == sorted(a for a, _ in site_map[name].words()), f'site {name}: not every word of the site is a record')
    need(sets.get(1, GG.VIDEO_MENU_SET) == GG.VIDEO_MENU_SET, 'set 1')
    for cset in {x[4] for x in recs if x[7] & 2}:
        at = [i for i, x in enumerate(recs) if x[4] == cset]
        need(at == list(range(at[0], at[0] + len(at))) and len(at) <= 64 and len({recs[i][3] for i in at}) == 1,
             f'all-or-none set {cset}: contiguous, one group, at most 64')
    for target in V.S5_SITE_TARGETS:
        got = sorted(x[11] for x in recs if x[0] == 2 and x[3] == 1 and x[11] + 4 * sx(x[12] & 0x3FFFFFF, 26) == target)
        need(got == V.stock_calls(stock, target), f'call sites of {target:#x} are not exactly the stock callers')
    need(sorted(x[11] for x in recs if x[0] == 1 and x[3] == 1) == sorted(V.S5_CALLBACK_WORDS), 'dispatcher callback records')
    need(len(helpers) == 1, 'thunks use different helpers')
    need(len({c for c, _ in thunks.values()}) == len(thunks) == len({i for _, i in thunks.values()}), 'thunk index or callee reused')
    # ---- 6. code rules ----
    # The core and its card-detect arming section as one code range (the gap between is zero).
    n_core = n
    core = candidate[off(LO):off(CARD_LO) + cn]
    n = len(core)
    site_calls = {}
    for name in contributors:
        site_calls.update({v: k for k, v in site_map[name].stock_calls.items()})
    core_calls = {**V.S5_CALLS, **site_calls}
    constants, calls = _code_rules(core, LO, 'core')
    for i in range(0, n - 3, 4):
        w = struct.unpack_from('<I', core, i)[0]
        if w & 0xFFFFFC1F == 0xD63F0000:
            a, b = struct.unpack_from('<2I', core, i - 8) if i >= 8 else (0, 0)
            need(w == 0xD63F0200 and a & 0xFFE0001F == 0xD2800010 and b & 0xFFE0001F == 0xF2A00010, f'core: indirect call without its address at {LO + i:#x}')
            need(((a >> 5) & 0xFFFF) | (((b >> 5) & 0xFFFF) << 16) in core_calls, f'core: indirect call outside the allowlist at {LO + i:#x}')
    hooks = {va for m in contributors.values() for _, va, _ in m}
    restore_cbs = {g.name: gsym[g.name].get(GG.restore_symbol(g.id)) for g in groups if g.restore}
    for name, va in restore_cbs.items():
        need(va is not None, f'{name}: restore callback missing')
    hooks |= set(restore_cbs.values())
    publish_cbs = {g.name: gsym[g.name].get(GG.publish_symbol(g.id)) for g in groups if g.publish}
    for name, va in publish_cbs.items():
        need(va is not None, f'{name}: publication callback missing')
    hooks |= set(publish_cbs.values())
    break_cbs = {g.name: gsym[g.name].get(GG.break_symbol(g.id)) for g in groups if g.restore_now}
    for name, va in break_cbs.items():
        need(va is not None, f'{name}: break callback missing')
    hooks |= set(break_cbs.values())
    caps = {g.name: gsym[g.name].get(g.pass_cap) for g in groups if g.pass_cap}
    for name, va in caps.items():
        need(va is not None, f'{name}: pass cap missing')
    hooks |= set(caps.values())
    external = {t for _, t in calls if t is None or not LO <= t < LO + n}
    need(external <= set(core_calls) | hooks, f'core calls outside the allowlist: {sorted(hex(t) if t else str(t) for t in external - set(core_calls) - hooks)}')
    # Calls that stand in for the call a record replaces: the thunks' callees, the poll, each
    # composed site's stock target (the data word's old value, or the callee / B target of its
    # code words).
    passthrough = {c for c, _ in thunks.values()} | {V.POLL}
    for nm in contributors:
        sm = site_map[nm]
        for at, old in sm.words():
            passthrough.add(old if sm.kind == 'data64' else at + 4 * sx(old & 0x3FFFFFF, 26))
    head = r['code'].get('build_header', '')
    for nm in contributors:
        sm = site_map[nm]
        present = {gid: h for gid, _, h in contributors[nm]}
        joins = {g.id: next(c.role for c in g.shared if c.site == nm) for g in groups if any(c.site == nm for c in g.shared)}
        for role in sm.roles():
            members = [(gid, present[gid]) for ro, gid in sm.order if ro == role and gid in present]
            members += [(gid, present[gid]) for gid in sorted(present)
                        if not any(gid == x for _, x in sm.order) and (joins[gid] or sm.roles()[0]) == role]
            line = f'#define Z63_SITE_{nm.upper()}_{role.upper()}(X) ' + ' '.join(f'X({gid},{h})' for gid, h in members)
            need(line in head.split('\n'), f'site {nm}: role {role} is not generated in the site order')
    cbs = [g for g in groups if g.restore]
    if cbs:
        need(f'#define Z63_RESTORE_COUNT {len(cbs)}u' in head.split('\n') and
             '#define Z63_RESTORE_CBS(X) ' + ' '.join(f'X({g.id},{GG.restore_symbol(g.id)})' for g in cbs) in head.split('\n'),
             'the core restore callbacks differ from the declarations')
    else:
        need('Z63_RESTORE_COUNT' not in head and 'Z63_RESTORE_CBS' not in head, 'restore callbacks in a composition that declares none')
    # Publication callbacks and anchor holds, exactly as declared (none: no line).
    d26 = [ln for ln in head.split('\n') if ln.startswith(('#define Z63_PUBCB', '#define Z63_HOLD'))]
    pubs, held = [g for g in groups if g.publish], [g for g in groups if g.hold]
    want = ([f'#define Z63_PUBCB_COUNT {len(pubs)}u', '#define Z63_PUBCBS(X) ' + ' '.join(f'X({g.id},{GG.publish_symbol(g.id)})' for g in pubs)] if pubs else []) + \
           ([f'#define Z63_HOLD_COUNT {len(held)}u', '#define Z63_HOLDS(X) ' + ' '.join(f'X({g.id},{g.hold:#x}u)' for g in held)] if held else [])
    need(d26 == want, 'the core publication callbacks or anchor holds differ from the declarations')
    # R-G10-0/1/2/4, exactly as declared (none: no line).
    g10 = [ln for ln in head.split('\n') if ln.startswith(('#define Z63_ENTRY', '#define Z63_BREAKCB', '#define Z63_RAISE', '#define Z63_NRAW_BUDGET_CAP'))]
    nows = [g for g in groups if g.restore_now]
    want = (['#define Z63_ENTRY 1'] if any(g.id and not g.movie_publish for g in groups) else []) + \
           ([f'#define Z63_BREAKCB_COUNT {len(nows)}u', '#define Z63_BREAKCBS(X) ' + ' '.join(f'X({g.id},{GG.break_symbol(g.id)})' for g in nows)] if nows else []) + \
           (['#define Z63_RAISE 1'] if any(g.raises for g in groups) else []) + \
           [f'#define Z63_NRAW_BUDGET_CAP {g.pass_cap}' for g in groups if g.pass_cap]
    need(g10 == want, 'the core movie-entry word, break callbacks, raise or pass cap differ from the declarations')
    # Movie-view arming, recomputed here from the declarations (not from gate_groups' helpers):
    # a group installs at a QM arming only when every code record replacing a non-branch word has
    # its evidence; qm_install True is refused otherwise; the never-deferred groups (rows, the
    # video-menu set); the pipeline classes only from the verifier's own list; the i-menu switch
    # also an install term of G3.
    def qm_ok(g):
        nb = {x.address for x in g.records if x.kind == 'code32' and x.old >> 26 not in (0x05, 0x25)}
        ev = {e.address for e in g.qm_evidence}
        need(ev <= nb and all(e.evidence.strip() for e in g.qm_evidence), f'{g.name}: QM evidence for a word that is not a non-branch code record')
        need(any(g.qm_install is v for v in (None, True, False)) and (g.qm_install is not True or nb <= ev), f'{g.name}: qm_install True with a non-branch code record without evidence')
        return g.qm_install is not False and nb <= ev
    need(set(GG.QM_CLASSES) <= QM_CLASSES_VERIFIED, 'a QM pipeline class the verifier does not accept')
    # The GUI terms (i-menu closed by app id and hold bit; shooting screen): the declared object and
    # fields are the verifier's, and the stock words that pin them read as recorded.
    need((GG.GUI_OBJECT, GG.GUI_SELF, GG.GUI_HOLD, GG.GUI_APP) == tuple(GUI_VERIFIED[k] for k in ('object', 'self', 'hold', 'app')),
         'the GUI object or its fields differ from the verifier\'s')
    gui = hashlib.sha256(b'z63-gui-stock-v1\0')
    for va in GUI_SITES_VERIFIED:
        gui.update(struct.pack('<QI', va, 4) + bytes(stock[off(va):off(va) + 4]))
    need(gui.hexdigest() == GUI_DIGEST_VERIFIED, 'a stock GUI word changed (verifier digest)')
    imenu = {(t.address, t.width, t.mask, t.value, t.equal) for g in groups if g.id == 3 for t in g.terms}
    need(not GG.IMENU_CLOSED or 3 not in ids or (GUI_VERIFIED['hold'], 4, 4, 0, True) in imenu,
         'the i-menu switch is on but G3 lacks its install term (i-menu hold bit clear)')
    need(not any(t.address == 0xA0348863C for g in groups for t in g.terms), 'a term on the i-menu entry count (not an open flag)')
    want = [f'#define Z63_QM_OK {1 | sum(1 << g.id for g in groups if qm_ok(g)):#x}u',
            f'#define Z63_NODEFER {sum(1 << g.id for g in groups if g.rows or any(x.set == "video_menu" for x in g.records)):#x}u',
            f'#define Z63_QM_CLASSES {sum(1 << c for c in GG.QM_CLASSES):#x}u', f'#define Z63_QM_IMENU {1 if GG.IMENU_CLOSED else 0}',
            f'#define Z63_QM_SCREEN {1 if GG.SHOOTING_SCREEN else 0}']
    need([ln for ln in head.split('\n') if ln.startswith(('#define Z63_QM_', '#define Z63_NODEFER'))] == want,
         'the core movie-view arming lines differ from the declarations')
    for g in groups:
        need(not g.hold or (g.state and g.hold % 4 == 0 and g.state[0] <= g.hold and g.hold + 4 <= g.state[0] + g.state[1]),
             f'{g.name}: the anchor hold is not a word of its own state')
    after = {g.id: sum(1 << d for d in g.restore_after if d in ids) for g in groups}
    if any(after.values()):
        need(all(d > g.id for g in groups for d in g.restore_after), 'restore_after names a group restored after the reader')
        need('#define Z63_AFTER {' + ','.join(f'{after.get(i, 0):#x}' for i in range(16)) + '}' in head.split('\n'),
             'the core restore order (Z63_AFTER) differs from the declarations')
    else:
        need('Z63_AFTER' not in head, 'a restore order in a composition that declares none')
    core_targets = {t for t in external if t is not None and t not in hooks}
    flash = {'core': _flash_rules(stock, core, LO, 'core', core_targets, core_targets - passthrough)}
    rules = {}
    enter, leave = next(iter(helpers))
    for name in CORE_API:
        va = core_sym.get(name)
        need(va is not None and LO <= va < LO + n, f'{name} missing')
        rules[name] = _wrapper(core, LO, va, set(), name, False, name in ('z63_enter', 'z63_admit'))
    need(core_sym['z63_enter'] == enter and core_sym['z63_leave'] == leave, 'thunk helpers are not z63_enter/z63_leave')
    for t, (c, _) in thunks.items():
        rules[f'thunk {t:#x}'] = _wrapper(core, LO, t, {c}, f'thunk {t:#x}', True, False)
    # Nothing a wrapper, thunk, the API, a site adapter or the deny routine reaches lies in the
    # card-detect arming section.
    entries = [(nm, core_sym[nm]) for nm in CORE_API] + [(f'thunk {t:#x}', t) for t in thunks] + \
              [(site_map[nm].adapter, core_sym[site_map[nm].adapter]) for nm in contributors] + [('z63_update_denied', denied)]
    for label, va in entries:
        need(va < CARD_LO and all(pc < CARD_LO for pcs, _, _ in V._walk_wrapper(core, LO, va)[0].values() for pc in pcs),
             f'{label}: reaches the card-detect arming section')
    for name, members in contributors.items():
        s = site_map[name]
        allowed_calls = set(s.stock_calls.values()) | {va for _, va, _ in members}
        if name == 'nraw_budget':
            allowed_calls |= set(caps.values())          # R-G10-2: the pass cap (its own rules below)
        rules[s.adapter] = _wrapper(core, LO, core_sym[s.adapter], allowed_calls, s.adapter, False, True)
    group_rules = {}
    text_unresolved = {}
    for g in groups:
        code = gcode[g.name]
        base = g.slot[0]
        _, gcalls = _code_rules(code, base, g.name)
        for i in range(0, len(code) - 3, 4):
            w = struct.unpack_from('<I', code, i)[0]
            need(w & 0xFFFFFC1F not in (0xD63F0000, 0xD61F0000), f'{g.name}: indirect branch at {base + i:#x}')
        clone_entry = {c.at for c in g.clones}
        gallowed = set(g.stock_calls.values()) | clone_entry
        gext = {t for _, t in gcalls if t is None or not base <= t < base + len(code)}
        for t in sorted(t for t in gext if t is not None):          # the flash rule first (its own message)
            need(not is_flash(stock, t), f'{g.name}: calls a flash routine at {t:#x}')
        need(gext <= gallowed, f'{g.name}: calls {sorted(hex(t) if t else str(t) for t in gext - gallowed)} outside its declared stock calls')
        # Every entry into the group's code, with the tail branches it may take: a code record's
        # replaced target (B/BL old word) or its continuation (address + 4 after the displaced
        # word), a data record's old function, a B clone patch's return into the clone.
        entries = []      # (label, va, tails, displaced, stores, first access, replaced target)
        for d in g.records:
            if d.kind == 'data16' or (d.kind == 'data64' and d.target == 'data'):
                continue
            va = gsym[g.name][d.symbol]
            if d.kind == 'data64':
                rt = d.old
                tails, displaced = {d.old}, {}
            elif d.old >> 26 in (0x05, 0x25):
                rt = d.address + 4 * sx(d.old & 0x3FFFFFF, 26)
                tails, displaced = {rt}, {}
            else:
                rt = d.address + 4 if d.continuation else None     # the rest of the stock function the word sits in
                tails = {d.address + 4} if d.continuation else set()
                displaced = {d.address + 4: d.old} if d.continuation else {}
            entries.append((f'{g.name}.{d.symbol}@{d.address:#x}', va, tails, displaced, d.stores, d.lookup, rt))
        for e in g.entries:
            entries.append((f'{g.name}.{e.symbol}', gsym[g.name][e.symbol], set(), {}, e.stores, e.lookup, None))
        for c in g.shared:
            h = c.hook or GG.hook_name(c.site, g.id)
            entries.append((f'{g.name}.{h}', gsym[g.name][h], set(), {}, g.hook_stores, False, None))
        for c in g.clones:
            for pt in c.patches:
                tails = set() if pt.link else {c.at + pt.offset + 4}
                entries.append((f'{g.name}.{pt.symbol}@clone {c.name}+{pt.offset:#x}', gsym[g.name][pt.symbol], tails, {}, True, False, None))
        if g.restore:
            # Restore callback: card-detect task, no first access rule (it never reads the gate),
            # no external call of any kind, bounded, may store (its run-time words).
            va = gsym[g.name][GG.restore_symbol(g.id)]
            rule, pcs, ext = _group_wrapper(code, base, va, set(), set(), f'{g.name}.{GG.restore_symbol(g.id)}', True, False)
            need(not ext, f'{g.name}: the restore callback calls or branches out of the group ({sorted(hex(x) for x in ext)})')
            group_rules[f'{g.name}.{GG.restore_symbol(g.id)}'] = rule
            entries.append((f'{g.name}.{GG.restore_symbol(g.id)}', va, set(), {}, True, False, None))
        if g.restore_now:
            # Break callback (R-G10-1): the same rules as the restore callback.
            va = gsym[g.name][GG.break_symbol(g.id)]
            rule, pcs, ext = _group_wrapper(code, base, va, set(), set(), f'{g.name}.{GG.break_symbol(g.id)}', True, False)
            need(not ext, f'{g.name}: the break callback calls or branches out of the group ({sorted(hex(x) for x in ext)})')
            group_rules[f'{g.name}.{GG.break_symbol(g.id)}'] = rule
            entries.append((f'{g.name}.{GG.break_symbol(g.id)}', va, set(), {}, True, False, None))
        if g.pass_cap:
            need(g.pass_cap in PASS_CAP_TABLES and (GG.PASS_CAPS.get(g.id) == g.pass_cap or g.test_only),
                 f'{g.name}: pass cap {g.pass_cap} is not the declared one')
            va = gsym[g.name][g.pass_cap]
            group_rules[f'{g.name}.{g.pass_cap}'] = _pass_cap(code, base, va, f'{g.name}.{g.pass_cap}', PASS_CAP_TABLES[g.pass_cap])
            entries.append((f'{g.name}.{g.pass_cap}', va, set(), {}, True, False, None))
        if g.publish:
            # Publication callback: the same rules as the restore callback.
            va = gsym[g.name][GG.publish_symbol(g.id)]
            rule, pcs, ext = _group_wrapper(code, base, va, set(), set(), f'{g.name}.{GG.publish_symbol(g.id)}', True, False)
            need(not ext, f'{g.name}: the publication callback calls or branches out of the group ({sorted(hex(x) for x in ext)})')
            group_rules[f'{g.name}.{GG.publish_symbol(g.id)}'] = rule
            entries.append((f'{g.name}.{GG.publish_symbol(g.id)}', va, set(), {}, True, False, None))
        reached = {}      # label -> (pcs, replaced target)
        stock_targets = set()
        for label, va, tails, displaced, stores, first, rt in entries:
            rule, pcs, ext = _group_wrapper(code, base, va, gallowed, tails, label, stores, first, displaced)
            group_rules[label] = rule
            reached[label] = (pcs, rt)
            stock_targets |= {t for t in ext if t not in clone_entry}
        # Clones: byte rule, and their stock targets count as calls on the group's own account.
        clone_ext = set()
        for c in g.clones:
            clone_ext |= _clone_rules(stock, candidate, g, c, gsym[g.name])
        save = {va for va in g.save_calls.values() if GG.SAVE_ENTRIES.get(va)}
        targets = {t for t in gext if t is not None and t not in clone_entry} | stock_targets | clone_ext
        flash[g.name] = _flash_rules(stock, code, base, g.name, targets, set(), save)
        # Direct branches out of the code that no entry reaches are refused (dead tails), after the flash rules.
        reach_all = set().union(*(p for p, _ in reached.values())) if reached else set()
        for pc, kind, t in _direct_branches(code, base):
            if kind == 'b' and not base <= t < base + len(code):
                need(pc in reach_all, f'{g.name}: branch out of the code at {pc:#x} not reached from any entry')
        # Structural carve-out: a stock function that can reach a flash
        # routine may be called (or tail-branched to) only by the wrapper of a record whose stock
        # word called it, and only from code no other entry reaches. Everything else must be clean.
        sites_of = {}
        for pc, kind, t in _direct_branches(code, base):
            if kind in ('bl', 'b') and not base <= t < base + len(code):
                sites_of.setdefault(t, []).append(pc)
        for t in sorted(targets):
            if flash[g.name][hex(t)]['reach'] == 'clean' or t in save:
                continue
            need(t not in clone_ext, f'{g.name}: a clone calls {t:#x}, which can reach a flash routine')
            owners = [lab for lab, (_, rt) in reached.items() if rt == t]
            need(owners, f'{g.name}: stock call {t:#x} can reach a flash routine ({flash[g.name][hex(t)]["reach"]} {flash[g.name][hex(t)]["via"]}) and replaces no record of the group')
            for pc in sites_of.get(t, ()):
                by = [lab for lab, (pcs, _) in reached.items() if pc in pcs]
                need(by and all(reached[lab][1] == t for lab in by),
                     f'{g.name}: {t:#x} (reaches a flash routine) is called at {pc:#x} from {sorted(set(by) - set(owners)) or "no entry"}, not only by the wrapper of the record it replaces')
            flash[g.name][hex(t)]['carve_out'] = owners
        # Text slots and assets.
        text_unresolved[g.name] = _text_slots(code, base, g.name, set(g.text_slots))
        for x in r['group_code'][g.name].get('assets', []) + r['group_code'][g.name].get('derived', []):
            va = int(x['address'], 16)
            blob = candidate[off(va):off(va) + x['bytes']]
            lits = {struct.unpack_from('<Q', blob, i)[0] for i in range(0, len(blob) - 7, 4)} | {struct.unpack_from('<I', blob, i)[0] for i in range(0, len(blob) - 3, 4)}
            named = sorted(hex(c) for c in lits if is_flash(stock, c))
            need(not named, f'{g.name}: asset at {va:#x} holds the address of a flash routine {named}')
        # Rows template and sanitiser: the core's generated header matches the declarations.
        if g.rows:
            sym, nrows = g.rows   # not `count`: that is the record count reported below
            va = gsym[g.name].get(sym)
            spans = [(base, base + len(code))] + [(int(x['address'], 16), int(x['address'], 16) + x['bytes']) for x in r['group_code'][g.name].get('assets', [])]
            need(va is not None and any(a <= va and va + 44 * nrows <= b for a, b in spans), f'{g.name}: rows template outside its code and assets')
            need(f'#define Z63_ROWS_TEMPLATE {va:#x}ull' in head and f'#define Z63_ROWS_COUNT {nrows}u' in head and f'#define Z63_ROWS_GROUP {g.id}u' in head,
                 f'{g.name}: the core does not copy the declared rows template')
    want_sans = []
    for g in groups:
        k0 = len(want_sans)
        for t in g.sanitise:
            when = sum(1 << (k0 + i) for i in t.when)
            want_sans.append(f'{{{t.address:#x}ull,{t.match_lo:#x}ull,{t.match_hi:#x}ull,{t.stock:#x}ull,{when:#x}u,{g.id},{t.width},{{0,0}}}}')
    if want_sans:
        need(f'#define Z63_SANS {{{",".join(want_sans)}}}' in head, 'the core sanitiser differs from the declarations')
    else:
        need('Z63_SAN_COUNT' not in head, 'a sanitiser in a composition that declares none')
    if not any(g.rows for g in groups):
        need('Z63_ROWS_COUNT' not in head, 'a rows template in a composition that declares none')
    card = {}
    for name in ('z63_entry', 'z63_armed'):
        # The card-detect paths: control stays in the core (no tail branch out), calls only to
        # the allowed stock functions and the composed hooks, bounded loops.
        funcs, ext, probs, bound = V._walk_wrapper(core, LO, core_sym[name])
        need(not probs, f'{name}: {probs}')
        # (and the composed groups' restore callbacks, which the card-detect task calls in a restore pass)
        cb = set(restore_cbs.values()) | set(publish_cbs.values()) | set(break_cbs.values())
        need(ext <= set(core_calls) | cb, f'{name}: calls {sorted(hex(x) for x in ext - set(core_calls) - cb)} outside the allowlist')
        need(bound <= CARD_STACK, f'{name}: card-detect stack bound {bound} above {CARD_STACK}')
        card[name] = bound
    # ---- 12. stock digest ----
    # The recipe carries no stock value, only one digest over every stock value it relies on. It is
    # recomputed here, independently of the builder, from this stock image: each record's restore
    # value as the table holds it (each already checked above against stock, naming its address),
    # the stock bytes under every recipe edit, and the declared sanitiser stock values. A mismatch
    # cannot name a site (one digest, by design: a per-site hash of a 4-byte word is reversible).
    need(not pinned or isinstance(r.get('stock_digest'), str), 'the recipe has no stock digest (older format)')
    if 'stock_digest' in r:
        dg = hashlib.sha256(b'z63-stock-digest-v1\0')
        for (kind, _c, _o, _g, _s, _r, _w, _f, _t, _r8, _r32, address, old, _n) in recs:
            w = {1: 8, 2: 4, 3: 8, 4: 2}[kind]
            dg.update(struct.pack('<QB', address, w) + (old & ((1 << 8 * w) - 1)).to_bytes(w, 'little'))
        for e in sorted(r['edits'], key=lambda e: e['offset']):
            need(e['offset'] == off(int(e['address'], 16)), f'recipe edit at {e["address"]}: offset')
            dg.update(struct.pack('<QI', int(e['address'], 16), e['bytes']) + bytes(stock[e['offset']:e['offset'] + e['bytes']]))
        for g in sorted(groups, key=lambda g: g.id):
            for t in g.sanitise:
                dg.update(struct.pack('<QBQ', t.address, t.width, t.stock))
        need(dg.hexdigest() == r['stock_digest'], 'stock digest: the recipe and this stock image disagree on the stock values '
             '(records, edited regions or sanitiser values; the per-record checks above passed)')
    # ---- 1. changed regions, recipe edits ----
    runs = V.diff_runs(stock, candidate)
    flashed = [(off(V.GUARD), off(V.GUARD_END)), (off(V.HOOK), off(V.HOOK) + 4), (off(LO), off(HI))]
    for a, b in runs:
        need(any(x <= a and b <= y for x, y in flashed), f'changed bytes outside the flashed set (guard, hook word, patch range) at file offset {a:#x}')
        need(any(x <= a and b <= y for x, y in allowed), f'changed bytes outside the declared regions at file offset {a:#x}')
    for e in (r['edits'] if pinned else ()):
        at = int(e['address'], 16)
        need(hashlib.sha256(candidate[off(at):off(at) + e['bytes']]).hexdigest() == e['after_sha256'], f'recipe edit differs at {e["address"]}')
    out.update(composition=[g.name for g in groups], groups=mask, core=dict(bytes=n_core, end=hex(LO + n_core), limit=hex(GG.CORE_END),
                                                                 card=dict(bytes=cn, end=hex(CARD_LO + cn), limit=hex(CARD_HI))),
               group_code={g.name: dict(slot=[hex(g.slot[0]), hex(g.slot[1])], bytes=len(gcode[g.name])) for g in groups},
               install_table=dict(address=hex(TABLE), records=count, end=hex(TABLE + REC * count), bound=hex(TABLE_END)),
               sets={str(k): v for k, v in sorted(sets.items())}, wrapper_rules=rules, group_rules=group_rules,
               card_detect_stack_bound=card, flash=flash, text_slots_unresolved=text_unresolved,
               header=dict(build_id=f'{build_id:016x}', crc32=f'{crc:08x}', entry=hex(entry), table=hex(table), records=count),
               changed_runs=len(runs), changed_bytes=sum(b - a for a, b in runs), stock_digest=r.get('stock_digest'), accepted=True)
    return out
