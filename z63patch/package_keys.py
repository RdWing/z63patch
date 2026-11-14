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
"""Read the user's local C2.01 key material. Never include it in reports or exceptions.

The material is three byte strings: the AES-256 key (32 bytes), the counter block (16 bytes) and
the digest suffix (8 bytes). The patcher does not care how the user's file is laid out. It
identifies the key and the counter block by the SHA-256 of the bytes themselves, pinned below, and
the suffix by the stock update file's own header digest (SHA-1 over the clear body and the suffix),
so any container that holds the three values works: a schema-1 shared-material JSON, another JSON layout, a
text file of hex strings in any order (plain, byte-separated or as a C array), base64 (also
line-wrapped), base32, a list of decimal byte values, a raw binary file, or any of these with the
bytes in reversed order. A fingerprint is a one-way
hash of a 128- or 256-bit secret; it does not reveal the value. The 8-byte suffix has no pinned
fingerprint: a hash of 64 bits could be reversed by brute force, so nothing about it is stored.

This toolkit neither supplies nor acquires keys.
"""
from dataclasses import dataclass
import base64
import hashlib
from pathlib import Path
import re

ORIGINAL_SHA256='2369c01dcc194c1b9517e91ad2ee8bab8ff03cf52635473404ce8553b59ce131'

# name -> (length in bytes, SHA-256 of the bytes). Z6III C2.01.
FINGERPRINTS={
    'key':(32,'7d85432d3b8423b7bc82cb08c5f3193c04d78b4acbedaef407e4d9dca84f9804'),
    'iv':(16,'f1a2ef70cb6829d974e11c66b058b9c9fe4886b3721b88ad678c0a26df9f5259'),
}
SUFFIX_BYTES=8
MAX_FILE=1<<20
_HEX=re.compile(rb'[0-9a-fA-F]{16,}')
_B64=re.compile(rb'[A-Za-z0-9+/_-]{11,}={0,2}')
_B32=re.compile(rb'[A-Z2-7]{13,}={0,6}')
_DEC=re.compile(rb'(?:\b\d{1,3}\b[\s,;]*){8,}')
_HEX_NOISE=re.compile(rb'0[xX]|\\x|[\s:,;\-_{}\[\]()"\'hH]')
_WHITESPACE=re.compile(rb'\s+')


@dataclass(frozen=True,repr=False)
class Material:
    key: bytes
    iv: bytes
    suffix: bytes


def unique_object(pairs):
    """JSON object hook that refuses duplicate fields (used by tools that read reviewed JSON)."""
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('duplicate JSON field')
        result[key]=value
    return result


def _windows(blob,size):
    for start in range(0,len(blob)-size+1):
        yield blob[start:start+size]


def _hex_runs(data):
    for match in _HEX.finditer(data):
        text=match.group(0)
        for skew in (0,1):                       # a run may start on an odd nibble of a longer string
            body=text[skew:]
            body=body[:len(body)&~1]
            if body:yield bytes.fromhex(body.decode('ascii'))


def _decoded_runs(data):
    """Byte runs the file may hold the values in, for the ways keys are commonly written down:

    - hex, plain or with `0x`, and hex written byte by byte with separators or as a C array
      (`aa:bb:cc`, `AA BB CC`, `0xaa, 0xbb`, `\\xaa\\xbb`, `{0xaa, 0xbb}`);
    - base64 and URL-safe base64, on one line or wrapped over several (PEM-style bodies);
    - base32;
    - lists of decimal byte values (`[170, 187, 204]`), as JSON or code;
    - the raw bytes of a binary key file.
    Each byte value is also tried in reversed order by the caller."""
    yield from _hex_runs(data)
    yield from _hex_runs(_HEX_NOISE.sub(b'',data))
    for text in (data,_WHITESPACE.sub(b'',data)):
        for match in _B64.finditer(text):
            token=match.group(0).rstrip(b'=').replace(b'-',b'+').replace(b'_',b'/')
            for skew in range(4):                # the value may not start on a 4-character boundary
                part=token[skew:]
                part=part[:len(part)&~3] if len(part)%4==1 else part
                try:yield base64.b64decode(part+b'='*(-len(part)%4),validate=True)
                except (ValueError,TypeError):pass
    for match in _B32.finditer(data):
        token=match.group(0).rstrip(b'=')
        try:yield base64.b32decode(token+b'='*(-len(token)%8))
        except (ValueError,TypeError):pass
    for match in _DEC.finditer(data):
        values=[int(v) for v in re.findall(rb'\d{1,3}',match.group(0))]
        run=bytearray()
        for value in values:                     # split at anything that is not a byte value
            if value<256:run.append(value)
            else:
                if len(run)>=8:yield bytes(run)
                run=bytearray()
        if len(run)>=8:yield bytes(run)
    yield data


def find_material(data,fingerprints=None,suffix_check=None):
    """Return the Material found in `data`, whatever its layout, or raise ValueError.

    The key and the counter block are found by hashing every candidate byte run of the right
    length and comparing it with the pinned fingerprint. The suffix is found by `suffix_check`:
    called with the key and counter block once they are found, it returns a predicate that is
    true for the one 8-byte value that reproduces the stock file's digest (`package.suffix_check`).
    Nothing about the values is reported; the error names only the missing parts."""
    fingerprints=FINGERPRINTS if fingerprints is None else fingerprints
    wanted={name:(size,bytes.fromhex(digest)) for name,(size,digest) in fingerprints.items()}
    found={}
    seen=set()
    for run in _decoded_runs(bytes(data)):
        if run in seen:continue
        seen.add(run)
        for name,(size,digest) in wanted.items():
            if name in found or len(run)<size:continue
            for piece in _windows(run,size):
                if hashlib.sha256(piece).digest()==digest:
                    found[name]=piece
                    break
                if hashlib.sha256(piece[::-1]).digest()==digest:   # written least significant byte first
                    found[name]=piece[::-1]
                    break
        if len(found)==len(wanted):break
    if 'suffix' not in found and 'key' in found and 'iv' in found and suffix_check is not None:
        matches=suffix_check(found['key'],found['iv'])
        tried=set()
        for run in _decoded_runs(bytes(data)):
            if run in tried or len(run)<SUFFIX_BYTES:continue
            tried.add(run)
            for piece in _windows(run,SUFFIX_BYTES):
                if matches(piece):found['suffix']=piece;break
                if matches(piece[::-1]):found['suffix']=piece[::-1];break
            if 'suffix' in found:break
    missing=sorted({'key','iv','suffix'}-set(found))
    if missing:
        raise ValueError('local C2.01 material: no value matching the expected fingerprint or digest for '+', '.join(missing))
    return Material(found['key'],found['iv'],found['suffix'])


def load_material(path,suffix_check=None):
    """Read the user's key-material file in any layout; identify the key and counter block by
    fingerprint and the suffix with `suffix_check` (see `find_material`)."""
    try:
        data=Path(path).read_bytes()
    except OSError:
        raise ValueError('local C2.01 material file is missing or unreadable') from None
    if len(data)>MAX_FILE:
        raise ValueError('local C2.01 material file is too large')
    return find_material(data,suffix_check=suffix_check)
