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
"""Independent C2.01 package audit. Does not import the builder or its report.

The container rules (header, directory, member and body CRCs, digest) are checked here with a
separate implementation of the body cipher. Tests may pass a synthetic profile and a toy
`decipher`; the real path always uses the pinned profile and AES-256-CTR below.
"""
import binascii
import hashlib
import json
from pathlib import Path
import struct
from .image import MAIN_SHA256,MAIN_BYTES,require,sha
from .package_contract import verify_main

PROFILE=Path(__file__).resolve().parents[1]/'profiles/z6iii-c201-package.json'
HEADER_BYTES=608        # [0,0x240) protected prefix and RSA slots, digest [0x240,0x254), tail [0x254,0x260)
DIGEST=(576,596)
TAIL=(596,608)
LABEL=b' '*32


def expected_tail(digest):
    """Independent form of the header-tail rule: 32 steps of x -> 214013 x + 2531011 (mod 2**32)
    from the first digest byte; the tail is bits 16..23 of steps 21 to 32."""
    x=digest[0];steps=[]
    while len(steps)<32:
        x=(x*214013+2531011)%(1<<32)
        steps.append(x>>16&255)
    return bytes(steps[-12:])


def load_profile():
    return json.loads(PROFILE.read_text())


def aes_decipher(encrypted,material):
    """Plaintext of the body bytes after the 32-byte clear label (counter block C0+2 onwards)."""
    from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
    require(len(material.key)==32 and len(material.iv)==16 and len(material.suffix)==8,'invalid material extent')
    decryptor=Cipher(algorithms.AES(material.key),modes.CTR(material.iv)).decryptor()
    decryptor.update(bytes(32))
    return decryptor.update(encrypted)+decryptor.finalize()


def open_package(packet,material,profile,decipher=aes_decipher):
    """(clear body, [member bytes]) after every container check; members include their CRC."""
    require(len(packet)==profile['package_bytes'],'package extent differs')
    require(packet[544:576]==packet[:32][::-1],'wrapper mirror differs')
    require(packet[HEADER_BYTES:HEADER_BYTES+32]==LABEL,'clear body label differs')
    body=LABEL+decipher(packet[HEADER_BYTES+32:],material)
    require(hashlib.sha1(body+material.suffix).digest()==packet[DIGEST[0]:DIGEST[1]],'header digest differs')
    n=len(profile['members'])
    require(body[32:48]==struct.pack('>4I',n,48+32*n,0,0),'directory header differs')
    require(body[-14:]==bytes(14) and binascii.crc_hqx(body[:-14],0)==0,'body CRC or reserved tail differs')
    members=[];cursor=48+32*n
    for index,expected in enumerate(profile['members']):
        name,offset,size,z0,z1=struct.unpack_from('>16s4I',body,48+index*32)
        require(name==expected['name'].encode().ljust(16,b'\0'),'member name differs')
        require((offset,size,z0,z1)==(expected['offset'],expected['bytes'],0,0) and offset==cursor,'member layout differs')
        require(size>=2 and offset+size<=len(body)-16,'member extent exceeds body')
        member=body[offset:offset+size]
        require(binascii.crc_hqx(member,0)==0,'member CRC differs')
        members.append(member);cursor+=size
    require(cursor==len(body)-16,'directory does not own the complete body')
    return body,members


def verify_package(original,candidate,material,recipe,*,profile=None,decipher=aes_decipher,
                   main_identity=(MAIN_BYTES,MAIN_SHA256),check_main=verify_main):
    profile=profile or load_profile()
    target=profile.get('target_index',2)
    require(sha(original)==profile['original_sha256'] and len(original)==profile['package_bytes'],'original package identity differs')
    require(len(candidate)==len(original),'package extent changed')
    require(original[:DIGEST[0]]==candidate[:DIGEST[0]],'unapproved outer-header edit')
    for packet in (original,candidate):
        require(packet[TAIL[0]:TAIL[1]]==expected_tail(packet[DIGEST[0]:DIGEST[1]]),'header tail does not follow the digest')
    require(len(material.key)==32 and len(material.iv)==16 and len(material.suffix)==8,'invalid material extent')
    old,old_members=open_package(original,material,profile,decipher)
    require(sha(old)==profile['body_sha256'],'original plaintext identity differs')
    new,new_members=open_package(candidate,material,profile,decipher)
    for index,expected in enumerate(profile['members']):
        require(sha(old_members[index])==expected['member_sha256'],'original member differs')
        if index!=target:
            require(sha(new_members[index])==expected['member_sha256'] and new_members[index]==old_members[index],
                    'non-target member differs')
    row=profile['members'][target];start=row['offset'];end=start+row['bytes']
    require(old[:start]==new[:start] and old[end:-16]==new[end:-16],'unapproved body change')
    require(old[-14:]==new[-14:],'reserved body tail changed')
    require(len(old_members[target])==main_identity[0]+2 and sha(old_members[target][:-2])==main_identity[1],'original MAIN identity differs')
    check_main(old_members[target][:-2],new_members[target][:-2],recipe)
    # CTR keeps unchanged plaintext positions unchanged in ciphertext too.
    b=HEADER_BYTES
    require(original[b:b+start]==candidate[b:b+start] and
            original[b+end:-16]==candidate[b+end:-16] and original[-14:]==candidate[-14:],
            'ciphertext changed outside MAIN and body CRC')
    return dict(schema=1,target='Z6III C2.01',recipe=recipe,status='DO_NOT_FLASH',passed=True,
        original_sha256=sha(original),package_sha256=sha(candidate),body_sha256=sha(new),
        main_sha256=sha(new_members[target][:-2]),package_bytes=len(candidate),members=len(new_members),
        member_crcs=True,body_crc=True,header_digest=True,unchanged_non_target_members=True,
        unchanged_header_fields=True,header_tail_from_digest=True,unchanged_directory=True,unchanged_reserved_tail=True,
        independent_frozen_main_recipe=True,native_admission=False,programming=False,
        recovery=False,physical_acceptance=False)
