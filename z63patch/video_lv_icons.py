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
"""Live-view and i-menu frame-size icons derived from the user's exact C2.01 input.

Extends the nine recording-menu extension icons (0x7000..0x7008) with twelve
non-star twins (0x7009..0x7014) for modes whose stock table has only a star
icon. Each derived icon copies one bounded block from a stock non-star donor
of the same icon set. No extracted Nikon icon is stored in the repository.
"""
import binascii
import json
from pathlib import Path
import struct
import zlib
from .image import Image, require, sha
from .deflate import fixed_zlib
from . import video_icons

PROFILE=Path(__file__).resolve().parents[1]/'profiles/z6iii-c201-video-lv-icons.json'
SIGNATURE=b'\x89PNG\r\n\x1a\n'


def decode(raw,width,height):
    """Read the bounded 8-bit grayscale, non-interlaced form used by these icons."""
    require(raw[:8]==SIGNATURE,'invalid icon PNG')
    at=8;chunks=[]
    while at<len(raw):
        require(at+12<=len(raw),'short icon chunk')
        size=struct.unpack_from('>I',raw,at)[0];kind=raw[at+4:at+8]
        require(size<=len(raw)-at-12,'icon chunk exceeds input')
        data=raw[at+8:at+8+size]
        require(binascii.crc32(kind+data)&0xffffffff==struct.unpack_from('>I',raw,at+8+size)[0],'icon CRC differs')
        chunks.append((kind,data));at+=12+size
    require([k for k,_ in chunks]==[b'IHDR',b'IDAT',b'IEND'],'unsupported icon chunks')
    require(chunks[0][1]==struct.pack('>IIBBBBB',width,height,8,0,0,0,0) and not chunks[2][1],'unsupported icon geometry')
    stride=width+1
    stream=zlib.decompressobj();data=stream.decompress(chunks[1][1],stride*height+1)
    require(len(data)==stride*height and stream.eof and not stream.unused_data and not stream.unconsumed_tail,'invalid icon raster')
    pixels=bytearray(width*height)
    for y in range(height):
        kind=data[stride*y];require(kind<=4,'unsupported PNG filter')
        for x in range(width):
            a=pixels[y*width+x-1] if x else 0;b=pixels[(y-1)*width+x] if y else 0
            c=pixels[(y-1)*width+x-1] if x and y else 0
            p=a+b-c;da,db,dc=abs(p-a),abs(p-b),abs(p-c)
            predictor=(0,a,b,(a+b)//2,a if da<=db and da<=dc else b if db<=dc else c)[kind]
            pixels[y*width+x]=(data[stride*y+x+1]+predictor)&255
    return pixels


def encode(pixels,width,height):
    raster=b''.join(b'\0'+bytes(pixels[y*width:(y+1)*width]) for y in range(height))
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',binascii.crc32(kind+data)&0xffffffff)
    return SIGNATURE+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0,0,0,0))+chunk(b'IDAT',fixed_zlib(raster))+chunk(b'IEND',b'')


def source(image,profile,number):
    record=profile['records'][str(number)];table=int(profile['resource_table'],16)
    flags,n,z0,z1,pointer=struct.unpack('<IIIIQ',image.read(table+24*number,24))
    require((flags,n,z0,z1,pointer)==(record['flags'],record['bytes'],0,0,int(record['va'],16)),'changed icon record')
    raw=image.read(pointer,n);require(sha(raw)==record['sha256'],'changed icon source')
    return raw


def derived(image,profile):
    out=[]
    for name in ('fill','outline'):
        group=profile['sets'][name];width,height=group['size'];rows,first=group['block']
        donor=decode(source(image,profile,group['donor']),width,height)
        for row in group['generated']:
            pixels=decode(source(image,profile,row['star']),width,height)
            for y in range(rows):pixels[y*width+first:(y+1)*width]=donor[y*width+first:(y+1)*width]
            raw=encode(pixels,width,height)
            require(len(raw)==row['bytes'] and sha(raw)==row['sha256'],'derived icon differs')
            out.append((row['plain'],raw))
    require([n for n,_ in out]==list(range(0x7009,0x7009+len(out))) and len(out)==12,'derived icon numbering')
    return [raw for _,raw in out]


def assets(original,log_icons=False):
    """Twenty-one extension records followed by their PNG data. With the
    live-view Log icon unit there are twenty-five records (0x7015..0x7018: the
    Z8 patch's S-Log3 and Log3G10 fill and outline icons) and the four Log PNGs
    are returned separately, for the asset block at log_record.LV_ICON_PNGS
    (the space after the 21 PNGs is taken by the Desqueeze tables in the
    combined builds)."""
    image=Image(original);profile=json.loads(PROFILE.read_text())
    menu=json.loads(video_icons.PROFILE.read_text())
    require(int(menu['asset_start'],16)==int(profile['asset_start'],16),'icon asset base differs')
    table=int(menu['resource_table'],16);raws=[]
    for row in menu['generated']:
        record=menu['records'][str(row['star'])]
        flags,n,z0,z1,pointer=struct.unpack('<IIIIQ',image.read(table+24*row['star'],24))
        require((flags,n,z0,z1,pointer)==(record['flags'],record['bytes'],0,0,int(record['va'],16)),'changed icon record')
        raw=video_icons.plain_png(image.read(pointer,n));require(sha(raw)==row['sha256'],'derived icon differs');raws.append(raw)
    raws+=derived(image,profile)
    extra=[]
    if log_icons:
        from .log_record import lv_icons
        extra=lv_icons()
    start=int(profile['asset_start'],16);out=bytearray(16*(len(raws)+len(extra)))
    for index,raw in enumerate(raws):
        out.extend(bytes(-len(out)%8));pointer=start+len(out)
        struct.pack_into('<HHIQ',out,index*16,0x100,0,len(raw),pointer);out.extend(raw)
    require(len(raws)==profile['extension_count']==21 and start+len(out)<=int(profile['asset_end'],16),'icon assets exceed reservation')
    if not log_icons:return bytes(out)
    from .log_record import LV_ICON_PNGS
    logs=bytearray()
    for index,raw in enumerate(extra,len(raws)):
        logs.extend(bytes(-len(logs)%8));pointer=LV_ICON_PNGS+len(logs)
        struct.pack_into('<HHIQ',out,index*16,0x100,0,len(raw),pointer);logs.extend(raw)
    require(len(extra)==4 and LV_ICON_PNGS+len(logs)<=int(profile['asset_end'],16),'Log icon assets exceed reservation')
    return bytes(out),bytes(logs)
