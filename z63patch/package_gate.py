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
"""Package the single deliverable gate recipe from the user's own inputs.

Chain: stock C2.01 update file (identified by size and SHA-256 before any key is read) + the
user's key file -> decrypt in memory -> exact stock C2.01 body and MAIN (pinned hashes) -> the
all-groups gate image (stage s7-all, built from that MAIN; equal to the reviewed recipe) ->
verify_gate and the packager interlocks below -> reseal into the five-member package (MAIN
member CRC, body CRC and header digest recomputed; everything else kept) -> self-check (the
package is opened again independently, MAIN re-extracted and compared with the built image,
every other member compared byte for byte with stock). Every output is DO_NOT_FLASH.

Interlocks (check_gate_main), each refusing the package:
  1. verify_gate accepts the image against the reviewed recipe (pinned: output hash, edits, code);
  2. flash rule: the builder's flashed set (gate.FLASHED_SET: guard, hook word, patch range with
     its header) equals the verifier's, and every changed byte lies inside it;
  3. inert until armed: the anchor is stock (zero), the hook word is `bl guard`, the guard is the
     rebuilt v2 guard (verify_gate), and every install-table site holds its stock value in the
     image (no record pre-installed);
  4. update deny: the two deny words are stock in the flashed image and no byte of the
     protected update ranges differs from stock;
  5. start-up inputs: MAIN's first 128 KiB and one further pinned range equal stock; the other
     four members are byte-identical with stock (package_verify).
"""
from .image import MAIN_BYTES,MAIN_SHA256,require,sha

STAGE='s7-all'
RECIPE='arm-time-all'
MAIN_BASE=0x40160000
FIRST_BLOCK=0x20000
LOADER_CALLBACK=(0x41801000,0x41801370)
DENY_WORDS=(0x410143EC,0x4101442C)
UPDATE_TEXT=((0x41013000,0x4101E300,'protected update range'),(0x40CA8000,0x40CA9000,'protected update range'),
             (0x455CB000,0x455CE000,'protected update range'))
WIDTH={'code32':4,'data64':8,'page64':8,'data16':2}
_CACHE={}


def flash_rule(stock,candidate):
    """Interlock 2: changed bytes only inside the gate's own flash rule (guard, hook word, patch
    range with its header). Returns the per-range table and the run count."""
    from . import gate
    from . import verify_gate as VG
    from . import verify_arm_time as V
    verifier_set=((V.GUARD,V.GUARD_END),(V.HOOK,V.HOOK+4),(VG.LO,VG.HI))
    require(tuple(map(tuple,gate.FLASHED_SET))==verifier_set,'the builder flashed set differs from the verifier flashed set')
    ranges=[dict(name=n,start=hex(a),end=hex(b),changed_bytes=0) for n,(a,b) in zip(('guard','hook word','patch range (header included)'),verifier_set)]
    runs=V.diff_runs(stock,candidate)
    for a,b in runs:
        va,vb=a+MAIN_BASE,b+MAIN_BASE
        hit=[r for r,(x,y) in zip(ranges,verifier_set) if x<=va and vb<=y]
        require(hit,f'changed bytes outside the flashed set at {va:#x}')
        hit[0]['changed_bytes']+=b-a
    return ranges,runs


def inert_until_armed(stock,candidate,recipe):
    """Interlock 3: the anchor is stock zero and no install site is pre-installed in the image."""
    from . import arm_time as A
    from . import verify_arm_time as V
    o=V.off(A.ANCHOR)
    require(candidate[o:o+16]==stock[o:o+16]==bytes(16),'anchor is not stock zero')
    in_image=ram_only=0
    for r in recipe['install_table']['records']:
        at=int(r['address'],16);w=WIDTH[r['kind']]
        try:
            o=V.off(at)
        except ValueError:
            ram_only+=1;continue
        require(candidate[o:o+w]==stock[o:o+w],f'install site {at:#x} is pre-installed in the image')
        in_image+=1
    return in_image,ram_only


def update_deny_stock(stock,candidate):
    """Interlock 4: the deny words and the protected update ranges are stock in the image,
    so an update works untouched in any session that never armed."""
    from . import verify_arm_time as V
    for at in DENY_WORDS:
        require(V.word(candidate,at)==V.word(stock,at),f'update deny word {at:#x} is not stock in the image')
    for a,b,what in UPDATE_TEXT:
        require(candidate[a-MAIN_BASE:b-MAIN_BASE]==stock[a-MAIN_BASE:b-MAIN_BASE],f'{what} {a:#x}-{b:#x} differs from stock')


def loader_inputs_stock(stock,candidate):
    """Interlock 5: what start-up reads of MAIN is stock (the first 128 KiB and one pinned range)."""
    require(candidate[:FIRST_BLOCK]==stock[:FIRST_BLOCK],'MAIN first block differs from stock')
    a,b=LOADER_CALLBACK
    require(candidate[a-MAIN_BASE:b-MAIN_BASE]==stock[a-MAIN_BASE:b-MAIN_BASE],'start-up range differs from stock')


def check_gate_main(stock,candidate,stage=STAGE):
    """Dict of the interlocks passed; raises ValueError on the first failure."""
    key=(sha(stock),sha(candidate),stage)
    if key in _CACHE:
        return _CACHE[key]
    from . import verify_gate as VG
    require(len(stock)==MAIN_BYTES and sha(stock)==MAIN_SHA256,'stock C2.01 MAIN required')
    require(len(candidate)==len(stock),'candidate MAIN length differs')
    out=VG.verify(stock,candidate,stage)                      # pinned to profiles/...-<stage>-recipe.json
    require(out.get('accepted') is True,'verify_gate did not accept the image')
    recipe=VG.recipe(stage)
    ranges,runs=flash_rule(stock,candidate)
    in_image,ram_only=inert_until_armed(stock,candidate,recipe)
    update_deny_stock(stock,candidate)
    loader_inputs_stock(stock,candidate)
    require(out['install_table']['records']==len(recipe['install_table']['records']),'record count differs from the recipe')
    result=dict(stage=stage,image_sha256=sha(candidate),recipe_output_sha256=recipe['output_sha256'],
                records=out['install_table']['records'],groups=hex(out['groups']),composition=out['composition'],
                changed_bytes=sum(b-a for a,b in runs),changed_runs=len(runs),changed_ranges=ranges,
                checks=dict(verify_gate_accepted=True,changed_bytes_inside_flash_rule=True,
                            anchor_stock=True,install_sites_stock_in_image=in_image,install_sites_ram_only=ram_only,
                            update_deny_words_stock=True,update_code_stock=True,
                            first_block_stock=True,start_up_range_stock=True))
    _CACHE[key]=result
    return result


def build_main(stock_main,compiler='clang'):
    """The reviewed all-groups image built from the user's MAIN (assets derived at build time)."""
    from . import gate
    from . import verify_gate as VG
    data,manifest=gate.build(stock_main,STAGE,compiler)
    expected=VG.recipe(STAGE)
    got=gate.recipe(manifest)
    require(got['output_sha256']==expected['output_sha256'] and got['edits']==expected['edits'] and
            got['stock_digest']==expected['stock_digest'],'the built image differs from the reviewed recipe')
    return data


def build_gate_package(raw,material,*,profile=None,cipher=None,decipher=None,main_identity=(MAIN_BYTES,MAIN_SHA256),
                       builder=None,check_main=None,recipe=RECIPE):
    """(package bytes, manifest). Everything stays in memory."""
    from . import package as P
    from . import package_verify as PV
    from .package_contract import verify_main
    cipher=cipher or P.transform
    decipher=decipher or PV.aes_decipher
    real=check_main is None
    check_main=check_main or verify_main
    profile,body=P.checked_original(raw,material,profile=profile,cipher=cipher,main_identity=main_identity)
    stock=P.stock_main(profile,body)
    image=(builder or build_main)(stock)
    packet,report=P.build(raw,image,material,recipe,profile=profile,cipher=cipher,decipher=decipher,
                          main_identity=main_identity,check_main=check_main)
    # Self-check: open the package again, re-extract every member and compare.
    _,old=PV.open_package(raw,material,profile,decipher)
    new_body,new=PV.open_package(packet,material,profile,decipher)
    target=profile.get('target_index',2)
    require(new[target][:-2]==image,'re-extracted MAIN differs from the built image')
    for i in range(len(new)):
        require(i==target or new[i]==old[i],f"member {profile['members'][i]['name']} differs from stock")
    gate_report=check_gate_main(stock,image,STAGE) if real else check_main(stock,image,recipe)   # cached: no second run
    members=[]
    for i,row in enumerate(profile['members']):
        members.append(dict(name=row['name'],offset=row['offset'],bytes=row['bytes'],stock_sha256=sha(old[i]),
                            output_sha256=sha(new[i]),crc16=new[i][-2:].hex(),
                            role='MAIN (replaced)' if i==target else 'byte-identical with stock'))
    expected=profile.get('gate_package',{}).get(recipe)
    if expected:
        # Reproducibility: the same input and recipe give these exact results on every machine.
        require(sha(image)==expected['main_sha256'] and sha(new[target])==expected['main_member_sha256'] and
                sha(new_body)==expected['body_sha256'],'result differs from the expected result for stock C2.01')
        require(expected.get('output_sha256') in (None,sha(packet)),'package differs from the expected package for stock C2.01')
    manifest=dict(schema=1,status='DO_NOT_FLASH',target='Z6III C2.01',recipe=recipe,stage=STAGE,
                  input_sha256=sha(raw),input_bytes=len(raw),output_sha256=sha(packet),output_bytes=len(packet),
                  stock_main_sha256=sha(stock),main_sha256=sha(image),body_sha256=sha(new_body),
                  header_digest_recomputed=True,header_tail_regenerated=True,header_bytes_kept='0x000-0x240 (prefix, RSA slots, mirror)',
                  expected_output_sha256=(expected or {}).get('output_sha256'),
                  members=members,
                  gate=gate_report if isinstance(gate_report,dict) else None,
                  checks=dict(input_identity=True,key_decrypts_to_stock_body=True,stock_main_identity=True,
                              built_image_equals_reviewed_recipe=True,interlocks=True,package_verifier=report['passed'],
                              self_check_main_reextracted=True,self_check_other_members_identical=True),
                  qualification=dict(native_admission=False,programming=False,boot=False,recovery=False,physical_acceptance=False))
    return packet,manifest
