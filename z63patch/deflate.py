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
"""Deterministic RFC 1951 fixed-code compression for bounded icon rasters."""
from bisect import bisect_right
import struct
import zlib

LENGTHS=(3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258)
LENGTH_BITS=(0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0)
DISTANCES=(1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577)
DISTANCE_BITS=(0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13)


def fixed_zlib(raw):
    if type(raw) is not bytes or len(raw)>4096:
        raise ValueError('icon raster must contain at most 4096 bytes')
    out=bytearray();pending=0;bits=0

    def write(value,count):
        nonlocal pending,bits
        pending|=value<<bits;bits+=count
        while bits>=8:
            out.append(pending&255);pending>>=8;bits-=8

    def huffman(value,count):
        reversed_bits=0
        for _ in range(count):reversed_bits=(reversed_bits<<1)|(value&1);value>>=1
        write(reversed_bits,count)

    def literal(value):
        if value<144:huffman(0x30+value,8)
        elif value<256:huffman(0x190+value-144,9)
        elif value<280:huffman(value-256,7)
        else:huffman(0xc0+value-280,8)

    write(3,3)  # Final block, fixed Huffman codes.
    positions={};at=0
    while at<len(raw):
        length=0;distance=0;limit=min(258,len(raw)-at)
        for previous in reversed(positions.get(raw[at:at+3],())[-64:]):
            n=0
            while n<limit and raw[previous+n]==raw[at+n]:n+=1
            if n>length:length=n;distance=at-previous
            if length==limit:break
        step=length if length>=3 else 1
        if length>=3:
            code=bisect_right(LENGTHS,length)-1
            literal(257+code);write(length-LENGTHS[code],LENGTH_BITS[code])
            code=bisect_right(DISTANCES,distance)-1
            huffman(code,5);write(distance-DISTANCES[code],DISTANCE_BITS[code])
        else:literal(raw[at])
        for index in range(at,at+step):
            if index+3<=len(raw):positions.setdefault(raw[index:index+3],[]).append(index)
        at+=step
    literal(256)
    if bits:out.append(pending)
    return b'\x78\x01'+bytes(out)+struct.pack('>I',zlib.adler32(raw)&0xffffffff)
