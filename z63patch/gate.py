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
"""Composed arm-time gate builds (stages S7 on): the gate core plus any subset of feature groups.

Separate from z63patch/arm_time.py, whose S1-S6 builds stay byte-identical (native/arm_time/s6.c
is frozen). A composed build is

  - the gate core native/arm_time/gate.c at 0x401A0000 (must end below 0x401A5000; its
    card-detect arming section .text.z63card at 0x401A5000, below 0x401A6000), compiled
    with a generated gate_build.h (group mask, record count, dependencies, quiescence terms,
    shared-site contributor lists) and gate_sites.inc (the merged adapters of the shared sites
    the composition uses), linked with the contributors' hook addresses as externals;
  - each group's one translation unit, compiled and linked alone at its fixed slot with its
    declared stock calls as externals, so its bytes do not depend on the composition;
  - install table v2 at 0x401F6000 (40-byte records), the 64-byte header, the S6 guard and hook
    word (the guard, anchor and hook are unchanged from S6).

Group declarations: z63patch/gate_groups (README.md there). Independent checks:
z63patch/verify_gate.py. Every image is DO_NOT_FLASH; nothing here writes a container.
"""
import hashlib
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path

from .image import Image, require, MAIN_SHA256
from .gate_linker import link
from . import arm_time as A
from . import gate_groups as GG

ROOT = Path(__file__).resolve().parents[1]
# Stage name -> group names (besides the infrastructure, always present), from the STAGES of the
# group modules (z63patch/gate_groups/<name>.py), so a new stage edits no shared file.
COMPOSITIONS = GG.compositions()
STAGES = tuple(COMPOSITIONS)
CORE_SYMBOLS = ('z63_entry', 'z63_armed', 'z63_enter', 'z63_leave', 'z63_admit', 'z63_exit', 'z63_effective',
                'z63_capture_dispatch', 'z63_movie_dispatch', 'z63_pipeline_drain', 'z63_pipeline_configure', 'z63_update_denied')
# The two pinned deny records, the last two
# records of every composed table (record flag F_PIN). Each replaces a pinned stock branch with
# `b z63_update_denied`, so no firmware update can start; installed first, never restored.
# (address, stock B target, purpose)
F_PIN = 4
UPDATE_DENY = ((0x410143EC, 0x41014ED4, 'update deny 1'),
               (0x4101442C, 0x4101557C, 'update deny 2'))
RECORD = A.S6_RECORD
CORE_DEFINES = ['-Os', '-mno-outline']    # the core only (groups keep -O2); gate.c gives its card-detect half minsize; no machine outliner
RECORD_BYTES = A.S6_RECORD_BYTES
KINDS = {'data64': A.K_DATA, 'code32': A.K_CODE, 'page64': A.K_PAGE, 'data16': 4}
PAGE_MAP = {'0x000': 'video menu rows copy (45 x 44 now, room for 48 x 44)',
            '0x840': 'feature state (368 B; fixed per-group offsets declared in z63patch/gate_groups)',
            '0x9b0': 'counters (64 B, magic Z6P6, version 7; +28 groups selected at arming)',
            '0x9f0': 'reserved: text-slot originals (to 0xC38); +0xC38 stage-2 settle (count, groups), +0xC3C groups deferred to stage 2 (movie-view arming)', '0xc40': 'core words (16 B; gate_api.h): publication callbacks pending and anchor hold, movie-entry word, raised breaks',
            '0xc60': 'record statuses (1 B each, 800)',
            '0xf80': 'gate block (128 B; +0x6C groups requested, +0x70 groups dropped at arming)'}


def resolve(names, extra=()):
    """(groups of the composition in id order, every declared group, sites). Refuses an unknown
    group, a composition without a group's dependencies, or declarations that break the rules."""
    declared = GG.compose(names, dict(GG.discover()))
    for g in extra:
        require(g.name not in declared, f'group {g.name} declared twice')
        declared[g.name] = g
    site_map = GG.shared_sites()
    GG.check(list(declared.values()), site_map)
    for n in names:
        require(n in declared, f'unknown group {n}')
    groups = sorted((declared[n] for n in set(names)), key=lambda g: g.id)
    ids = {g.id for g in groups}
    for g in groups:
        missing = [d for d in g.requires if d not in ids]
        require(not missing, f'{g.name} requires groups {missing} not in the composition')
        require(not g.test_only or extra, f'{g.name} is a test-only group')
    used = 0
    for g in groups:
        if g.state:
            used += g.state[1]
    require(used <= GG.FEATURE_STATE[1] - GG.FEATURE_STATE[0], 'feature state above 368 bytes')
    require(len([g for g in groups if g.rows]) <= 1, 'more than one composed group declares the video-menu rows')
    check_state_header()
    return groups, declared, site_map


def check_state_header():
    """gate_api.h's Z63_STATE_G<n> / Z63_STATE_BYTES_G<n> equal the registry PLAN_STATE."""
    text = (ROOT / 'native/arm_time/gate_api.h').read_text()
    offs = {int(g): int(v, 16) for g, v in re.findall(r'#define Z63_STATE_G(\d+)\s+(0x[0-9a-f]+)u', text)}
    size = {int(g): int(v) for g, v in re.findall(r'#define Z63_STATE_BYTES_G(\d+)\s+(\d+)u', text)}
    require({g: (offs[g], size.get(g)) for g in offs} == GG.PLAN_STATE, 'gate_api.h state offsets differ from PLAN_STATE')


def _compile(compiler, source, defines, includes, out):
    run = subprocess.run([compiler, *A.FLAGS, '-ffixed-x18', *defines, *[f'-I{i}' for i in includes],
                          '-c', str(source), '-o', str(out)], capture_output=True, text=True)
    require(run.returncode == 0, run.stderr)
    return out.read_bytes()


def continuation_externals(g):
    """z63_continue_<address hex> = address + 4 for each continuation record (where the adapter
    returns after the displaced word)."""
    return {f'z63_continue_{r.address:x}': r.address + 4 for r in g.records if r.kind == 'code32' and r.continuation}


def clone_externals(g):
    """The externs a group links against for its clones: z63_clone_<name> (the clone entry) and,
    for each B patch, z63_clone_<name>_<offset+4 hex> (where the group returns to)."""
    out = {}
    for c in g.clones:
        out[f'z63_clone_{c.name}'] = c.at
        for pt in c.patches:
            if not pt.link:
                out[f'z63_clone_{c.name}_{pt.offset + 4:x}'] = c.at + pt.offset + 4
    return out


def derived_externals(g):
    """z63_derived_<lo hex> = lo for each build-time derived asset of the group."""
    return {GG.derived_symbol(x.lo): x.lo for x in g.derived}


def link_group(g, compiler='clang', assets=None, stock=None):
    """(code bytes, sections, symbols, relocations) of one group at its slot; its asset sections
    go into `assets` ({address: bytes}). A group with a stock_header is compiled with the header
    its builder makes from `stock` (the user's MAIN), written only to the build's temporary
    directory and force-included; it never reaches the repository or a recipe."""
    with tempfile.TemporaryDirectory(prefix='z63-gate-') as t:
        defines = ['-fno-optimize-sibling-calls', *g.defines]
        if g.id and not g.movie_publish:
            defines.append('-DZ63_EFFECTIVE_ENTRY')    # R-G10-0: its reads take the movie-entry word
        if g.stock_header:
            require(stock is not None, f'{g.name}: its stock header needs the stock image')
            head = Path(t) / 'stock_header.h'
            head.write_text(GG.call_builder(g.stock_header, bytes(stock)))
            defines += ['-include', str(head)]
        raw = _compile(compiler, ROOT / g.source, defines, [], Path(t) / 'g.o')
    for name, va in g.stock_calls.items():
        require(A.PATCH_LO > va or va >= A.PATCH_HI, f'{g.name}: stock call {name} inside the patch range')
    placed = {}
    end = min([c.at for c in g.clones] + [g.slot[1]])
    code, sections, symbols, relocations = link(raw, base=g.slot[0], end=end, externals={**g.stock_calls, **clone_externals(g), **continuation_externals(g),
                                                                                          **derived_externals(g)},
                                                assets=placed)
    declared = {x.lo: x for x in g.assets}
    for va, data in placed.items():
        require(va in declared and len(data) <= declared[va].hi - declared[va].lo, f'{g.name}: asset section at {va:#x} is not a declared asset or too large')
    require(set(placed) == set(declared), f'{g.name}: declared assets without a section: {sorted(hex(x) for x in set(declared) - set(placed))}')
    if assets is not None:
        assets.update({va: bytes(d) for va, d in placed.items()})
    for r in g.records:
        require(r.symbol is None or r.symbol in symbols, f'{g.name}: adapter {r.symbol} not defined')
    for e in g.entries:
        require(e.symbol in symbols, f'{g.name}: entry {e.symbol} not defined')
    for c in g.shared:
        require(hook(g, c) in symbols, f'{g.name}: hook {hook(g, c)} not defined')
    for c in g.clones:
        for pt in c.patches:
            require(pt.symbol in symbols, f'{g.name}: clone patch adapter {pt.symbol} not defined')
    if g.rows:
        require(g.rows[0] in symbols, f'{g.name}: rows template {g.rows[0]} not defined')
    if g.restore:
        require(GG.restore_symbol(g.id) in symbols, f'{g.name}: restore callback {GG.restore_symbol(g.id)} not defined')
    if g.publish:
        require(GG.publish_symbol(g.id) in symbols, f'{g.name}: publication callback {GG.publish_symbol(g.id)} not defined')
    if g.restore_now:
        require(GG.break_symbol(g.id) in symbols, f'{g.name}: break callback {GG.break_symbol(g.id)} not defined')
    if g.pass_cap:
        require(g.pass_cap in symbols, f'{g.name}: pass cap {g.pass_cap} not defined')
    return code, sections, symbols, relocations


def build_derived(original, g):
    """[(address, bytes, builder)] of the group's derived assets, made from the user's MAIN."""
    out = []
    for x in g.derived:
        data = GG.call_builder(x.builder, bytes(original))
        require(isinstance(data, (bytes, bytearray)) and 0 < len(data) <= x.hi - x.lo, f'{g.name}: derived asset at {x.lo:#x} is empty or larger than declared')
        out.append((x.lo, bytes(data), x.builder))
    return out


def restore_order(groups):
    """{group id: mask of the composed groups it is restored after} (restore_after, composed only)."""
    ids = {g.id for g in groups}
    return {g.id: sum(1 << d for d in g.restore_after if d in ids) for g in groups if any(d in ids for d in g.restore_after)}


def hook(g, c):
    return c.hook or GG.hook_name(c.site, g.id)


def _sx(v, bits):
    return v - (1 << bits) if v >> (bits - 1) & 1 else v


def relocate(word, old_pc, new_pc, lo, hi):
    """The word moved from old_pc to new_pc keeping every absolute target outside [lo, hi) and
    every relative target inside it: (new word, kind, absolute target or None)."""
    def fix(target, bits, shift, place, mask):
        if lo <= target < hi:
            return word, target
        d = (target - new_pc) >> shift
        require((target - new_pc) % (1 << shift) == 0 and -(1 << (bits - 1)) <= d < 1 << (bits - 1), f'clone relocation out of range at {old_pc:#x}')
        return (word & ~mask) | ((d & ((1 << bits) - 1)) << place), target
    if word & 0x7C000000 == 0x14000000:                    # b, bl
        t = old_pc + 4 * _sx(word & 0x3FFFFFF, 26)
        w, t = fix(t, 26, 2, 0, 0x3FFFFFF)
        return w, 'branch', t
    if word & 0xFF000010 == 0x54000000 or word & 0x7E000000 == 0x34000000 or word & 0x3B000000 == 0x18000000:
        t = old_pc + 4 * _sx((word >> 5) & 0x7FFFF, 19)  # b.cond, cbz/cbnz, ldr literal
        w, t = fix(t, 19, 2, 5, 0x7FFFF << 5)
        return w, 'literal' if word & 0x3B000000 == 0x18000000 else 'branch', t
    if word & 0x7E000000 == 0x36000000:                    # tbz/tbnz
        t = old_pc + 4 * _sx((word >> 5) & 0x3FFF, 14)
        w, t = fix(t, 14, 2, 5, 0x3FFF << 5)
        return w, 'branch', t
    if word & 0x1F000000 == 0x10000000:                    # adr, adrp
        imm = _sx(((word >> 5) & 0x7FFFF) << 2 | ((word >> 29) & 3), 21)
        if word >> 31:
            t = (old_pc & ~0xFFF) + (imm << 12)
            d = (t >> 12) - (new_pc >> 12)
        else:
            t = old_pc + imm
            require(not lo <= t < hi, f'clone adr into the range at {old_pc:#x} (not supported)')
            d = t - new_pc
        require(-(1 << 20) <= d < 1 << 20, f'clone adr/adrp out of range at {old_pc:#x}')
        return (word & ~(0x7FFFF << 5 | 3 << 29)) | (d & 3) << 29 | ((d >> 2) & 0x7FFFF) << 5, 'literal', t
    return word, None, None


def build_clone(image, g, c, symbols):
    """The bytes of clone c of group g: the stock range from the user's MAIN, PC-relative words
    re-encoded for the new address, the declared patch words. Never stored in the repository."""
    stock = image.read(c.lo, c.hi - c.lo)
    require(hashlib.sha256(stock).hexdigest() == GG.CLONE_APPROVED.get((c.lo, c.hi)), f'{g.name}: clone {c.name}: stock range differs from the approved bytes')
    patches = {pt.offset: pt for pt in c.patches}
    out = bytearray()
    for k in range(0, len(stock), 4):
        w = struct.unpack_from('<I', stock, k)[0]
        if k in patches:
            pt = patches[k]
            w = A.branch(c.at + k, int(symbols[pt.symbol]['va'], 16), pt.link)
        else:
            w2, kind, _ = relocate(w, c.lo + k, c.at + k, c.lo, c.hi)
            require(kind != 'literal' or k in c.literals, f'{g.name}: clone {c.name}: undeclared literal or address load at +{k:#x}')
            w = w2
        out += struct.pack('<I', w)
    return bytes(out)


def site_contributors(groups, site_map):
    """{site name: [(group id, hook)]} for the sites the composition uses, contributors in id order."""
    out = {}
    for g in groups:
        for c in g.shared:
            out.setdefault(c.site, []).append((g.id, hook(g, c)))
    return {name: out[name] for name in sorted(out)}


def site_roles(groups, site_map):
    """{site: {role: [(gid, hook)]}} for the composed contributors, in each site's order."""
    present = {(c.site, g.id): hook(g, c) for g in groups for c in g.shared}
    wanted = {(c.site, g.id): c.role for g in groups for c in g.shared}
    out = {}
    for name, s in site_map.items():
        roles = {}
        order = list(s.order)
        if not order:
            out[name] = roles
            continue
        for role, gid in order:
            roles.setdefault(role, [])
            if (name, gid) in present:
                roles[role].append((gid, present[(name, gid)]))
        for gid in sorted(gid for (n, gid) in present if n == name):
            if not any(gid == x for _, x in order):
                roles[wanted[(name, gid)] or s.roles()[0]].append((gid, present[(name, gid)]))   # test-only contributors
        out[name] = roles
    return out


def sanitisers(groups, group_symbols):
    """[(group, entry, global when mask)] of the composed groups, in id order."""
    out = []
    for g in groups:
        base = len(out)
        for t in g.sanitise:
            out.append((g, t, sum(1 << (base + i) for i in t.when)))
    require(len(out) <= 32, 'more than 32 sanitiser entries')
    return out


def build_header(groups, contributors, site_map, records, batch=None, group_symbols=None):
    mask = GG.composition_mask(groups)
    deps = [0] * 16
    for g in groups:
        deps[g.id] = sum(1 << d for d in g.requires)
    qneed = sum(1 << g.id for g in groups if g.install_needs_q)
    terms = [(t, g.id) for g in groups for t in g.terms]
    lines = ['/* Generated by z63patch/gate.py: the composition. Do not edit. */',
             f'#define Z63_CARD_PRESENT {A.CARD_PRESENT}', f'#define Z63_TABLE {GG.TABLE:#x}ull',
             f'#define Z63_GROUPS {mask:#x}u', f'#define Z63_RECORDS {records}u',
             '#define Z63_DEPS {' + ','.join(f'{d:#x}' for d in deps) + '}',
             f'#define Z63_QNEED {qneed:#x}u', f'#define Z63_QTERM_COUNT {len(terms)}u',
             f'#define Z63_MOVIE_HOLD {sum(1 << g.id for g in groups if g.id and not g.movie_publish):#x}u']
    lines += header_qm(groups)
    body = ','.join(f'{{{t.address:#x}ull,{t.mask:#x}u,{t.value:#x}u,{gid},{t.width},{0 if t.equal else 1},0}}' for t, gid in terms)
    lines.append('#define Z63_QTERMS {' + body + '}')
    if batch is not None:
        lines.append(f'#define Z63_BATCH {batch}u')
    roles = site_roles(groups, site_map)
    for name in site_map:
        items = ' '.join(f'X({gid},{h})' for gid, h in contributors.get(name, ()))
        lines.append(f'#define Z63_SITE_{name.upper()}_CONTRIBUTORS(X) {items}')
        for role, members in roles[name].items():
            items = ' '.join(f'X({gid},{h})' for gid, h in members)
            lines.append(f'#define Z63_SITE_{name.upper()}_{role.upper()}(X) {items}')
    data16 = any(r.kind == 'data16' for g in groups for r in g.records) or any(t.width == 2 for g in groups for t in g.sanitise)
    if data16:
        lines.append('#define Z63_DATA16 1')
    for g in groups:
        if g.rows:
            sym, count = g.rows
            lines += [f'#define Z63_ROWS_GROUP {g.id}u', f'#define Z63_ROWS_COUNT {count}u',
                      f"#define Z63_ROWS_TEMPLATE {group_symbols[g.name][sym]['va']}ull"]
    cbs = [g for g in groups if g.restore]
    if cbs:
        lines += [f'#define Z63_RESTORE_COUNT {len(cbs)}u',
                  '#define Z63_RESTORE_CBS(X) ' + ' '.join(f'X({g.id},{GG.restore_symbol(g.id)})' for g in cbs)]
    lines += header_d26(groups)
    after = restore_order(groups)
    if after:
        lines.append('#define Z63_AFTER {' + ','.join(f'{after.get(i, 0):#x}' for i in range(16)) + '}')
    sans = sanitisers(groups, group_symbols)
    if sans:
        body = ','.join(f'{{{t.address:#x}ull,{t.match_lo:#x}ull,{t.match_hi:#x}ull,{t.stock:#x}ull,{when:#x}u,{g.id},{t.width},{{0,0}}}}'
                        for g, t, when in sans)
        lines += [f'#define Z63_SAN_COUNT {len(sans)}u', '#define Z63_SANS {' + body + '}']
    inc = ''.join(f'#include "{Path(site_map[n].source).relative_to("native/arm_time").as_posix()}"\n' for n in contributors)
    return '\n'.join(lines) + '\n', inc


def header_qm(groups):
    """Movie-view arming lines of gate_build.h (every composition): the groups that may install at
    an arming in QM (GG.qm_ok), the groups never deferred (GG.no_defer), the accepted pipeline
    classes (GG.QM_CLASSES) and the two GUI switches (GG.IMENU_CLOSED, GG.SHOOTING_SCREEN)."""
    return [f'#define Z63_QM_OK {1 | sum(1 << g.id for g in groups if GG.qm_ok(g)):#x}u',
            f'#define Z63_NODEFER {sum(1 << g.id for g in groups if GG.no_defer(g)):#x}u',
            f'#define Z63_QM_CLASSES {GG.qm_class_mask():#x}u', f'#define Z63_QM_IMENU {1 if GG.IMENU_CLOSED else 0}',
            f'#define Z63_QM_SCREEN {1 if GG.SHOOTING_SCREEN else 0}']


def header_d26(groups):
    """Movie-view publication lines of gate_build.h: publication callbacks and anchor holds (none: no line)."""
    out = []
    pubs = [g for g in groups if g.publish]
    if pubs:
        out += [f'#define Z63_PUBCB_COUNT {len(pubs)}u',
                '#define Z63_PUBCBS(X) ' + ' '.join(f'X({g.id},{GG.publish_symbol(g.id)})' for g in pubs)]
    holds = [g for g in groups if g.hold]
    if holds:
        out += [f'#define Z63_HOLD_COUNT {len(holds)}u',
                '#define Z63_HOLDS(X) ' + ' '.join(f'X({g.id},{g.hold:#x}u)' for g in holds)]
    return out + header_g10(groups)


def header_g10(groups):
    """R-G10-0/1/2/4 lines of gate_build.h (none: no line): the movie-entry word when a feature
    group lacks movie_publish, break callbacks, group-raised breaks, the nraw_budget pass cap."""
    out = []
    if any(g.id and not g.movie_publish for g in groups):
        out.append('#define Z63_ENTRY 1')
    cbs = [g for g in groups if g.restore_now]
    if cbs:
        out += [f'#define Z63_BREAKCB_COUNT {len(cbs)}u',
                '#define Z63_BREAKCBS(X) ' + ' '.join(f'X({g.id},{GG.break_symbol(g.id)})' for g in cbs)]
    if any(g.raises for g in groups):
        out.append('#define Z63_RAISE 1')
    caps = [g.pass_cap for g in groups if g.pass_cap]
    require(len(caps) <= 1, 'more than one nraw_budget pass cap')
    if caps:
        out.append(f'#define Z63_NRAW_BUDGET_CAP {caps[0]}')
    return out


def compile_core(groups, contributors, site_map, records, hooks, compiler='clang', batch=None, group_symbols=None):
    head, inc = build_header(groups, contributors, site_map, records, batch, group_symbols)
    with tempfile.TemporaryDirectory(prefix='z63-gate-') as t:
        p = Path(t)
        (p / 'gate_build.h').write_text(head)
        (p / 'gate_sites.inc').write_text(inc)
        raw = _compile(compiler, ROOT / 'native/arm_time/gate.c', CORE_DEFINES, [p, ROOT / 'native/arm_time'], p / 'gate.o')
    slots = {GG.CARD_SECTION: list(GG.CARD)}
    code, sections, symbols, relocations = link(raw, base=GG.CORE_LO, end=GG.CORE_END, externals=hooks, slots=slots)
    require(int(symbols['z63_entry']['va'], 16) == GG.CORE_LO, 'z63_entry is not first')
    require(GG.CORE_LO + len(code) <= GG.CORE_END, 'gate core ends above 0x401A5000')
    card = bytes(slots[GG.CARD_SECTION][2]) if len(slots[GG.CARD_SECTION]) == 3 else b''
    require(GG.CARD[0] + len(card) <= GG.CARD[1], 'card section ends above 0x401A6000')
    return code, sections, symbols, relocations, head, inc, card


def records(image, groups, site_map, contributors, core_symbols, group_symbols):
    """(packed table, manifest rows, set ids). Order: the infrastructure records (S6 order: P,
    the 8 pipeline call sites, the 2 dispatcher callbacks), then per group in id order the
    shared sites it is the lowest contributor of, then its own records in declaration order.
    Members of a plain set are then ordered by rank in the positions the set holds; an
    all-or-none set must already be contiguous. Last, the two update-deny records (UPDATE_DENY).
    Install runs in table order after the deny records, restore in reverse; the deny records are
    never restored."""
    _, base = A.records_s5(image, core_symbols, marker=False)
    rows = []
    for i, r in enumerate(base):
        kind = r['kind']
        if kind == 'page64':
            extra = dict(groups=1, set=GG.VIDEO_MENU_SET, rank=1, wrapper='-', needs_q=True, tx=0)
        else:
            extra = dict(groups=1, set='', rank=0, wrapper='F', needs_q=False, tx=0)
        rows.append(dict(kind=kind, cls=r['cls'], address=int(r['address'], 16), old=int(r['old'], 16),
                         new=int(r['new'], 16), all_or_none=False, owner='infrastructure', purpose=r['purpose'], **extra))
    for g in groups:
        for name, members in contributors.items():
            if members[0][0] != g.id:
                continue
            s = site_map[name]
            adapter = int(core_symbols[s.adapter]['va'], 16)
            for at, old in s.words():
                stock = struct.unpack('<Q', image.read(at, 8))[0] if s.kind == 'data64' else image.words(at, 1)[0]
                require(stock == old, f'site {name}: stock word at {at:#x} changed')
                new = adapter if s.kind == 'data64' else A.branch(at, adapter, s.link)
                rows.append(dict(kind=s.kind, cls=s.cls, address=at, old=old, new=new, tx=0,
                                 groups=sum(1 << gid for gid, _ in members), set=s.set, rank=s.rank, wrapper=s.wrapper,
                                 needs_q=s.needs_q, all_or_none=False, owner=f'site {name}', purpose=s.purpose))
        for r in g.records:
            if r.kind == 'data16':
                stock = struct.unpack('<H', image.read(r.address, 2))[0]
                new = r.new
            elif r.kind == 'data64':
                stock = struct.unpack('<Q', image.read(r.address, 8))[0]
                new = int(group_symbols[g.name][r.symbol]['va'], 16)
            else:
                stock = image.words(r.address, 1)[0]
                new = A.branch(r.address, int(group_symbols[g.name][r.symbol]['va'], 16), r.link)
            require(stock == r.old, f'{g.name}: stock word at {r.address:#x} is {stock:#x}, not {r.old:#x}')
            setname = r.set if r.set == GG.VIDEO_MENU_SET else f'{g.name}.{r.set}' if r.set else ''
            rows.append(dict(kind=r.kind, cls=r.cls, address=r.address, old=r.old, new=new, groups=1 << g.id, tx=r.tx,
                             set=setname, rank=r.rank, wrapper=r.wrapper,
                             needs_q=r.needs_q, all_or_none=r.all_or_none, owner=g.name, purpose=r.purpose))
    # Set ids: the video-menu set is 1, the others from 2 in order of first appearance.
    ids = {GG.VIDEO_MENU_SET: 1}
    for r in rows:
        if r['set'] and r['set'] not in ids:
            ids[r['set']] = len(ids) + 1
    require(len(ids) <= 255, 'more than 255 sets')
    for name, sid in ids.items():
        at = [i for i, r in enumerate(rows) if r['set'] == name]
        members = sorted((rows[i] for i in at), key=lambda r: r['rank'])
        if any(r['all_or_none'] for r in members):
            require(at == list(range(at[0], at[0] + len(at))), f'all-or-none set {name} is not contiguous')
            require(len({r['groups'] for r in members}) == 1 and len(at) <= 64, f'all-or-none set {name}: one group, at most 64')
        for i, r in zip(at, members):
            rows[i] = r
    # The deny records last (installed first by the core, never restored).
    denied = int(core_symbols['z63_update_denied']['va'], 16)
    for at, target, purpose in UPDATE_DENY:
        old = image.words(at, 1)[0]
        require(old == A.branch(at, target, False), f'update deny: stock word at {at:#x} is not b {target:#x}')
        rows.append(dict(kind='code32', cls='X', address=at, old=old, new=A.branch(at, denied, False), groups=1, tx=0, set='', rank=0,
                         wrapper='X', needs_q=False, all_or_none=False, pinned=True, owner='update deny', purpose=purpose))
    require(len({r['address'] for r in rows}) == len(rows), 'a word appears twice')
    require(len(rows) <= GG.MAX_RECORDS and GG.TABLE + RECORD_BYTES * len(rows) <= GG.TABLE_END, 'install table does not fit')
    packed = b''
    out = []
    for i, r in enumerate(rows):
        sid = ids[r['set']] if r['set'] else 0
        flags = (A.F_NEEDQ if r['needs_q'] else 0) | (A.F_ATOMIC if r['all_or_none'] else 0) | (F_PIN if r.get('pinned') else 0)
        wclass = 0 if r['wrapper'] == '-' else GG.WRAPPER_CLASSES[r['wrapper']]
        packed += struct.pack(RECORD, KINDS[r['kind']], ord(r['cls']), i, r['groups'], sid, r['rank'], wclass, flags,
                              r['tx'], 0, 0, r['address'], r['old'], r['new'])
        code = r['kind'] == 'code32'
        out.append(dict(order=i, kind=r['kind'], cls=r['cls'], groups=r['groups'], set=sid, set_name=r['set'],
                        rank=r['rank'], wrapper_class=r['wrapper'], restore_needs_q=r['needs_q'],
                        all_or_none=r['all_or_none'], tx_pins=r['tx'], owner=r['owner'], address=hex(r['address']),
                        old=f"{r['old']:08x}" if code else hex(r['old']), new=f"{r['new']:08x}" if code else hex(r['new']),
                        purpose=r['purpose'], **({'pinned': True} if r.get('pinned') else {})))
    return packed, out, ids


MAIN_BASE = 0x40160000
FLASHED_SET = ((A.GUARD, A.GUARD + A.GUARD_BYTES), (A.HOOK, A.HOOK + 4), (A.PATCH_LO, A.PATCH_HI))


def check_flashed_set(original, data, edits):
    """MAIN only, and inside MAIN only the flashed set: the guard, the hook word and the patch
    range (header included). Every recorded edit lies inside it, and no byte outside it differs
    from stock."""
    require(len(data) == len(original), 'output is not a MAIN image of the stock length')
    for e in edits:
        at = int(e['address'], 16)
        require(any(a <= at and at + e['bytes'] <= b for a, b in FLASHED_SET), f'edit outside the flashed set at {e["address"]}')
    at = 0
    for a, b in sorted(FLASHED_SET):
        require(data[at:a - MAIN_BASE] == original[at:a - MAIN_BASE], f'bytes outside the flashed set differ from stock before {a:#x}')
        at = b - MAIN_BASE
    require(data[at:] == original[at:], 'bytes outside the flashed set differ from stock')


def build(original, stage=None, compiler='clang', *, groups=None, extra=(), batch=None):
    """(image bytes, manifest) for a composition: a stage of COMPOSITIONS, or (tests) an
    explicit list of group names with test-only groups in extra."""
    names = COMPOSITIONS[stage] if stage is not None else tuple(groups)
    require(stage is None or stage in COMPOSITIONS, 'unknown stage')
    composed, declared, site_map = resolve(names, extra)
    image = Image(original)
    A.check_stock(image)
    A.check_stock_s5(image)
    try:
        GG.check_gui_stock(lambda va: image.words(va, 1)[0])     # the GUI object the arming views read
    except ValueError as e:
        require(False, str(e))
    group_code = {}
    group_symbols = {}
    hooks = {}
    blobs = {}          # group name -> [(address, bytes, purpose)] of its assets and clones
    for g in composed:
        assets = {}
        code, sections, symbols, relocations = link_group(g, compiler, assets, stock=original)
        group_symbols[g.name] = symbols
        blobs[g.name] = [(va, data, f'arm-time group {g.id} {g.name} asset') for va, data in sorted(assets.items())]
        derived = build_derived(original, g)
        blobs[g.name] += [(va, data, f'arm-time group {g.id} {g.name} derived asset ({builder})') for va, data, builder in derived]
        clones = []
        for c in g.clones:
            data = build_clone(image, g, c, symbols)
            blobs[g.name].append((c.at, data, f'arm-time group {g.id} {g.name} clone {c.name} of {c.lo:#x}..{c.hi:#x}'))
            clones.append(dict(name=c.name, address=hex(c.at), bytes=len(data), stock=[hex(c.lo), hex(c.hi)],
                               stock_sha256=GG.CLONE_APPROVED[(c.lo, c.hi)], sha256=hashlib.sha256(data).hexdigest(),
                               patches=[dict(offset=hex(pt.offset), symbol=pt.symbol, link=pt.link) for pt in c.patches]))
        group_code[g.name] = dict(id=g.id, slot=[hex(g.slot[0]), hex(g.slot[1])], address=hex(g.slot[0]), bytes=len(code),
                                  sha256=hashlib.sha256(code).hexdigest(), sections=sections, relocations=len(relocations),
                                  symbols=symbols, requires=list(g.requires), install_needs_q=g.install_needs_q,
                                  state=[hex(g.state[0]), g.state[1]] if g.state else None, source=g.source,
                                  stock_calls={k: hex(v) for k, v in g.stock_calls.items()},
                                  assets=[dict(address=hex(va), bytes=len(d), sha256=hashlib.sha256(d).hexdigest()) for va, d in sorted(assets.items())],
                                  clones=clones, entries=[e.symbol for e in g.entries],
                                  rows=[g.rows[0], g.rows[1]] if g.rows else None, text_slots=list(g.text_slots),
                                  sanitise=[dict(address=hex(t.address), width=t.width, stock=hex(t.stock), match=[hex(t.match_lo), hex(t.match_hi)],
                                                 when=list(t.when)) for t in g.sanitise],
                                  derived=[dict(address=hex(va), bytes=len(d), sha256=hashlib.sha256(d).hexdigest(), builder=b) for va, d, b in derived],
                                  restore=GG.restore_symbol(g.id) if g.restore else None, restore_after=list(g.restore_after), _code=code)
        if g.publish:
            group_code[g.name]['publish'] = GG.publish_symbol(g.id)
        if g.hold:
            group_code[g.name]['hold'] = hex(g.hold)
        for key, value in (('restore_now', GG.break_symbol(g.id) if g.restore_now else None), ('raises', g.raises or None),
                           ('pass_cap', g.pass_cap or None)):
            if value:
                group_code[g.name][key] = value
        for c in g.shared:
            hooks[hook(g, c)] = int(symbols[hook(g, c)]['va'], 16)
        if g.restore:
            hooks[GG.restore_symbol(g.id)] = int(symbols[GG.restore_symbol(g.id)]['va'], 16)
        if g.publish:
            hooks[GG.publish_symbol(g.id)] = int(symbols[GG.publish_symbol(g.id)]['va'], 16)
        if g.restore_now:
            hooks[GG.break_symbol(g.id)] = int(symbols[GG.break_symbol(g.id)]['va'], 16)
        if g.pass_cap:
            hooks[g.pass_cap] = int(symbols[g.pass_cap]['va'], 16)
    contributors = site_contributors(composed, site_map)
    # The record count is part of the core (Z63_RECORDS): count, compile, then derive the table.
    count = len(A.S5_CALL_SITES[0][3]) + len(A.S5_CALL_SITES[1][3]) + len(A.S5_CALLBACKS) + 1
    count += sum(len(site_map[n].words()) for n in contributors) + sum(len(g.records) for g in composed) + len(UPDATE_DENY)
    core, sections, core_symbols, relocations, head, inc, card = compile_core(composed, contributors, site_map, count, hooks, compiler, batch, group_symbols)
    packed, rows, set_ids = records(image, composed, site_map, contributors, core_symbols, group_symbols)
    require(len(rows) == count, 'record count')
    span = bytearray(A.HEADER - A.PATCH_LO)
    span[:len(core)] = core
    span[GG.CARD[0] - A.PATCH_LO:GG.CARD[0] - A.PATCH_LO + len(card)] = card
    for g in composed:
        code = group_code[g.name]['_code']
        span[g.slot[0] - A.PATCH_LO:g.slot[0] - A.PATCH_LO + len(code)] = code
        for va, data, _ in blobs[g.name]:
            require(not any(span[va - A.PATCH_LO:va - A.PATCH_LO + len(data)]), f'{g.name}: asset or clone at {va:#x} overlaps other bytes')
            span[va - A.PATCH_LO:va - A.PATCH_LO + len(data)] = data
    span[GG.TABLE - A.PATCH_LO:GG.TABLE - A.PATCH_LO + len(packed)] = packed
    head_bytes, build_id, crc = A.header(bytes(span), GG.CORE_LO, GG.TABLE, len(rows))
    mask = GG.composition_mask(composed)
    manifest = dict(schema=1, status='DO_NOT_FLASH', stage=stage or 'test', target='Z6III C2.01', stock_sha256=MAIN_SHA256,
                    design='arm-time gate',
                    composition=[g.name for g in composed], groups=mask,
                    group_ids={'0': 'infrastructure', **{str(g.id): g.name for g in composed}})
    manifest['code'] = dict(address=hex(GG.CORE_LO), bytes=len(core), end=hex(GG.CORE_LO + len(core)), limit=hex(GG.CORE_END),
                            sha256=hashlib.sha256(core).hexdigest(), sections=sections, relocations=len(relocations),
                            symbols={k: v for k, v in core_symbols.items() if k in CORE_SYMBOLS or k.startswith('z63_site_')},
                            build_header=head, sites_include=inc,
                            card=dict(section=GG.CARD_SECTION, address=hex(GG.CARD[0]), bytes=len(card), end=hex(GG.CARD[0] + len(card)),
                                      limit=hex(GG.CARD[1]), sha256=hashlib.sha256(card).hexdigest()))
    manifest['group_code'] = {k: {x: y for x, y in v.items() if x != '_code'} for k, v in group_code.items()}
    roles = site_roles(composed, site_map)
    manifest['sites'] = {n: dict(address=hex(site_map[n].address), addresses=[hex(a) for a, _ in site_map[n].words()], adapter=site_map[n].adapter,
                                 contributors=[dict(group=gid, hook=h) for gid, h in m],
                                 roles={r: [dict(group=gid, hook=h) for gid, h in x] for r, x in roles[n].items()}) for n, m in contributors.items()}
    manifest['dependencies'] = {g.name: list(g.requires) for g in composed}
    manifest['install_needs_q'] = [g.id for g in composed if g.install_needs_q]
    manifest['qm_install'] = [g.id for g in composed if GG.qm_ok(g)]       # the others are deferred at a QM arming
    manifest['sets'] = {name: sid for name, sid in set_ids.items()}
    manifest['install_table'] = dict(address=hex(GG.TABLE), record_bytes=RECORD_BYTES, capacity=GG.MAX_RECORDS,
                                     end=hex(GG.TABLE_END), records=rows)
    manifest['state_page'] = PAGE_MAP
    manifest['armed_hook'] = dict(address=hex(A.HOOK), flashed=f'{A.branch(A.HOOK, A.GUARD, True):08x}',
                                  armed=f"{A.branch(A.HOOK, int(core_symbols['z63_armed']['va'], 16), True):08x}",
                                  stock=f'{A.branch(A.HOOK, A.POLL, True):08x}')
    manifest['free_bytes'] = dict(core_to_limit=GG.CORE_END - GG.CORE_LO - len(core), card_to_limit=GG.CARD[1] - GG.CARD[0] - len(card),
                                  table_to_end=GG.TABLE_END - GG.TABLE - len(packed),
                                  slots={g.name: min([c.at for c in g.clones] + [g.slot[1]]) - g.slot[0] - group_code[g.name]['bytes'] for g in composed})
    if batch is not None:
        manifest['batch'] = batch
    manifest['header'] = dict(address=hex(A.HEADER), magic=A.MAGIC.decode(), build_id=f'{build_id:016x}', version=A.VERSION,
                              crc32=f'{crc:08x}', entry=hex(GG.CORE_LO), table=hex(GG.TABLE), records=len(rows))
    image.put(GG.CORE_LO, core, f'arm-time gate core ({manifest["stage"].upper()})', zero=True)
    if card:
        image.put(GG.CARD[0], card, f'arm-time gate core, card-detect arming section ({manifest["stage"].upper()})', zero=True)
    for g in composed:
        code = group_code[g.name]['_code']
        image.put(g.slot[0], code, f'arm-time group {g.id} {g.name} code', zero=True)
        for va, data, purpose in blobs[g.name]:
            image.put(va, data, purpose, zero=True)
    image.put(GG.TABLE, packed, f'arm-time install table ({len(rows)} records)', zero=True)
    image.put(A.HEADER, head_bytes, 'arm-time patch header', zero=True)
    guard = A.guard_words_v2(GG.CORE_LO, build_id)       # No 16 s floor; update-client test
    image.put(A.GUARD, guard, 'arm-time guard v2 (copied text W3)', zero=True)
    image.put(A.HOOK, A.u32(A.branch(A.HOOK, A.GUARD, True)), 'arm-time entry hook: card-poll call -> guard',
              expected=A.u32(A.branch(A.HOOK, A.POLL, True)))
    manifest['guard'] = dict(address=hex(A.GUARD), bytes=len(guard), version=2, sha256=hashlib.sha256(guard).hexdigest())
    manifest['entry_hook'] = dict(address=hex(A.HOOK), old=f'{A.branch(A.HOOK, A.POLL, True):08x}',
                                  new=f'{A.branch(A.HOOK, A.GUARD, True):08x}')
    manifest['anchor'] = dict(address=hex(A.ANCHOR), bytes=16, image='zero')
    data = image.finalize()
    check_flashed_set(original, data, image.edits)
    manifest['edits'] = image.edits
    manifest['stock_digest'] = stock_digest(original, manifest['install_table']['records'], image.edits, composed)
    manifest['output_sha256'] = hashlib.sha256(data).hexdigest()
    manifest['regions'] = [dict(address=e['address'], bytes=e['bytes'], purpose=e['purpose']) for e in sorted(image.edits, key=lambda e: e['offset'])]
    manifest['changed_bytes'] = sum(1 for a, b in zip(data, original) if a != b)
    return data, manifest


RECIPE_KEYS = ('composition', 'groups', 'group_ids', 'code', 'group_code', 'sites', 'dependencies', 'install_needs_q',
               'qm_install', 'sets', 'install_table', 'state_page', 'armed_hook', 'free_bytes', 'guard', 'entry_hook', 'header')


# A recipe stores no stock value. These fields of the build manifest hold stock
# words or hashes of short stock regions; the recipe drops them and keeps ONE digest over all of
# them (stock_digest), recomputed from the user's verified stock MAIN at build and verify time.
STOCK_DIGEST_TAG = b'z63-stock-digest-v1\0'
RECORD_WIDTH = {'code32': 4, 'data64': 8, 'page64': 8, 'data16': 2}


def stock_digest(stock, records, edits, groups):
    """SHA-256 over, in order: every install record in table order (address u64, width u8, the
    stock value the record restores, width bytes little-endian); every edit in offset order
    (address u64, length u32, the stock bytes the edit replaces); every sanitiser entry of the
    composed groups in id order (address u64, width u8, stock value u64). One digest for the whole
    recipe: a per-site hash of a 4-byte word could be reversed by trying every value."""
    h = hashlib.sha256(STOCK_DIGEST_TAG)
    for r in records:
        w = RECORD_WIDTH[r['kind']]
        h.update(struct.pack('<QB', int(r['address'], 16), w) + int(r['old'], 16).to_bytes(w, 'little'))
    for e in sorted(edits, key=lambda e: e['offset']):
        h.update(struct.pack('<QI', int(e['address'], 16), e['bytes']) + bytes(stock[e['offset']:e['offset'] + e['bytes']]))
    for g in sorted(groups, key=lambda g: g.id):
        for t in g.sanitise:
            h.update(struct.pack('<QBQ', t.address, t.width, t.stock))
    return h.hexdigest()


def strip_stock(manifest):
    """The recipe form of the fields that carry stock values."""
    out = json.loads(json.dumps({k: manifest[k] for k in RECIPE_KEYS}))
    for r in out['install_table']['records']:
        r.pop('old', None)
    out['armed_hook'].pop('stock', None)
    out['entry_hook'].pop('old', None)
    for g in out['group_code'].values():
        for t in g.get('sanitise', ()):
            t.pop('stock', None)
    return out


def recipe_edits(edits):
    return [{k: v for k, v in e.items() if k != 'before_sha256'} for e in edits]


def recipe(manifest):
    return dict(schema=1, target='Z6III C2.01', status='DO_NOT_FLASH', stage=manifest['stage'],
                output_sha256=manifest['output_sha256'], edits=recipe_edits(manifest['edits']),
                stock_digest=manifest['stock_digest'], **strip_stock(manifest))


def arm_file(manifest, groups=None):
    """The 64-byte arm file for a composed build. The mask must name the infrastructure and only
    groups of this build, and every selected group's dependencies."""
    mask = manifest['groups'] if groups is None else groups
    require(mask & 1 and not mask & ~manifest['groups'], 'group mask outside the build or without the infrastructure (bit 0)')
    ids = {int(k): v for k, v in manifest['group_ids'].items()}
    for gid, name in ids.items():
        if gid and mask >> gid & 1:
            missing = [d for d in manifest['dependencies'][name] if not mask >> d & 1]
            require(not missing, f'group {name} needs groups {missing}')
    return A.arm_file(int(manifest['header']['build_id'], 16), mask)
