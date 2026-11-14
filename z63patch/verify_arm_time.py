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
"""Independent verification of an arm-time safety-gate candidate (stages S1-S5).

Written separately from z63patch/arm_time.py and imports nothing from it. It checks:

 1. the stock input is exact C2.01 and the candidate has the same length;
 2. candidate minus stock is confined to the regions the stage declares (S1: the header;
    S2: header, guard, hook word, the 20-byte entry; S3: header, guard, hook word, code
    region, one install record) and the reviewed recipe's edit list and output hash;
 3. the header is recomputed: magic, version, build ID (SHA-256 prefix) and CRC-32 of
    [0x401A0000, 0x401FFF00), entry, table, reserved bytes;
 4. the guard is rebuilt here from the header byte for byte, then decoded: every branch
    goes to the original poll, to the header's entry or inside the guard; no store; loads
    only from the detector byte, the header page and its own literals;
 5. the hook word is the only changed instruction outside the patch range and the guard
    (bl guard at 0x4032768C, stock bl 0x403277C0);
 6. S2: the entry is exactly stp/mov/bl 0x403277C0/ldp/ret;
 7. S3: the code region has the reviewed hash; the install record's old value equals the
    stock data word; every absolute code address the code builds is on an allowlist; no
    system-register write, interrupt mask, barrier-free cache or TLB operation, exception
    call or wait instruction;
 8. S5 (verify_s5): the changed bytes are the header, the guard, the hook word, the code and
    the install table only; every record is re-derived from the stock image (class range,
    stock value at the address, order, completeness of the re-sited call sites) and its new
    value must lead to a thunk of the fixed reviewed shape that calls exactly the stock
    function the record replaces; the calls of the code are on the S5 allowlist;
 9. S5M (verify_s5 with the marker): as S5, plus the group field of every record (0 for the
    infrastructure records, 1 only for the marker record), the build's group mask (3: both
    groups; the infrastructure group must be present wherever the marker is), and the marker
    record under the S3 rules: kind 1, class 'M', last in the table, the video menu refresh
    handler with its stock value, new value inside the code and equal to the reviewed recipe's;
    the marker's four menu calls are added to the allowlist;
10. S6 (verify_s6): the S5M checks on install table v2 (0x401F6000, 40-byte records: u16
    contributor-group mask, u8 set, u8 rank, u8 wrapper class, u8 flags, u8 TX pins, reserved
    zero), plus: the coupling-set rule (set 0 is no set; ranks rise in table order inside a set)
    and the reviewed set, rank, wrapper-class and restore-under-Q values of every record (P rank 1
    and the marker handler rank 4 form set 1); no TX pin bits and no all-or-none flag (nothing in
    this build implements either); one record per word; the old S5 table slot 0x401FFC00 empty;
    the guard words and the hook word as in S5M; and the wrapper rules
    checked on the code itself: from every wrapper entry (the thunks, the
    marker adapter, and the admission API) the reachable code is walked; (a) the first access
    outside the stack in the gate helpers and the adapter is the acquire load of the publication
    word; (b) the helpers store nothing outside the stack except by exclusive store, the adapter
    nothing at all; (c) every loop is a bounded exclusive load/store loop (mov wT,#N <= 64;
    subs/b.ne), and every exclusive store anywhere in the code sits in such a loop; (d) no
    system-register write, no indirect jump, and only the allowed stock calls (a thunk: its own
    stock function only; the helpers and the admission API: none); (e) a stack bound per wrapper;
11. the recipe's stock digest, recomputed from this stock image and the
    candidate's install table: the recipe holds no stock word and no hash of stock bytes.
"""
import hashlib
import json
import struct
import zlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BASE=0x40160000
DATA_OFFSET,DATA_VA,DATA_BYTES=0x5095000,0xA02808000,0x3D9028
STOCK_SHA256='236bbd874e0942fd91fb54a99af456f4ebcc0fd966af930753c43efd1839fe87'
LO,HDR,HI=0x401A0000,0x401FFF00,0x40200000
GUARD,GUARD_END=0x40194F00,0x40194F70
HOOK,POLL=0x4032768C,0x403277C0
TABLE=0x401FFE00
POINTER=0xA028F5600
DETECTOR=0xA0305DAA5
STAGES=('s1','s2','s3','s4','s5','s5m','s6')
CODE_SITE=0x4059652C   # S4: bl TEXT_BIND (0x4040A1AC) in the video menu refresh
# Absolute code addresses S3 may build (functions it calls, words it reads or writes).
S3_ALLOWED_CODE={CODE_SITE:'S4 code site',POLL:'original card poll',HOOK:'entry hook site',GUARD:'guard',0x40194FF0:'anchor',
    LO:'patch range start',HDR:'header',TABLE:'install table',0x40596140:'stock video refresh',
    0x4052C900:'ROW_VALUE',0x404066C8:'TEXT_STORE',0x4040A1AC:'TEXT_BIND',
    0x402E3674:'FS_OPEN',0x402E3774:'FS_READ',0x402E36D4:'FS_CLOSE'}


# The only stock functions S3 may call.
S3_CALLS={POLL:'original card poll',0x40596140:'stock video refresh',0x4052C900:'ROW_VALUE',
    0x404066C8:'TEXT_STORE',0x4040A1AC:'TEXT_BIND',0x402E3674:'FS_OPEN',0x402E3774:'FS_READ',0x402E36D4:'FS_CLOSE'}


# ---- S5: infrastructure armed, features off ----
S5_TABLE=0x401FFC00
S5_MAX_RECORDS=24
ITEMS_POINTER=0xA028CAE20          # video menu page +0x10: the state page publication
REGISTER=0x40C00204                # dispatcher registration: stores x1 at [x0+0x48]
S5_TEXT=((0x40200000,0x4090EBE8),(0x40C00000,0x416692F8))   # W1, W2: text copied to RAM
# Hook rules: no installed word in these.
S5_DENY=((0x40CA8000,0x40CA9000,'protected range'),
         (0x41189C00,0x41189E00,'protected range'))
# Class S: stock functions whose every caller may be re-sited to a thunk.
S5_SITE_TARGETS={0x40E80C74:'pipeline drain',0x40E7FD08:'pipeline configure'}
# Class R: dispatcher callback words (object + 0x48) that may be swapped.
S5_CALLBACK_WORDS={0xA048BAFE0:'capture dispatcher callback',0xA03324BB0:'movie dispatcher callback'}
# The only stock functions S5 may call. New against S3: the stock heap (allocate, its lock's
# try and release entries) and the four functions the thunks stand in front of. The menu
# calls of the S3/S4 trial marker are not allowed here.
S5_CALLS={POLL:'original card poll',0x402E3674:'FS_OPEN',0x402E3774:'FS_READ',0x402E36D4:'FS_CLOSE',
    0x40170D08:'HEAP_ALLOC (heap 0xA04EE71E0)',0x40172F0C:'heap lock, try',0x40172F10:'heap lock, release',
    0x40755A54:'stock capture dispatch',0x40D22770:'stock movie dispatch',
    0x40E80C74:'stock pipeline drain',0x40E7FD08:'stock pipeline configure'}
S5_ALLOWED_CODE={**S5_CALLS,HOOK:'entry hook site',GUARD:'guard',0x40194FF0:'anchor',LO:'patch range start',
    HDR:'header',S5_TABLE:'install table',0x40200000:'copied text start',0x41800000:'copied text end'}
# S5M: the marker group (S3's menu calls, for z63_marker_refresh only) and the allowed masks.
S5M_CALLS={**S5_CALLS,0x40596140:'stock video refresh',0x4052C900:'ROW_VALUE',0x404066C8:'TEXT_STORE',0x4040A1AC:'TEXT_BIND'}
S5M_ALLOWED_CODE={**S5_ALLOWED_CODE,**S5M_CALLS}
MARKER_GROUP=1
GROUP_MASKS={'s5':1,'s5m':3,'s6':3}       # the build's groups; an arm file may select 1, or 3 (S5M)
# The thunk, word for word; index (0..3) and the two halves of the stock address vary.
S5_THUNK=('a9b97bfd 910003fd a90107e0 a9020fe2 a90317e4 a9041fe6 f9002be8 MOVI BL f9002fe0 '
          'a94107e0 a9420fe2 a94317e4 a9441fe6 f9402be8 MOVZ MOVK d63f0200 f9402fe9 b40000c9 '
          'a90107e0 aa0903e0 MOV1 BL a94107e0 a8c77bfd d65f03c0').split()


class Reject(ValueError):pass


def need(ok,why):
    if not ok:raise Reject(why)


def off(va):
    if BASE<=va<BASE+DATA_OFFSET:return va-BASE
    if DATA_VA<=va<DATA_VA+DATA_BYTES:return va-DATA_VA+DATA_OFFSET
    raise Reject(f'unmapped {va:#x}')


def word(data,va):return struct.unpack_from('<I',data,off(va))[0]


def recipe(stage):return json.loads((ROOT/'profiles'/f'z6iii-c201-arm-time-{stage}-recipe.json').read_text())


def diff_runs(a,b):
    runs=[];i=0;n=len(a)
    # Compare in large blocks; most of the image is equal.
    step=1<<16
    while i<n:
        j=min(n,i+step)
        if a[i:j]!=b[i:j]:
            for k in range(i,j):
                if a[k]!=b[k]:
                    if runs and runs[-1][1]==k:runs[-1][1]=k+1
                    else:runs.append([k,k+1])
        i=j
    return runs


def sx(v,bits):return v-(1<<bits) if v&(1<<(bits-1)) else v


def expected_guard(entry,build_id):
    """Rebuilt from fixed reviewed words; only the two literals and the entry branch vary."""
    fixed=('d29b54b0 f2a060b0 f2c00150 39400211 7100063f 54000221 f0000350 f9478211 '
           '58000210 eb11021f 54000181 f0000350 f9478611 580001b0 eb11021f 540000e1 '
           'd53be030 d53be011 d37cee31 eb11021f 54000043').split()
    words=[int(x,16) for x in fixed]
    at=GUARD+4*len(words)
    words.append(0x14000000|(((entry-at)>>2)&0x3FFFFFF))       # b z63_entry
    words.append(0x14000000|(((POLL-(at+4))>>2)&0x3FFFFFF))     # b original poll
    words.append(0xD503201F)                                     # nop
    return b''.join(struct.pack('<I',w) for w in words)+b'Z63GATE1'+struct.pack('<Q',build_id)



def expected_guard_v2(entry,build_id):
    """Guard version 2 (s7 family): words 16-20 (the 16 s counter test) become the update
    client test movz/movk/movk x16 = 0xA04073C70, ldr x17,[x16], cbnz x17,<stock poll branch>.
    Rebuilt from fixed reviewed words."""
    g=bytearray(expected_guard(entry,build_id))
    for k,w in enumerate((0xD2878E10,0xF2A080F0,0xF2C00150,0xF9400211,0xB5000051)):
        struct.pack_into('<I',g,4*(16+k),w)
    return bytes(g)


def decode_guard(g):
    """Small decoder: branch targets, loads (with the x16 value tracked), and no stores."""
    x16=None;targets=set();loads=[]
    for i in range(0,96,4):
        w=struct.unpack_from('<I',g,i)[0];pc=GUARD+i
        if w&0xFF800000==0xD2800000:x16=((w>>5)&0xFFFF)<<(16*((w>>21)&3)) if w&31==16 else x16
        elif w&0xFF800000==0xF2800000 and w&31==16:
            hw=(w>>21)&3;x16=(x16&~(0xFFFF<<(16*hw)))|(((w>>5)&0xFFFF)<<(16*hw))
        elif w&0x9F000000==0x90000000:
            imm=sx(((w>>5)&0x7FFFF)<<2|((w>>29)&3),21)
            if w&31==16:x16=(pc&~0xFFF)+(imm<<12)
        elif w&0xFFC00000==0x39400000:loads.append(('ldrb',x16+((w>>10)&0xFFF)))
        elif w&0xFFC00000==0xF9400000:loads.append(('ldr',(x16 if x16 is not None else -1)+8*((w>>10)&0xFFF)))
        elif w&0xFF000000==0x58000000:
            loads.append(('literal',pc+4*sx((w>>5)&0x7FFFF,19)))
            if w&31==16:x16=None
        elif w&0xFC000000==0x14000000:targets.add(pc+4*sx(w&0x3FFFFFF,26))
        elif w&0xFF000010==0x54000000:targets.add(pc+4*sx((w>>5)&0x7FFFF,19))
        elif w&0xFF00001F==0xB5000011:targets.add(pc+4*sx((w>>5)&0x7FFFF,19))   # cbnz x17 (guard v2)
        elif w in (0xEB11021F,0xD37CEE31,0xD503201F) or w&0xFFFFFFE0 in (0xD53BE020,0xD53BE000) or w&0xFF00001F==0x7100001F:pass
        else:raise Reject(f'unexpected guard instruction {w:08x} at {pc:#x}')
        # stores: STR/STP/STUR/atomics all have bit 27 set and bit 25 clear with L (bit 22) clear
        need(w&0xFF000000==0x58000000 or not (w&0x0A000000==0x08000000 and not w&(1<<22)),f'store in guard at {pc:#x}')
    return targets,loads


def scan_code(code,base):
    """S3: constants built by MOVZ/MOVK/ADRP, system instructions."""
    regs={};constants=set();problems=[];calls=[]
    for i in range(0,len(code)-3,4):
        w=struct.unpack_from('<I',code,i)[0];pc=base+i;rd=w&31
        if w&0x7F800000==0x52800000:regs[rd]=((w>>5)&0xFFFF)<<(16*((w>>21)&3))              # movz
        elif w&0x7F800000==0x72800000:                                                        # movk
            hw=(w>>21)&3;regs[rd]=(regs.get(rd,0)&~(0xFFFF<<(16*hw)))|(((w>>5)&0xFFFF)<<(16*hw))
        elif w&0x9F000000==0x90000000:regs[rd]=(pc&~0xFFF)+(sx(((w>>5)&0x7FFFF)<<2|((w>>29)&3),21)<<12)
        elif w&0x9F000000==0x10000000:regs[rd]=pc+sx(((w>>5)&0x7FFFF)<<2|((w>>29)&3),21)
        elif w&0x7FC00000==0x11000000 and ((w>>5)&31) in regs:regs[rd]=regs[(w>>5)&31]+((w>>10)&0xFFF)  # add imm (32/64)
        elif w&0xFFFFFC1F==0xD63F0000:                                                       # blr
            calls.append((pc,regs.get((w>>5)&31)));continue
        elif w&0xFC000000==0x94000000:calls.append((pc,pc+4*sx(w&0x3FFFFFF,26)));continue    # bl
        elif w&0xFFFFFC1F==0xD61F0000:problems.append((hex(pc),'br (indirect jump)'));continue
        else:
            if rd in regs:del regs[rd]
            continue
        constants.add(regs[rd])
    for i in range(0,len(code)-3,4):
        w=struct.unpack_from('<I',code,i)[0];pc=base+i
        if w&0xFFF00000==0xD5100000:problems.append((hex(pc),'msr (system register write)'))
        if w&0xFFF8F01F==0xD500401F:problems.append((hex(pc),'msr immediate (PSTATE, DAIF)'))
        if w&0xFFE0001F in (0xD4000001,0xD4000002,0xD4000003):problems.append((hex(pc),'svc/hvc/smc'))
        if w in (0xD503207F,0xD503205F,0xD503203F):problems.append((hex(pc),'wfi/wfe/yield'))
        if w&0xFFF8F000==0xD5088000:problems.append((hex(pc),'tlbi'))
        if w&0xFFF80000==0xD5080000 and w&0xFFFFFFE0 not in (0xD50B7E20,0xD50B7520):
            problems.append((hex(pc),'cache operation other than DC CIVAC / IC IVAU'))
    code_constants={c for c in constants if 0x40160000<=c<0x41847000}
    return code_constants,problems,calls


def decode_thunk(code,base,at):
    """The stock function a thunk at `at` calls, its index and its two helper calls."""
    need(base<=at and at+4*len(S5_THUNK)<=base+len(code) and at%4==0,f'thunk outside the code at {at:#x}')
    index=None;target=0;helpers=[]
    for k,shape in enumerate(S5_THUNK):
        pc=at+4*k;w=struct.unpack_from('<I',code,pc-base)[0]
        if shape=='MOVI':
            need(w&0xFFFFFF9F==0x52800000,f'thunk index at {pc:#x}');index=(w>>5)&3
        elif shape=='MOV1':
            need(w&0xFFFFFF9F==0x52800001 and (w>>5)&3==index,f'thunk leave index at {pc:#x}')
        elif shape=='MOVZ':
            need(w&0xFFE0001F==0xD2800010,f'thunk address low half at {pc:#x}');target=(w>>5)&0xFFFF
        elif shape=='MOVK':
            need(w&0xFFE0001F==0xF2A00010,f'thunk address high half at {pc:#x}');target|=((w>>5)&0xFFFF)<<16
        elif shape=='BL':
            need(w>>26==0x25,f'thunk helper call at {pc:#x}');helpers.append(pc+4*sx(w&0x3FFFFFF,26))
        else:need(w==int(shape,16),f'thunk word differs at {pc:#x}')
    need(all(base<=h<base+len(code) for h in helpers) and helpers[0]!=helpers[1],'thunk helper outside the code')
    return target,index,tuple(helpers)


def in_text(va):return va%4==0 and any(a<=va<b for a,b in S5_TEXT)


def stock_calls(stock,target):
    """Every bl to target in the copied text of the stock image."""
    out=[]
    for a,b in S5_TEXT:
        words=struct.unpack_from(f'<{(b-a)//4}I',stock,off(a))
        out+=[a+4*i for i,w in enumerate(words) if w>>26==0x25 and a+4*i+4*sx(w&0x3FFFFFF,26)==target]
    return out


def stock_registrations(stock):
    """{object + 0x48: callback} for every stock dispatcher registration whose x0 and x1 are
    built by movz/movk just before the call."""
    found={}
    for call in stock_calls(stock,REGISTER):
        regs={}
        for pc in range(call-48,call,4):
            w=word(stock,pc);rd=w&31
            if w&0xFF800000==0xD2800000:regs[rd]=((w>>5)&0xFFFF)<<(16*((w>>21)&3))
            elif w&0xFF800000==0xF2800000 and rd in regs:
                hw=(w>>21)&3;regs[rd]=(regs[rd]&~(0xFFFF<<(16*hw)))|(((w>>5)&0xFFFF)<<(16*hw))
            elif rd in regs and w>>26 not in (0x25,0x05):del regs[rd]
        if 0 in regs and 1 in regs:found[regs[0]+0x48]=regs[1]
    return found


def verify_s5(stock,candidate,r,pinned,entry,count,allowed,out,stage='s5'):
    marker=stage=='s5m'
    calls_allowed=S5M_CALLS if marker else S5_CALLS
    code_allowed=S5M_ALLOWED_CODE if marker else S5_ALLOWED_CODE
    need(r.get('groups')==GROUP_MASKS[stage],'build group mask is not the reviewed one')
    need(1<=count<=S5_MAX_RECORDS and S5_TABLE+32*count<=HDR,'S5 record count')
    n=r['code']['bytes'];code=candidate[off(LO):off(LO)+n]
    need(0<n and LO+n<=S5_TABLE,'S5 code size')
    need(not pinned or hashlib.sha256(code).hexdigest()==r['code']['sha256'],'S5 code differs from the reviewed recipe')
    need(not pinned or count==len(r['install_table']['records']),'S5 record count differs from the reviewed recipe')
    need(not any(candidate[off(LO)+n:off(S5_TABLE)]),'bytes between code and table')
    need(not any(candidate[off(S5_TABLE)+32*count:off(HDR)]),'bytes after the install table')
    registrations=stock_registrations(stock)
    records=[];helpers=set();thunks={};sites={}
    for i in range(count):
        kind,cls,order,group,address,old,new=struct.unpack_from('<BBHIQQQ',candidate,off(S5_TABLE)+32*i)
        cls=chr(cls);where=f'record {i} at {address:#x}'
        # Install order is the table order and undo is its reverse: the order field must say so.
        need(order==i,f'{where}: order or group')
        if marker and group==MARKER_GROUP:
            # The marker (S3 rules): kind 1, class M, the refresh handler, its stock value, a new
            # value inside the code; last in the table so that disarm restores it first.
            need(kind==1 and cls=='M',f'{where}: marker record kind or class')
            need(i==count-1,f'{where}: marker record is not last')
            need(address==POINTER,f'{where}: marker record address')
            need(old==struct.unpack_from('<Q',stock,off(POINTER))[0]==0x40596140,f'{where}: marker old value is not the stock word')
            need(candidate[off(address):off(address)+8]==stock[off(address):off(address)+8],f'{where}: pre-installed in the flashed image')
            need(LO<new<LO+n and new%4==0,f'{where}: marker new value outside the code')
            need(not pinned or new==int(r['install_table']['records'][i]['new'],16),f'{where}: marker new value differs from the reviewed recipe')
            records.append((kind,cls,address,old,new,group))
            continue
        need(group==0,f'{where}: order or group')
        if kind==3:
            need(i==0 and cls=='D' and address==ITEMS_POINTER,f'{where}: the page record must be first, at the video rows pointer')
            need(old==struct.unpack_from('<Q',stock,off(address))[0]==0x43CB3868 and new==0,f'{where}: old value is not the stock word')
            need(candidate[off(address):off(address)+8]==stock[off(address):off(address)+8],f'{where}: pre-installed in the flashed image')
        elif kind==2:
            need(i>0 and cls=='S' and records[-1][0] in (3,2),f'{where}: code records follow the page record')
            need(in_text(address),f'{where}: outside the copied text windows')
            for a,b,what in S5_DENY:need(not a<=address<b,f'{where}: inside {what} {a:#x}-{b:#x}')
            stock_word=word(stock,address)
            need(old==stock_word and old>>26==0x25,f'{where}: old value is not the stock bl')
            target=address+4*sx(old&0x3FFFFFF,26)
            need(target in S5_SITE_TARGETS,f'{where}: the stock call does not go to a wrapped function')
            need(word(candidate,address)==stock_word,f'{where}: pre-installed in the flashed image')
            need(new>>26==0x25,f'{where}: new value is not a bl')
            thunk=address+4*sx(new&0x3FFFFFF,26)
            callee,index,h=decode_thunk(code,LO,thunk)
            need(callee==target,f'{where}: the thunk calls {callee:#x}, not the replaced {target:#x}')
            helpers.add(h);thunks[thunk]=(callee,index);sites.setdefault(target,[]).append(address)
        elif kind==1:
            need(i>0 and cls=='R',f'{where}: class')
            need(address in S5_CALLBACK_WORDS,f'{where}: not a dispatcher callback word')
            need(registrations.get(address)==old and in_text(old),f'{where}: old value is not the callback the stock code registers')
            callee,index,h=decode_thunk(code,LO,new)
            need(callee==old,f'{where}: the thunk calls {callee:#x}, not the replaced {old:#x}')
            helpers.add(h);thunks[new]=(callee,index)
        else:raise Reject(f'{where}: unknown kind {kind}')
        records.append((kind,cls,address,old,new,group))
    need(records and records[0][0]==3,'the page record is missing')
    need(len({a for _,_,a,_,_,_ in records})==count,'an address appears twice')
    # Groups: the record groups are exactly the build's mask; the infrastructure group is always there.
    groups=sum(1<<g for g in {g for *_,g in records})
    need(groups&1 and groups==GROUP_MASKS[stage],f'record groups {groups:#x} are not the build mask {GROUP_MASKS[stage]:#x}')
    need(not marker or sum(1 for *_,g in records if g==MARKER_GROUP)==1,'S5M needs exactly one marker record')
    # A wrapped function entry is replaced at every stock caller, or not at all.
    for target,got in sites.items():
        need(got==stock_calls(stock,target),f'call sites of {target:#x} are not exactly the stock callers')
    need(len(helpers)==1,'thunks use different helpers')
    need(len({i for _,i in thunks.values()})==len(thunks) and len({c for c,_ in thunks.values()})==len(thunks),'thunk index or callee reused')
    constants,problems,calls=scan_code(code,LO)
    # Every indirect call names its target in the two instructions before it.
    for i in range(0,len(code)-3,4):
        w=struct.unpack_from('<I',code,i)[0]
        if w&0xFFFFFC1F==0xD63F0000:
            a,b=struct.unpack_from('<2I',code,i-8) if i>=8 else (0,0)
            need(w==0xD63F0200 and a&0xFFE0001F==0xD2800010 and b&0xFFE0001F==0xF2A00010,f'indirect call without its address at {LO+i:#x}')
            need(((a>>5)&0xFFFF)|(((b>>5)&0xFFFF)<<16) in calls_allowed,f'indirect call outside the allowlist at {LO+i:#x}')
    external={t for _,t in calls if t is None or not LO<=t<LO+n}
    need(external<=set(calls_allowed),f'S5 calls outside the allowlist: {sorted(hex(t) if t else str(t) for t in external-set(calls_allowed))}')
    spans=((LO,LO+n),(HDR,HDR+64),(S5_TABLE,S5_TABLE+32*count),(0x40194FF0,0x40195000))
    stray=sorted(hex(c) for c in constants if not (c in code_allowed or any(x<=c<y for x,y in spans)))
    need(not stray,f'S5 builds code addresses outside the allowlist: {stray}')
    need(not problems,f'S5 forbidden instructions: {problems}')
    allowed+=[(off(LO),off(LO)+n),(off(S5_TABLE),off(S5_TABLE)+32*count)]
    out['code']=dict(bytes=n,sha256=hashlib.sha256(code).hexdigest(),code_constants=sorted(hex(c) for c in constants),
                     external_calls={hex(t):calls_allowed[t] for t in sorted(external)},calls=len(calls),
                     thunks={hex(t):dict(calls=hex(c),index=i) for t,(c,i) in sorted(thunks.items())})
    out['records']=[dict(order=i,kind={1:'data64',2:'code32',3:'page64'}[k],cls=c,group=g,address=hex(a),old=hex(o),new=hex(v)) for i,(k,c,a,o,v,g) in enumerate(records)]
    out['groups']=GROUP_MASKS[stage]


# ---- S6: lifecycle machinery ----
S6_RECORDS=12
S6_TABLE,S6_TABLE_END,S6_REC,S6_FMT,S6_MAX=0x401F6000,0x401FFC00,40,'<BBHHBBBBBBIQQQ',800
W_NONE,W_C,W_F=0,2,3
F_NEEDQ,F_ATOMIC=1,2
S6_WRAPPER_STACK=256          # 3.5(e): bytes of patch frames on any wrapper path (stock callees excluded)
S6_API=('z63_admit','z63_exit','z63_effective')


def _branch(w,pc):
    """(kind, target) of a control-flow instruction, else None. kind: b, bl, cond, ret, br, blr."""
    if w&0xFC000000==0x14000000:return 'b',pc+4*sx(w&0x3FFFFFF,26)
    if w&0xFC000000==0x94000000:return 'bl',pc+4*sx(w&0x3FFFFFF,26)
    if w&0xFF000010==0x54000000:return 'cond',pc+4*sx((w>>5)&0x7FFFF,19)
    if w&0x7E000000==0x34000000:return 'cond',pc+4*sx((w>>5)&0x7FFFF,19)       # cbz/cbnz
    if w&0x7E000000==0x36000000:return 'cond',pc+4*sx((w>>5)&0x3FFF,14)        # tbz/tbnz
    if w&0xFFFFFC1F==0xD65F0000:return 'ret',None
    if w&0xFFFFFC1F==0xD61F0000:return 'br',None
    if w&0xFFFFFC1F==0xD63F0000:return 'blr',None
    return None


def _is_store(w):
    return w&0x0A000000==0x08000000 and not w&(1<<22) and w&0x3B000000!=0x18000000


def _exclusive_store(w):return w&0x3FE00000==0x08000000      # stxr/stlxr (any size; not stlr, not pairs)


def _exclusive_load(w):return w&0x3FE00000==0x08400000       # ldxr/ldaxr


def _bounded_loop(code,base,at):
    """The bounded-loop shape at a backward b.ne at `at`: subs wT,wT,#1 before it, the head preceded
    by movz wT,#N (1 <= N <= 64), an exclusive load at the head and an exclusive store inside."""
    w=struct.unpack_from('<I',code,at-base)[0]
    if w&0xFF00001F!=0x54000001:return False                        # b.ne
    head=at+4*sx((w>>5)&0x7FFFF,19)
    if not base+4<=head<at:return False
    sub=struct.unpack_from('<I',code,at-4-base)[0];mov=struct.unpack_from('<I',code,head-4-base)[0]
    t=sub&31
    if sub!=0x71000400|(t<<5)|t:return False                          # subs wT,wT,#1
    if mov&0xFFE0001F!=0x52800000|t or not 1<=(mov>>5)&0xFFFF<=64:return False
    body=struct.unpack_from(f'<{(at-head)//4}I',code,head-base)
    return _exclusive_load(body[0]) and any(_exclusive_store(x) for x in body)


def _explore(code,base,entry):
    """Instructions of the function at `entry` (direct branches followed, calls not), its direct
    calls inside the code and its veneer calls out (movz/movk x16; blr x16), and problems."""
    n=len(code);seen=set();work=[entry];calls=set();external=set();problems=[];edges=[]
    while work:
        pc=work.pop()
        if pc in seen:continue
        if not base<=pc<base+n:problems.append(f'{pc:#x}: control leaves the code');continue
        seen.add(pc);w=struct.unpack_from('<I',code,pc-base)[0];b=_branch(w,pc)
        if b is None:work.append(pc+4);continue
        kind,t=b
        if kind=='ret':continue
        if kind=='br':problems.append(f'{pc:#x}: indirect jump');continue
        if kind=='blr':
            a,c=struct.unpack_from('<2I',code,pc-8-base) if pc-8>=base else (0,0)
            if w==0xD63F0200 and a&0xFFE0001F==0xD2800010 and c&0xFFE0001F==0xF2A00010:
                external.add(((a>>5)&0xFFFF)|(((c>>5)&0xFFFF)<<16))
            else:problems.append(f'{pc:#x}: indirect call without its address')
            work.append(pc+4);continue
        if kind=='bl':
            if base<=t<base+n:calls.add(t)
            else:external.add(t)
            work.append(pc+4);continue
        if not base<=t<base+n:problems.append(f'{pc:#x}: branch out of the code');continue
        edges.append((pc,t));work.append(t)
        if kind=='cond':work.append(pc+4)
    return seen,calls,external,problems,edges


def _frame(code,base,pcs):
    """Upper bound of the stack a function takes: every sp decrement in it, summed."""
    total=0
    for pc in pcs:
        w=struct.unpack_from('<I',code,pc-base)[0]
        if w&0x3FC00000==0x29800000 and (w>>5)&31==31:                       # stp ...,[sp,#-N]!
            total+=-sx((w>>15)&0x7F,7)*(8 if w>>31 else 4)
        elif w&0xFF8003FF==0xD10003FF:total+=((w>>10)&0xFFF)<<(12 if w&(1<<22) else 0)   # sub sp,sp,#N
        elif w&0xFFE00C00==0xF8000C00 and (w>>5)&31==31:total+=-sx((w>>12)&0x1FF,9)       # str x,[sp,#-N]!
    return total


def _walk_wrapper(code,base,entry):
    """Every function reachable from entry: {function: (instructions, calls)}, external calls,
    problems, and the stack bound (patch frames along the deepest chain)."""
    funcs={};external=set();problems=[];stack=[entry]
    while stack:
        f=stack.pop()
        if f in funcs:continue
        pcs,calls,ext,probs,edges=_explore(code,base,f)
        funcs[f]=(pcs,calls,edges);external|=ext;problems+=probs;stack+=list(calls)
    depth={}
    def d(f,trail=()):
        if f in trail:problems.append(f'{f:#x}: recursion');return 0
        if f not in depth:depth[f]=_frame(code,base,funcs[f][0])+max((d(c,trail+(f,)) for c in funcs[f][1]),default=0)
        return depth[f]
    bound=d(entry)
    return funcs,external,problems,bound


def _loops(code,base,funcs):
    """Back edges that close a cycle (inside one function) and are not the bounded shape."""
    bad=[]
    for f,(pcs,calls,edges) in funcs.items():
        succ={}
        for pc in pcs:
            w=struct.unpack_from('<I',code,pc-base)[0];b=_branch(w,pc)
            if b is None or b[0] in ('bl','blr'):succ[pc]=[pc+4]
            elif b[0]=='b':succ[pc]=[b[1]]
            elif b[0]=='cond':succ[pc]=[b[1],pc+4]
            else:succ[pc]=[]
        for pc,t in edges:
            if t>pc:continue
            seen={t};work=[t];cycle=False
            while work and not cycle:
                x=work.pop()
                for y in succ.get(x,()):
                    if y==pc:cycle=True;break
                    if y in pcs and y not in seen:seen.add(y);work.append(y)
            if cycle and not _bounded_loop(code,base,pc):bad.append(hex(pc))
    return bad


def _first_access(code,base,entry):
    """The first load or store not based on sp on the fall-through path from entry, with the
    constant its base register holds (movz/movk tracked)."""
    regs={};pc=entry
    for _ in range(64):
        w=struct.unpack_from('<I',code,pc-base)[0];rd=w&31
        if w&0x7F800000==0x52800000:regs[rd]=((w>>5)&0xFFFF)<<(16*((w>>21)&3))
        elif w&0x7F800000==0x72800000:hw=(w>>21)&3;regs[rd]=(regs.get(rd,0)&~(0xFFFF<<(16*hw)))|(((w>>5)&0xFFFF)<<(16*hw))
        elif w&0x0A000000==0x08000000 and w&0x3B000000!=0x18000000:
            rn=(w>>5)&31
            if rn!=31:return pc,w,regs.get(rn)
        b=_branch(w,pc)
        if b and b[0] in ('cond','ret','br','blr','bl'):return pc,w,None
        pc=b[1] if b and b[0]=='b' else pc+4
    return pc,0,None


def verify_s6(stock,candidate,r,pinned,entry,count,allowed,out):
    need(r.get('groups')==GROUP_MASKS['s6'],'build group mask is not the reviewed one')
    need(count==S6_RECORDS and count<=S6_MAX and S6_TABLE+S6_REC*count<=S6_TABLE_END,'S6 record count')
    n=r['code']['bytes'];code=candidate[off(LO):off(LO)+n]
    need(0<n and LO+n<=S6_TABLE,'S6 code size')
    need(not pinned or hashlib.sha256(code).hexdigest()==r['code']['sha256'],'S6 code differs from the reviewed recipe')
    need(not pinned or count==len(r['install_table']['records']),'S6 record count differs from the reviewed recipe')
    need(not any(candidate[off(LO)+n:off(S6_TABLE)]),'bytes between code and table')
    need(not any(candidate[off(S6_TABLE)+S6_REC*count:off(HDR)]),'bytes after the install table')
    registrations=stock_registrations(stock)
    records=[];helpers=set();thunks={};sites={};marker_new=None
    for i in range(count):
        kind,cls,order,groups,cset,rank,wclass,flags,tx,res8,res32,address,old,new=struct.unpack_from(S6_FMT,candidate,off(S6_TABLE)+S6_REC*i)
        cls=chr(cls);where=f'record {i} at {address:#x}'
        need(order==i,f'{where}: order')
        need(groups in (1,1<<MARKER_GROUP),f'{where}: group mask {groups:#x} is not in this build')
        need(not res8 and not res32,f'{where}: reserved bytes')
        need(not tx,f'{where}: TX pin bits in a build without transactions')
        need(not flags&~(F_NEEDQ|F_ATOMIC),f'{where}: unknown flags')
        need(not flags&F_ATOMIC,f'{where}: all-or-none set flag (not implemented in this build)')
        need(rank>=1 if cset else rank==0,f'{where}: set id or rank')
        for j,prev in enumerate(records):
            need(not cset or prev[6]!=cset or prev[7]<rank,f'{where}: rank does not rise inside set {cset}')
        group=MARKER_GROUP if groups==1<<MARKER_GROUP else 0
        flags=wclass|flags<<3          # reviewed form below: wrapper class | needs-Q << 3
        if group==MARKER_GROUP:
            need(kind==1 and cls=='M',f'{where}: marker record kind or class')
            need(i==count-1,f'{where}: marker record is not last')
            need(address==POINTER,f'{where}: marker record address')
            need(old==struct.unpack_from('<Q',stock,off(POINTER))[0]==0x40596140,f'{where}: marker old value is not the stock word')
            need(candidate[off(address):off(address)+8]==stock[off(address):off(address)+8],f'{where}: pre-installed in the flashed image')
            need(LO<new<LO+n and new%4==0,f'{where}: marker new value outside the code')
            need(not pinned or new==int(r['install_table']['records'][i]['new'],16),f'{where}: marker new value differs from the reviewed recipe')
            need(flags==W_C|8 and (cset,rank)==(1,4),f'{where}: marker flags or rank (video-menu set, handler rank 4)')
            marker_new=new
        elif kind==3:
            need(i==0 and cls=='D' and address==ITEMS_POINTER,f'{where}: the page record must be first, at the video rows pointer')
            need(old==struct.unpack_from('<Q',stock,off(address))[0]==0x43CB3868 and new==0,f'{where}: old value is not the stock word')
            need(candidate[off(address):off(address)+8]==stock[off(address):off(address)+8],f'{where}: pre-installed in the flashed image')
            need((cset,rank,flags)==(1,1,W_NONE|8),f'{where}: the page record is the video-menu set, rank 1, restored under Q')
        elif kind==2:
            need(i>0 and cls=='S' and records[-1][0] in (3,2),f'{where}: code records follow the page record')
            need(in_text(address),f'{where}: outside the copied text windows')
            for a,b,what in S5_DENY:need(not a<=address<b,f'{where}: inside {what} {a:#x}-{b:#x}')
            stock_word=word(stock,address)
            need(old==stock_word and old>>26==0x25,f'{where}: old value is not the stock bl')
            target=address+4*sx(old&0x3FFFFFF,26)
            need(target in S5_SITE_TARGETS,f'{where}: the stock call does not go to a wrapped function')
            need(word(candidate,address)==stock_word,f'{where}: pre-installed in the flashed image')
            need(new>>26==0x25,f'{where}: new value is not a bl')
            thunk=address+4*sx(new&0x3FFFFFF,26)
            callee,index,h=decode_thunk(code,LO,thunk)
            need(callee==target,f'{where}: the thunk calls {callee:#x}, not the replaced {target:#x}')
            need(flags==W_F and (cset,rank)==(0,0),f'{where}: flags or rank (F class, any poll, no set)')
            helpers.add(h);thunks[thunk]=(callee,index);sites.setdefault(target,[]).append(address)
        elif kind==1:
            need(i>0 and cls=='R',f'{where}: class')
            need(address in S5_CALLBACK_WORDS,f'{where}: not a dispatcher callback word')
            need(registrations.get(address)==old and in_text(old),f'{where}: old value is not the callback the stock code registers')
            callee,index,h=decode_thunk(code,LO,new)
            need(callee==old,f'{where}: the thunk calls {callee:#x}, not the replaced {old:#x}')
            need(flags==W_F and (cset,rank)==(0,0),f'{where}: flags or rank (F class, any poll, no set)')
            helpers.add(h);thunks[new]=(callee,index)
        else:raise Reject(f'{where}: unknown kind {kind}')
        records.append((kind,cls,address,old,new,group,cset,rank,flags))
    need(records and records[0][0]==3,'the page record is missing')
    need(len({x[2] for x in records})==count,'an address appears twice')
    need([i for i,x in enumerate(records) if x[6]==1]==[0,count-1] and {x[6] for x in records}=={0,1},
         'S6 sets: the video-menu set 1 is exactly P and the refresh handler; every other record in no set')
    groups=sum(1<<g for g in {x[5] for x in records})
    need(groups==GROUP_MASKS['s6'],f'record groups {groups:#x} are not the build mask {GROUP_MASKS["s6"]:#x}')
    for target,got in sites.items():
        need(got==stock_calls(stock,target),f'call sites of {target:#x} are not exactly the stock callers')
    need(len(helpers)==1,'thunks use different helpers')
    need(len({i for _,i in thunks.values()})==len(thunks) and len({c for c,_ in thunks.values()})==len(thunks),'thunk index or callee reused')
    constants,problems,calls=scan_code(code,LO)
    for i in range(0,n-3,4):
        w=struct.unpack_from('<I',code,i)[0]
        if w&0xFFFFFC1F==0xD63F0000:
            a,b=struct.unpack_from('<2I',code,i-8) if i>=8 else (0,0)
            need(w==0xD63F0200 and a&0xFFE0001F==0xD2800010 and b&0xFFE0001F==0xF2A00010,f'indirect call without its address at {LO+i:#x}')
            need(((a>>5)&0xFFFF)|(((b>>5)&0xFFFF)<<16) in S5M_CALLS,f'indirect call outside the allowlist at {LO+i:#x}')
        # 1.6(g): no exclusive store outside the bounded loop shape.
        if _exclusive_store(w):
            at=LO+i
            need(i+12<n and struct.unpack_from('<I',code,i+4)[0]&0xFF00001F==0x34000000|(w>>16)&31 and _bounded_loop(code,LO,at+12),
                 f'exclusive store outside a bounded loop at {at:#x}')
    external={t for _,t in calls if t is None or not LO<=t<LO+n}
    need(external<=set(S5M_CALLS),f'S6 calls outside the allowlist: {sorted(hex(t) if t else str(t) for t in external-set(S5M_CALLS))}')
    spans=((LO,LO+n),(HDR,HDR+64),(S6_TABLE,S6_TABLE+S6_REC*count),(0x40194FF0,0x40195000))
    stray=sorted(hex(c) for c in constants if not (c in S5M_ALLOWED_CODE or any(x<=c<y for x,y in spans)))
    need(not stray,f'S6 builds code addresses outside the allowlist: {stray}')
    need(not problems,f'S6 forbidden instructions: {problems}')
    # ---- wrapper rules (3.5) on the code ----
    enter,leave=next(iter(helpers))
    api={}
    for name in S6_API:
        sym=r['code']['symbols'].get(name)
        need(sym is not None,f'reviewed recipe lacks {name}')
        api[name]=int(sym['va'],16);need(LO<=api[name]<LO+n,f'{name} outside the code')
    wrappers={**{f'thunk {t:#x}':(t,{c}) for t,(c,_) in thunks.items()},
              'z63_enter':(enter,set()),'z63_leave':(leave,set()),
              'z63_marker_refresh':(marker_new,{0x40596140,0x4052C900,0x404066C8,0x4040A1AC}),
              **{name:(va,set()) for name,va in api.items()}}
    rules={}
    for name,(at,allowed_calls) in wrappers.items():
        funcs,ext,probs,bound=_walk_wrapper(code,LO,at)
        need(not probs,f'{name}: {probs}')
        need(ext<=allowed_calls,f'{name}: calls {sorted(hex(x) for x in ext-allowed_calls)} outside its allowed stock calls')
        bad=_loops(code,LO,funcs)
        need(not bad,f'{name}: unbounded loop at {bad}')
        need(bound<=S6_WRAPPER_STACK,f'{name}: stack bound {bound} above {S6_WRAPPER_STACK}')
        stores=[pc for f,(pcs,_,_) in funcs.items() for pc in pcs if _is_store(struct.unpack_from('<I',code,pc-LO)[0])
                and (struct.unpack_from('<I',code,pc-LO)[0]>>5)&31!=31]
        if name=='z63_marker_refresh':need(not stores,f'{name}: stores outside its frame at {[hex(x) for x in stores]}')
        elif not name.startswith('thunk'):
            plain=[hex(pc) for pc in stores if not _exclusive_store(struct.unpack_from('<I',code,pc-LO)[0])]
            need(not plain,f'{name}: plain stores outside the stack at {plain}')
        rules[name]=dict(entry=hex(at),functions=len(funcs),stack_bound=bound,external_calls=sorted(hex(x) for x in ext))
    for name in ('z63_enter','z63_marker_refresh','z63_admit'):
        at=wrappers[name][0];pc,w,base=_first_access(code,LO,at)
        need(w&0xFFFFFC00 in (0xC8DFFC00,) and base==ITEMS_POINTER,f'{name}: the first access at {pc:#x} is not the acquire load of the publication word')
    # The card-detect path, for the record (an upper bound of the patch's own frames).
    card={}
    for name in ('z63_entry','z63_armed'):
        va=LO if name=='z63_entry' else int(r['code']['symbols']['z63_armed']['va'],16)
        funcs,ext,probs,bound=_walk_wrapper(code,LO,va);card[name]=bound
    allowed+=[(off(LO),off(LO)+n),(off(S6_TABLE),off(S6_TABLE)+S6_REC*count)]
    out['install_table']=dict(address=hex(S6_TABLE),record_bytes=S6_REC,end=hex(S6_TABLE_END),capacity=S6_MAX)
    out['code']=dict(bytes=n,sha256=hashlib.sha256(code).hexdigest(),code_constants=sorted(hex(c) for c in constants),
                     external_calls={hex(t):S5M_CALLS[t] for t in sorted(external)},calls=len(calls),
                     thunks={hex(t):dict(calls=hex(c),index=i) for t,(c,i) in sorted(thunks.items())})
    out['records']=[dict(order=i,kind={1:'data64',2:'code32',3:'page64'}[k],cls=c,groups=1<<g,set=s_,rank=rk,wrapper_class=f&7,needs_q=bool(f&8),address=hex(a),old=hex(o),new=hex(v))
                    for i,(k,c,a,o,v,g,s_,rk,f) in enumerate(records)]
    out['groups']=GROUP_MASKS['s6']
    out['wrapper_rules']=rules
    out['card_detect_stack_bound']=card


def verify(stock,candidate,stage,*,recipe_data=None,pinned=True):
    """pinned=False skips only the recipe hash pins (output, edits, S3 code) so that tests can show
    the structural checks reject a mutation on their own."""
    need(stage in STAGES,'unknown stage')
    need(hashlib.sha256(stock).hexdigest()==STOCK_SHA256,'stock C2.01 MAIN required')
    need(len(candidate)==len(stock),'candidate length differs')
    r=recipe_data if recipe_data is not None else recipe(stage)
    out=dict(stage=stage,candidate_sha256=hashlib.sha256(candidate).hexdigest())
    need(not pinned or out['candidate_sha256']==r['output_sha256'],'candidate is not the reviewed recipe output')
    # Header (recomputed).
    span=candidate[off(LO):off(HDR)];h=candidate[off(HDR):off(HDR)+64]
    build_id=struct.unpack('<Q',hashlib.sha256(span).digest()[:8])[0];crc=zlib.crc32(span)&0xFFFFFFFF
    magic,bid,version,hcrc,entry,table,count=struct.unpack_from('<8sQIIQII',h)
    need(magic==b'Z63GATE1' and version==1 and not any(h[40:]),'header magic, version or reserved bytes')
    need(bid==build_id and hcrc==crc,'header build ID or CRC does not match the patch range')
    need(not any(candidate[off(HDR)+64:off(HI)]),'bytes after the header')
    if stage in ('s5','s5m','s6'):need((entry,table)==(LO,S6_TABLE if stage=='s6' else S5_TABLE),'header entry or table')
    else:need((entry,table,count)==({'s1':(0,0,0),'s2':(LO,0,0),'s3':(LO,TABLE,1),'s4':(LO,TABLE,2)}[stage]),'header entry or table')
    allowed=[(off(HDR),off(HDR)+64)]
    if stage!='s1':
        g=candidate[off(GUARD):off(GUARD_END)]
        need(g==expected_guard(entry,build_id),'guard differs from the independently rebuilt guard')
        targets,loads=decode_guard(g)
        need(targets<={POLL,entry,GUARD_END-0x18},'guard branch target outside {poll, entry, guard}')
        need({a for _,a in loads}<={DETECTOR,HDR,HDR+8,GUARD+0x60,GUARD+0x68},'guard load outside detector, header, literals')
        need(word(candidate,HOOK)==0x94000000|(((GUARD-HOOK)>>2)&0x3FFFFFF) and word(stock,HOOK)==0x94000000|(((POLL-HOOK)>>2)&0x3FFFFFF),'hook word')
        need(not any(candidate[off(GUARD_END):off(0x40195000)]),'anchor not zero in the image')
        allowed+=[(off(GUARD),off(GUARD_END)),(off(HOOK),off(HOOK)+4)]
        out['guard']=dict(sha256=hashlib.sha256(g).hexdigest(),targets=sorted(hex(t) for t in targets),loads=[(k,hex(a)) for k,a in loads])
    if stage=='s2':
        e=candidate[off(LO):off(LO)+20]
        bl=0x94000000|(((POLL-(LO+8))>>2)&0x3FFFFFF)
        need(e==struct.pack('<5I',0xA9BF7BFD,0x910003FD,bl,0xA8C17BFD,0xD65F03C0),'S2 entry differs')
        allowed.append((off(LO),off(LO)+20))
    if stage in ('s3','s4'):
        n=r['code']['bytes'];code=candidate[off(LO):off(LO)+n]
        need(not pinned or hashlib.sha256(code).hexdigest()==r['code']['sha256'],'S3 code differs from the reviewed recipe')
        need(not any(candidate[off(LO)+n:off(TABLE)]),'bytes between code and table')
        kind,group,address,old,new=struct.unpack_from('<IIQQQ',candidate,off(TABLE))
        need((kind,group,address)==(1,0,POINTER),'install record kind/group/address')
        need(old==struct.unpack_from('<Q',stock,off(POINTER))[0]==0x40596140,'install record old value is not the stock word')
        need(LO<new<LO+n and new==int(r['install_table']['records'][0]['new'],16),'install record new value outside code')
        if stage=='s4':
            kind,group,address,old,new=struct.unpack_from('<IIQQQ',candidate,off(TABLE)+32)
            stock_word=word(stock,CODE_SITE)
            need((kind,group,address)==(2,0,CODE_SITE),'S4 code record kind/group/address')
            need(old==stock_word==0x94000000|(((0x4040A1AC-CODE_SITE)>>2)&0x3FFFFFF),'S4 code record old value is not the stock bl TEXT_BIND')
            need(new>>26==0x25 and LO<=CODE_SITE+4*sx(new&0x3FFFFFF,26)<LO+n,'S4 code record new value is not a bl into the code')
            need(word(candidate,CODE_SITE)==stock_word,'S4 code site changed in the flashed image')
        need(not any(candidate[off(TABLE)+32*count:off(HDR)]),'bytes after the install table')
        constants,problems,calls=scan_code(code,LO)
        external={t for _,t in calls if t is None or not LO<=t<LO+n}
        need(external<=set(S3_CALLS),f'S3 calls outside the allowlist: {sorted(hex(t) if t else str(t) for t in external-set(S3_CALLS))}')
        spans=((LO,LO+n),(HDR,HDR+64),(TABLE,TABLE+32*count),(0x40194FF0,0x40195000))
        stray=sorted(hex(c) for c in constants if not (c in S3_ALLOWED_CODE or any(x<=c<y for x,y in spans)))
        need(not stray,f'S3 builds code addresses outside the allowlist: {stray}')
        need(not problems,f'S3 forbidden instructions: {problems}')
        allowed+=[(off(LO),off(LO)+n),(off(TABLE),off(TABLE)+32*count)]
        out['code']=dict(bytes=n,sha256=r['code']['sha256'],code_constants=sorted(hex(c) for c in constants),
                         external_calls={hex(t):S3_CALLS[t] for t in sorted(external)},calls=len(calls))
    if stage in ('s5','s5m'):verify_s5(stock,candidate,r,pinned,entry,count,allowed,out,stage)
    if stage=='s6':verify_s6(stock,candidate,r,pinned,entry,count,allowed,out)
    # Stock digest: the recipe stores no stock value, only one digest over
    # every stock value it relies on, recomputed here independently of the builder: each install
    # record's restore value as the candidate's table holds it (each already checked above against
    # stock, naming its address) and the stock bytes under every recipe edit (which also covers the
    # stock hook word). A mismatch cannot name a site: one digest, by design.
    need(not pinned or isinstance(r.get('stock_digest'),str),'the recipe has no stock digest (older format)')
    if 'stock_digest' in r:
        need(not any('before_sha256' in e for e in r['edits']),'the recipe holds a hash of stock bytes')
        dg=hashlib.sha256(b'z63-stock-digest-v1\0')
        if stage in ('s3','s4','s5','s5m','s6'):
            at,size,fmt={'s6':(S6_TABLE,S6_REC,S6_FMT)}.get(stage,(TABLE,32,'<IIQQQ') if stage in ('s3','s4') else (S5_TABLE,32,'<BBHIQQQ'))
            for i in range(count):
                rec=struct.unpack_from(fmt,candidate,off(at)+size*i)
                w={1:8,2:4,3:8}[rec[0]]
                dg.update(struct.pack('<QB',rec[-3],w)+(rec[-2]&((1<<8*w)-1)).to_bytes(w,'little'))
        for e in sorted(r['edits'],key=lambda e:off(int(e['address'],16))):
            a=off(int(e['address'],16));need(e['offset']==a,f'recipe edit at {e["address"]}: offset')
            dg.update(struct.pack('<QI',int(e['address'],16),e['bytes'])+bytes(stock[a:a+e['bytes']]))
        need(dg.hexdigest()==r['stock_digest'],'stock digest: the recipe and this stock image disagree on the stock values '
             '(records or edited regions; the per-record checks above passed)')
        out['stock_digest']=r['stock_digest']
    runs=diff_runs(stock,candidate)
    for a,b in runs:
        need(any(x<=a and b<=y for x,y in allowed),f'changed bytes outside the declared regions at file offset {a:#x}')
    edits=[(e['address'],e['bytes'],e['after_sha256']) for e in r['edits']]
    for e in (r['edits'] if pinned else ()):
        at=int(e['address'],16);data=candidate[off(at):off(at)+e['bytes']]
        need(hashlib.sha256(data).hexdigest()==e['after_sha256'],f'recipe edit differs at {e["address"]}')
    out.update(header=dict(build_id=f'{build_id:016x}',crc32=f'{crc:08x}',entry=hex(entry),table=hex(table),records=count),
               changed_runs=[(hex(BASE+a) if a<DATA_OFFSET else hex(a),b-a) for a,b in runs],
               changed_bytes=sum(b-a for a,b in runs),recipe_edits=len(edits),accepted=True)
    return out
