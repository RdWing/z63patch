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
"""Derive recording-menu icons from the user's exact C2.01 input."""
import binascii
import json
from pathlib import Path
import struct
import zlib
from .image import Image, require, sha
from .deflate import fixed_zlib

PROFILE=Path(__file__).resolve().parents[1]/'profiles/z6iii-c201-video-menu-icons.json'


def gray_png(raw):
    """Read only the bounded grayscale form used by the source icons."""
    require(raw[:8]==b'\x89PNG\r\n\x1a\n','invalid icon PNG')
    at=8;chunks=[]
    while at<len(raw):
        require(at+12<=len(raw),'short icon chunk')
        size=struct.unpack_from('>I',raw,at)[0];kind=raw[at+4:at+8]
        require(size<=len(raw)-at-12,'icon chunk exceeds input')
        data=raw[at+8:at+8+size]
        require(binascii.crc32(kind+data)&0xffffffff==struct.unpack_from('>I',raw,at+8+size)[0],'icon CRC differs')
        chunks.append((kind,data));at+=12+size
    require([k for k,v in chunks]==[b'IHDR',b'IDAT',b'IEND'],'unsupported icon chunks')
    require(chunks[0][1]==struct.pack('>IIBBBBB',72,36,8,0,0,0,0) and not chunks[2][1],'unsupported icon geometry')
    stream=zlib.decompressobj();data=stream.decompress(chunks[1][1],73*36+1)
    require(len(data)==73*36 and stream.eof and not stream.unused_data and not stream.unconsumed_tail,'invalid icon raster')
    pixels=bytearray(72*36)
    for y in range(36):
        kind=data[73*y];require(kind<=4,'unsupported PNG filter')
        for x in range(72):
            a=pixels[y*72+x-1] if x else 0;b=pixels[(y-1)*72+x] if y else 0
            c=pixels[(y-1)*72+x-1] if x and y else 0
            p=a+b-c;da,db,dc=abs(p-a),abs(p-b),abs(p-c)
            predictor=(0,a,b,(a+b)//2,a if da<=db and da<=dc else b if db<=dc else c)[kind]
            pixels[y*72+x]=(data[73*y+x+1]+predictor)&255
    return bytes(pixels)


def plain_png(raw):
    pixels=bytearray(gray_png(raw))
    for y in range(14):pixels[y*72+59:y*72+72]=bytes(13)
    raster=b''.join(b'\0'+pixels[y*72:(y+1)*72] for y in range(36))
    compressed=fixed_zlib(raster)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',binascii.crc32(kind+data)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',72,36,8,0,0,0,0))+chunk(b'IDAT',compressed)+chunk(b'IEND',b'')


def assets(original):
    image=Image(original);profile=json.loads(PROFILE.read_text())
    table=int(profile['resource_table'],16);raws={}
    for number,record in profile['records'].items():
        flags,n,z0,z1,pointer=struct.unpack('<IIIIQ',image.read(table+24*int(number),24))
        require((flags,n,z0,z1,pointer)==(record['flags'],record['bytes'],0,0,int(record['va'],16)),'changed icon record')
        raw=image.read(pointer,n);require(sha(raw)==record['sha256'],'changed icon source');raws[int(number)]=raw
    start=int(profile['asset_start'],16);out=bytearray(16*len(profile['generated']))
    for index,row in enumerate(profile['generated']):
        raw=plain_png(raws[row['star']]);require(sha(raw)==row['sha256'],'derived icon differs')
        out.extend(bytes(-len(out)%8));pointer=start+len(out)
        struct.pack_into('<HHIQ',out,index*16,0x100,0,len(raw),pointer);out.extend(raw)
    require(start+len(out)<=int(profile['asset_end'],16),'icon assets exceed reservation')
    return bytes(out)
