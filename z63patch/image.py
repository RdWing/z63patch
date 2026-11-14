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
"""Immutable input checks and explicit nonoverlapping edit accounting."""
import hashlib
import struct

BASE = 0x40160000
DATA_OFFSET, DATA_VA, DATA_BYTES = 0x5095000, 0xA02808000, 0x3D9028
MAIN_BYTES = 88830365
MAIN_SHA256 = '236bbd874e0942fd91fb54a99af456f4ebcc0fd966af930753c43efd1839fe87'


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def offset(va):
    if BASE <= va < BASE+DATA_OFFSET:
        return va-BASE
    if DATA_VA <= va < DATA_VA+DATA_BYTES:
        return va-DATA_VA+DATA_OFFSET
    raise ValueError(f'unmapped address {va:#x}')


def extent(va, n):
    require(isinstance(n,int) and n>0,'invalid mapped extent')
    at=offset(va)
    require(offset(va+n-1)==at+n-1,'mapped extent crosses address domain')
    return at


class Image:
    def __init__(self, original):
        require(len(original) == MAIN_BYTES and sha(original) == MAIN_SHA256,
                'exact original Z6III C2.01 MAIN required')
        self.original, self.data, self.edits = original, bytearray(original), []

    def read(self, va, n):
        at = extent(va,n)
        return self.original[at:at+n]

    def words(self, va, count):
        return struct.unpack('<'+'I'*count, self.read(va, count*4))

    def put(self, va, data, purpose, expected=None, zero=False):
        at = extent(va,len(data))
        require(0 < len(data) <= len(self.original)-at, 'invalid edit extent')
        before = self.original[at:at+len(data)]
        require(expected is None or before == expected, f'original mismatch: {purpose}')
        require(not zero or not any(before), f'occupied injection region: {purpose}')
        require(not any(at < e['offset']+e['bytes'] and e['offset'] < at+len(data)
                        for e in self.edits), f'overlapping edit: {purpose}')
        self.data[at:at+len(data)] = data
        self.edits.append(dict(offset=at, address=hex(va), bytes=len(data), purpose=purpose,
                               before_sha256=sha(before), after_sha256=sha(data)))

    def finalize(self):
        cursor = 0
        for e in sorted(self.edits, key=lambda x: x['offset']):
            require(self.data[cursor:e['offset']] == self.original[cursor:e['offset']],
                    'undeclared change')
            cursor = e['offset']+e['bytes']
        require(self.data[cursor:] == self.original[cursor:], 'undeclared tail change')
        return bytes(self.data)
