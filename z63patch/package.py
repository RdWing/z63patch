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
"""Equal-length C2.01 packaging from the original package and local materials.

This module uses the documented container fields and standard AES-CTR API.
It includes no third-party loader implementation, keys or firmware bytes.

Tests may pass a synthetic profile and a toy cipher (`profile=`, `cipher=`, `decipher=`); the
real path always uses the pinned profile and AES-256-CTR.
"""
import binascii
import hashlib
import json
from pathlib import Path
import struct
from .image import MAIN_BYTES,MAIN_SHA256,require,sha
from .package_contract import verify_main
from .package_keys import Material

PROFILE=Path(__file__).resolve().parents[1]/'profiles/z6iii-c201-package.json'
HEADER_BYTES=608
DIGEST=(576,596)
TAIL=(596,608)


def header_tail(digest):
    """The 12 header bytes after the digest, as every official package has them: a linear congruential generator seeded with the first digest byte, outputs 21 to 32."""
    state=digest[0];out=bytearray()
    for _ in range(32):
        state=(214013*state+2531011)&0xFFFFFFFF
        out.append((state>>16)&0xFF)
    return bytes(out[20:32])


def load_profile():
    return json.loads(PROFILE.read_text())


def transform(body,material):
    from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
    require(len(material.key)==32 and len(material.iv)==16 and len(material.suffix)==8,'invalid material extent')
    cipher=Cipher(algorithms.AES256(material.key),modes.CTR(material.iv)).encryptor()
    result=bytearray(cipher.update(body)+cipher.finalize())
    result[:32]=body[:32]
    return result


def check_identity(raw,profile=None):
    """The user's file must be exactly the stock Z6III C2.01 update file; checked before any key
    material is read."""
    profile=profile or load_profile()
    require(len(raw)==profile['package_bytes'] and sha(raw)==profile['original_sha256'],
            f"input is not the stock Nikon Z6III C2.01 update file (Z6_3_0201.bin, {profile['package_bytes']:,} bytes, "
            f"SHA-256 {profile['original_sha256']}); other models, versions and modified files are not supported")
    return profile


def suffix_check(raw,cipher=transform):
    """For `package_keys.load_material`: given the key and counter block, return a predicate that
    is true for the 8-byte digest suffix, the one value for which SHA-1(clear body + suffix) equals
    the stock file's header digest. No fingerprint of the suffix is stored (a 64-bit hash could be
    reversed); the stock file is the reference. The body is hashed once; each candidate costs one
    SHA-1 finalisation."""
    def for_key(key,iv):
        body=cipher(raw[HEADER_BYTES:],Material(key,iv,bytes(8)))
        state=hashlib.sha1(body);want=bytes(raw[DIGEST[0]:DIGEST[1]])
        def matches(candidate):
            h=state.copy();h.update(candidate);return h.digest()==want
        return matches
    return for_key


def checked_original(raw,material,*,profile=None,cipher=transform,main_identity=(MAIN_BYTES,MAIN_SHA256)):
    profile=check_identity(raw,profile)
    target=profile.get('target_index',2)
    require(raw[544:576]==raw[:32][::-1],'outer mirror differs')
    # The tail rule is checked on the user's own stock file before it is used for the output.
    require(raw[TAIL[0]:TAIL[1]]==header_tail(raw[DIGEST[0]:DIGEST[1]]),
            'the stock header tail does not follow the digest rule; refusing to regenerate it')
    body=cipher(raw[HEADER_BYTES:],material)
    # A wrong key decrypts to other bytes: this is the key check, and it reveals nothing of the key.
    require(len(body)==profile['body_bytes'] and sha(body)==profile['body_sha256'],
            'the key material does not decrypt this file to the known stock C2.01 body (wrong or damaged key file)')
    require(body[:32]==b' '*32 and body[-14:]==bytes(14),'original body framing differs')
    n=len(profile['members'])
    require(struct.unpack_from('>4I',body,32)==(n,48+32*n,0,0),'original directory differs')
    require(hashlib.sha1(body+material.suffix).digest()==raw[DIGEST[0]:DIGEST[1]],
            'original body digest differs (wrong digest suffix in the key file)')
    end=48+32*n
    for index,row in enumerate(profile['members']):
        name,start,size,r0,r1=struct.unpack_from('>16s4I',body,48+32*index)
        require((name,start,size,r0,r1)==(row['name'].encode().ljust(16,b'\0'),row['offset'],row['bytes'],0,0),'original member directory differs')
        require(start==end and start+size<=len(body)-16,'original member extent differs')
        member=body[start:start+size]
        require(sha(member)==row['member_sha256'] and sha(member[:-2])==row['payload_sha256'],'original member identity differs')
        require(binascii.crc_hqx(member,0)==0,'original member CRC differs')
        end+=size
    require(end==len(body)-16 and binascii.crc_hqx(body[:-14],0)==0,'original body CRC differs')
    main=profile['members'][target]
    require(main['bytes']==main_identity[0]+2 and main['payload_sha256']==main_identity[1],'MAIN profile identity differs')
    return profile,body


def stock_main(profile,body):
    row=profile['members'][profile.get('target_index',2)]
    return bytes(body[row['offset']:row['offset']+row['bytes']-2])


def reseal(raw,profile,body,candidate_main,material,cipher=transform):
    """The package with MAIN replaced. Recomputed: MAIN's member CRC, the body CRC, the 20-byte
    header digest and the 12-byte tail derived from it. Every other header byte (prefix, RSA
    slots, mirror), the directory, the other members and the reserved body tail are kept."""
    row=profile['members'][profile.get('target_index',2)];start=row['offset'];end=start+row['bytes']
    require(len(candidate_main)==end-start-2,'candidate MAIN length differs')
    body=bytearray(body)
    body[start:end-2]=candidate_main
    body[end-2:end]=binascii.crc_hqx(candidate_main,0).to_bytes(2,'big')
    body[-16:-14]=binascii.crc_hqx(body[:-16],0).to_bytes(2,'big')
    header=bytearray(raw[:HEADER_BYTES])
    header[DIGEST[0]:DIGEST[1]]=hashlib.sha1(body+material.suffix).digest()
    header[TAIL[0]:TAIL[1]]=header_tail(header[DIGEST[0]:DIGEST[1]])
    return bytes(header+cipher(body,material))


def build(raw,candidate_main,material,recipe,*,profile=None,cipher=transform,decipher=None,
          main_identity=(MAIN_BYTES,MAIN_SHA256),check_main=verify_main):
    profile,body=checked_original(raw,material,profile=profile,cipher=cipher,main_identity=main_identity)
    check_main(stock_main(profile,body),candidate_main,recipe)
    packet=reseal(raw,profile,body,candidate_main,material,cipher)
    # The separate verifier does not call this module or consume build metadata.
    from . import package_verify as PV
    report=PV.verify_package(raw,packet,material,recipe,profile=profile,decipher=decipher or PV.aes_decipher,
                             main_identity=main_identity,check_main=check_main)
    require(report['main_sha256']==sha(candidate_main),'packaged MAIN differs')
    return packet,report
