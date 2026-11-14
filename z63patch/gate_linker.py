# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 NikonFirmwarePatch contributors
# SPDX-FileCopyrightText: 2026 RdWing
#
# z63patch - Nikon Z6III Modifications
# Adapted from NikonFirmwarePatch (GPL-3.0-only); see NOTICE.md.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# @package z63patch
# @license GPLv3 License (https://www.gnu.org/licenses/gpl-3.0.html)
#
#   Copyright (C) 2026 NikonFirmwarePatch contributors
#   Copyright (C) 2026 RdWing (modifications)
#
"""z63patch/linker.py with named externals, for the composed arm-time gate (z63patch/gate.py).

A separate copy because z63patch/linker.py is part of the reviewed units' source identity
(z63patch/units.py source_identities): changing it would invalidate every reviewed unit.
Only read-only PROGBITS sections are accepted, as there."""
import struct
from .image import require
CAVE, CAVE_END = 0x40196000, 0x401A6000

ASSET_PREFIX = '.z63asset.'


def link(raw, *, base=CAVE, end=CAVE_END, externals=None, assets=None, slots=None):
    """externals (optional): {name: absolute address} for symbols the object leaves undefined
    (the arm-time gate links each feature group in its own range and names its stock calls
    and, for the core, the group hooks this way). Without it an undefined symbol is refused.

    assets (optional dict, filled in): sections named ``.z63asset.<hex address>`` are placed at
    that address in their own buffer, {address: bytearray}, not in the code; without the dict
    such a section is refused. Relocations inside them are applied like any other.

    slots (optional dict, {section name: [lo, hi]}, filled in): executable sections of that name
    are linked at lo in their own buffer, appended to the entry (the section bytes then sit at
    lo + offset, aligned as in the main code); the buffer must end at or below hi. The composed
    gate puts its card-detect arming code (.text.z63card) there. Without the dict such a section
    is linked with the rest."""
    require(type(base) is int and type(end) is int and 0 <= base < end <= 1 << 64 and base % 16 == 0, 'Invalid code reservation')
    require(raw[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<HH', raw, 16) == (1, 183), 'Invalid AArch64 object or relocation')
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    sz, n, si = struct.unpack_from('<HHH', raw, 58)
    ss = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * sz) for i in range(n)]
    names = raw[ss[si][4]:ss[si][4] + ss[si][5]]

    def name(s):
        return names[s[0]:names.index(0, s[0])].decode()
    indexes = [i for i, s in enumerate(ss) if s[2] & 2 and s[5]]
    indexes.sort(key=lambda i: (0 if name(ss[i]) == '.text.entry' else 1, i))
    out = bytearray()
    where = {}
    buffers = {}
    sections = []
    for i in indexes:
        s = ss[i]
        require(s[1] == 1 and (not s[2] & 1), ('mutable or non-PROGBITS section', name(s)))
        if name(s).startswith(ASSET_PREFIX):
            require(assets is not None and not s[2] & 4, ('asset section not allowed here or executable', name(s)))
            va = int(name(s)[len(ASSET_PREFIX):], 16)
            require(va % 16 == 0 and va not in assets, ('asset address', name(s)))
            assets[va] = bytearray(raw[s[4]:s[4] + s[5]])
            where[i] = va
            buffers[i] = (assets[va], va)
            sections.append(dict(index=i, name=name(s), va=hex(va), bytes=s[5], executable=False, asset=True))
            continue
        if slots is not None and name(s) in slots:
            require(s[2] & 4, ('slot section not executable', name(s)))
            entry = slots[name(s)]
            if len(entry) == 2:
                entry.append(bytearray())
            lo, hi, buf = entry
            require(lo % 16 == 0 and lo < hi, ('slot address', name(s)))
            alignment = max(16, s[8])
            at = len(buf) + alignment - 1 & -alignment
            buf.extend(bytes(at - len(buf)))
            where[i] = lo + at
            buffers[i] = (buf, lo)
            buf.extend(raw[s[4]:s[4] + s[5]])
            require(lo + len(buf) <= hi, ('slot section exceeds its slot', name(s)))
            sections.append(dict(index=i, name=name(s), va=hex(where[i]), bytes=s[5], executable=True, slot=hex(lo)))
            continue
        alignment = max(16, s[8])
        at = len(out) + alignment - 1 & -alignment
        out.extend(bytes(at - len(out)))
        where[i] = base + at
        buffers[i] = (out, base)
        out.extend(raw[s[4]:s[4] + s[5]])
        sections.append(dict(index=i, name=name(s), va=hex(where[i]), bytes=s[5], executable=bool(s[2] & 4)))
    for s in ss:
        require(not (s[2] & 2 and s[5]) or ss.index(s) in where, ('unplaced allocatable section', name(s)))
    syms = []
    for s in ss:
        if s[1] != 2:
            continue
        strings = raw[ss[s[6]][4]:ss[s[6]][4] + ss[s[6]][5]]
        for at in range(s[4], s[4] + s[5], s[9]):
            nm, info, other, sec, val, size = struct.unpack_from('<IBBHQQ', raw, at)
            text = strings[nm:strings.index(0, nm)].decode()
            syms.append(dict(name=text, section=sec, va=where.get(sec, 0) + val, size=size))
    relocations = []
    for sec in ss:
        if sec[1] != 4 or not sec[5]:
            continue
        target = sec[7]
        if target not in where:
            continue
        for at in range(sec[4], sec[4] + sec[5], sec[9]):
            off, info, addend = struct.unpack_from('<QQq', raw, at)
            typ, idx = (info & 4294967295, info >> 32)
            sym = syms[idx]
            if sym['section'] in where:
                s = sym['va'] + addend
            else:
                require(externals is not None and sym['section'] == 0 and sym['name'] in externals, ('unresolved symbol', sym))
                s = externals[sym['name']] + addend
            p = where[target] + off
            buf, bbase = buffers[target]
            o = p - bbase
            before = struct.unpack_from('<I', buf, o)[0]
            w = before
            if typ in (282, 283):
                d = s - p
                require(d % 4 == 0 and -(1 << 27) <= d < 1 << 27, 'Invalid AArch64 object or relocation')
                w = w & 4227858432 | d // 4 & 67108863
            elif typ in (274, 275):
                d = s - p if typ == 274 else ((s & ~4095) - (p & ~4095)) // 4096
                require(-(1 << 20) <= d < 1 << 20, 'Invalid AArch64 object or relocation')
                w = w & ~1627389920 | (d & 3) << 29 | (d >> 2 & 524287) << 5
            elif typ == 277:
                w = w & ~4193280 | (s & 4095) << 10
            elif typ in (278, 284, 285, 286, 299):
                shift = {278: 0, 284: 1, 285: 2, 286: 3, 299: 4}[typ]
                require(s % (1 << shift) == 0, 'Invalid AArch64 object or relocation')
                w = w & ~4193280 | (s & 4095) >> shift << 10
            elif typ in (280, 279):
                d = s - p
                require(d % 4 == 0, 'Invalid AArch64 object or relocation')
                if typ == 280:
                    require(-(1 << 20) <= d < 1 << 20, 'Invalid AArch64 object or relocation')
                    w = w & ~(524287 << 5) | (d // 4 & 524287) << 5
                else:
                    require(-(1 << 15) <= d < 1 << 15, 'Invalid AArch64 object or relocation')
                    w = w & ~(16383 << 5) | (d // 4 & 16383) << 5
            elif typ in (257, 258, 261):
                value = s - p if typ == 261 else s
                struct.pack_into('<Q' if typ == 257 else '<I', buf, o, value & (1 << (64 if typ == 257 else 32)) - 1)
                relocations.append(dict(va=hex(p), type=typ, symbol=sym['name'], resolved=hex(s), addend=addend))
                continue
            else:
                raise ValueError('Unsupported relocation ' + str(typ))
            struct.pack_into('<I', buf, o, w)
            relocations.append(dict(va=hex(p), type=typ, symbol=sym['name'], resolved=hex(s), addend=addend, before=hex(before), after=hex(w)))
    symbols = {s['name']: dict(va=hex(s['va']), bytes=s['size']) for s in syms if s['section'] in where and s['name']}
    require(base + len(out) <= end, 'Linked source exceeds reserved code region')
    return (bytes(out), sections, symbols, relocations)
