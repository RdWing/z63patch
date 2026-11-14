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
"""Arm-time safety gate: staged trial builds S1-S5 (separate build path).

The flashed MAIN differs from stock C2.01 only in

  S1  the 64-byte patch header at 0x401FFF00 (the rest of the patch range stays zero);
  S2  S1, plus z63_entry (call the original poll, return) at 0x401A0000, the 112-byte
      guard at 0x40194F00 (empty copied text) and the one hook word 0x4032768C
      (bl 0x403277C0 -> bl guard);
  S3  S2 with native/arm.c as z63_entry (arming state machine, arm-file reader, one data
      install: the video menu refresh handler 0xA028F5600) and a one-record install table;
  S4  S3 plus one instruction word installed through the RAM alias;
  S5  infrastructure armed, features off (native/arm_time/s5.c): a table-driven installer
      with rollback, the state page, the dispatcher callbacks and the pipeline call sites
      of the infrastructure group, and an armed poll that disarms. No trial marker;
  S5M S5 plus the visible marker as a second group (native/arm_time/s5.c, -DZ63_MARKER): group 0
      is the S5 infrastructure, group 1 the S3 data-pointer record (video menu refresh handler
      0xA028F5600 -> z63_marker_refresh, "Z63 ARMED" in the N-RAW quality row). Arm-file group
      mask 1 or 3; the marker record is last in the table, so disarm restores it first.
  S6  S5M plus the disarm lifecycle (native/arm_time/s6.c): the gate
      word in the state page, disarm LIVE -> DRAINING -> RESTORING -> STOCK, group masks with
      group ids in the records, coupling sets and ranks, admission and settings publication
      without a lock of our own. Same records, same flashed guard and hook word as S5M. Install
      table v2 at 0x401F6000 (40-byte records with contributor-group mask, coupling set, rank,
      wrapper class, flags and TX pins; room for the ~410 records of the full feature set) and
      the state-page map of the migration plan (counters at +0x9B0, statuses at +0xC60, 800
      records). No feature behaviour.

Nothing here writes an installable container; every image is DO_NOT_FLASH. The
independent checks are in z63patch/verify_arm_time.py.
"""
import hashlib
import json
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

from .image import Image, require, MAIN_SHA256
from .linker import link

ROOT=Path(__file__).resolve().parents[1]
STAGES=('s1','s2','s3','s4','s5','s5m','s6')

PATCH_LO,HEADER,PATCH_HI=0x401A0000,0x401FFF00,0x40200000
HEADER_BYTES=64
TABLE=0x401FFE00               # install table (S3), inside the CRC/ID span
GUARD,GUARD_BYTES=0x40194F00,112
ANCHOR=0x40194FF0
GUARD_RESERVATION=(0x40194F00,0x40195000)   # 0x40194180..0x40194F00 is left free
W3=(0x40168000,0x40195000)                   # small text window copied to RAM (W3)
HOOK,HOOK_RETURN,POLL=0x4032768C,0x40327690,0x403277C0
DETECT_SAMPLE=0xA0305DAA5      # slot record 0 (CFexpress) current sample
CARD_PRESENT=1                 # settled in the emulator: 1 with a card, 0 without
MAGIC=b'Z63GATE1'
VERSION=1
TRIAL_POINTER,TRIAL_STOCK=0xA028F5600,0x40596140
TRIAL_GROUPS=1                 # group bit 0: the S3 trial adapter (and the S4 code word)
CODE_SITE,CODE_TARGET=0x4059652C,0x4040A1AC   # S4: bl TEXT_BIND in the video menu refresh (menu only)
CODE_STOCK=0x94000000|(((CODE_TARGET-CODE_SITE)>>2)&0x3FFFFFF)   # The BL is derived, not stored
ARM_MAGIC=b'Z63ARM\0\1'
# S5 (infrastructure, features off). Kinds: 1 = 64-bit data word, 2 = instruction word, 3 = the
# state page publication (new value granted at run time). Classes as design 5.4.
S5_GROUPS=1                    # group bit 0: infrastructure
S5_TABLE=0x401FFC00            # 24 records of 32 bytes fit below the header
K_DATA,K_CODE,K_PAGE=1,2,3
ITEMS_POINTER,ITEMS_STOCK,ITEMS_COUNT=0xA028CAE20,0x43CB3868,0xA028CAE30   # video menu page +0x10, +0x20
DISPATCH_REGISTER=0x40C00204   # (object, callback, mask, config): stores the callback at object+0x48
# R class: (registration call, object, stock callback, thunk)
S5_CALLBACKS=((0x40755A44,0xA048BAF98,0x40755A54,'z63_capture_dispatch','capture dispatcher callback'),
              (0x40D22680,0xA03324B68,0x40D22770,'z63_movie_dispatch','movie dispatcher callback'))
# S class: every stock bl to the two pipeline transition entries that have callers. The third
# wrapped entry of the feature builds, cancel 0x40E80FCC, has no reference in the image at all.
S5_CALL_SITES=((0x40E80C74,'z63_pipeline_drain','pipeline drain',(0x40F6F204,0x40F72454,0x40F80910)),
               (0x40E7FD08,'z63_pipeline_configure','pipeline configure',(0x40F572AC,0x40F66CFC,0x40F73A24,0x40F77DBC,0x40F7E22C)))
S5_RECORDS=1+sum(len(x[3]) for x in S5_CALL_SITES)+len(S5_CALLBACKS)
S5_SYMBOLS=('z63_entry','z63_armed','z63_enter','z63_leave','z63_capture_dispatch','z63_movie_dispatch',
            'z63_pipeline_drain','z63_pipeline_configure')
# S5M: the marker group (group index 1, mask bit 1) after the S5 records.
S5M_GROUPS=3
MARKER_GROUP=1
S5M_RECORDS=S5_RECORDS+1
S5M_SYMBOLS=S5_SYMBOLS+('z63_marker_refresh',)
# S6: install table v2 (40-byte records: kind, cls, order, u16 contributor-group mask, set, rank,
# wrapper class, flags, TX pin mask, reserved, address, old, new), coupling sets (set 0 = none)
# and the group ids. Ids 2-7 are reserved for the feature groups the design
# names; this build has groups 0 and 1 only and an arm file may not name others.
W_NONE,W_A,W_C,W_F,W_S,W_X=0,1,2,3,4,5
F_NEEDQ,F_ATOMIC=1,2           # flags: restore only under Q; all-or-none set (refused by S6 code)
S6_TABLE,S6_TABLE_END=0x401F6000,0x401FFC00   # 39,936 B: 998 records; the status area caps at 800
S6_RECORD='<BBHHBBBBBBIQQQ'
S6_RECORD_BYTES=40
S6_MAX_RECORDS=800
S6_PAGE_MAP={'0x000':'video menu rows copy (45 x 44 now, room for 48 x 44)','0x840':'reserved: feature state (368 B)',
             '0x9b0':'S5 counters (64 B, magic Z6P6)','0x9f0':'reserved: text-slot originals (to 0xC40)',
             '0xc40':'reserved (16 B)','0xc60':'record statuses (1 B each, 800)','0xf80':'gate block (128 B)'}
S6_GROUPS=3
S6_GROUP_IDS={0:'infrastructure',1:'marker',2:'prores (reserved)',3:'h265 (reserved)',4:'nraw (reserved)',
              5:'view-only units (reserved)',6:'log recording (reserved)',7:'open gate (reserved)'}
S6_SET_VIDEO_MENU=1            # 3.2: rank 1 rows = P; 2 events, 3 count reserved; 4 handlers (the marker)
S6_RANK_HANDLERS=4
S6_SYMBOLS=S5M_SYMBOLS+('z63_admit','z63_exit','z63_effective')
FLAGS=['--target=aarch64-none-elf','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector',
       '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mgeneral-regs-only','-mstrict-align',
       '-mno-outline-atomics','-Wall','-Wextra','-Werror']
# Stock instructions the guard and entry stand on (C2.01, checked on every build). The two
# calls are derived from their targets; the two other words (the instruction after the hook and
# the poll prologue) are not stored: STOCK_SITES lists them and STOCK_SITES_DIGEST is SHA-256 of
# STOCK_SITES_TAG, then per site: address u64, 4 u32, word.
STOCK_WORDS={HOOK:0x94000000|(((POLL-HOOK)>>2)&0x3FFFFFF),0x40327688:0x94000000|(((0x403276E8-0x40327688)>>2)&0x3FFFFFF)}
STOCK_SITES=(HOOK_RETURN,POLL)
STOCK_SITES_TAG=b'z63-arm-time-stock-v1\0'
STOCK_SITES_DIGEST='fe4b8f8f85f47056999044d0d7c61900fbeeab4a29af4e893a09e853611c4c95'


def u32(w):return struct.pack('<I',w)


def branch(at,to,link_=False):
    d=to-at
    require(d%4==0 and -(1<<27)<=d<(1<<27),'branch out of range')
    return (0x94000000 if link_ else 0x14000000)|((d>>2)&0x3FFFFFF)


def bcond(at,to,cond):
    d=to-at
    require(d%4==0 and -(1<<20)<=d<(1<<20),'conditional branch out of range')
    return 0x54000000|(((d>>2)&0x7FFFF)<<5)|cond


def adrp(at,page,rd):
    imm=(page>>12)-(at>>12)
    require(-(1<<20)<=imm<(1<<20) and page&0xFFF==0,'adrp out of range')
    imm&=(1<<21)-1
    return 0x90000000|((imm&3)<<29)|((imm>>2)<<5)|rd


def ldr_literal(at,target,rt):
    d=target-at
    require(d%4==0 and -(1<<20)<=d<(1<<20),'literal out of range')
    return 0x58000000|(((d>>2)&0x7FFFF)<<5)|rt


def guard_words(entry,build_id):
    """The 23-instruction guard of design 5.3, its alignment word and literals (112 bytes)."""
    g=GUARD;at=lambda i:g+4*i
    stock=at(22);literals=g+96
    w=[0xD2800000|(0xDAA5<<5)|16,                  # movz x16,#0xdaa5
       0xF2A00000|(0x0305<<5)|16,                  # movk x16,#0x305,lsl #16
       0xF2C00000|(0x000A<<5)|16,                  # movk x16,#0xa,lsl #32   -> 0xA0305DAA5
       0x39400000|(16<<5)|17,                      # ldrb w17,[x16]
       0x7100001F|(CARD_PRESENT<<10)|(17<<5),      # cmp w17,#CARD_PRESENT
       bcond(at(5),stock,1),                       # b.ne stock
       adrp(at(6),HEADER&~0xFFF,16),               # adrp x16,0x401FF000
       0xF9400000|((0xF00//8)<<10)|(16<<5)|17,     # ldr x17,[x16,#0xf00]   header magic
       ldr_literal(at(8),literals,16),             # ldr x16,=magic
       0xEB11021F,                                 # cmp x16,x17
       bcond(at(10),stock,1),                      # b.ne stock
       adrp(at(11),HEADER&~0xFFF,16),              # adrp x16,0x401FF000
       0xF9400000|((0xF08//8)<<10)|(16<<5)|17,     # ldr x17,[x16,#0xf08]   header build ID
       ldr_literal(at(13),literals+8,16),          # ldr x16,=build id
       0xEB11021F,                                 # cmp x16,x17
       bcond(at(15),stock,1),                      # b.ne stock
       0xD53BE020|16,                              # mrs x16,cntpct_el0
       0xD53BE000|17,                              # mrs x17,cntfrq_el0
       0xD37CEE31,                                 # lsl x17,x17,#4         16 s of ticks
       0xEB11021F,                                 # cmp x16,x17
       bcond(at(20),stock,3),                      # b.lo stock
       branch(at(21),entry),                       # b z63_entry (LR still 0x40327690)
       branch(stock,POLL),                         # stock: b 0x403277C0 (tail call)
       0xD503201F]                                 # nop (alignment to the literals)
    require(len(w)==24,'guard length')
    data=b''.join(u32(x) for x in w)+MAGIC+struct.pack('<Q',build_id)
    require(len(data)==GUARD_BYTES,'guard bytes')
    return data



UPDATE_CLIENT=0xA04073C70   # update signal word (BSS, 0 at idle)


def guard_words_v2(entry,build_id):
    """Guard version 2 (the s7 family): design 5.3 with the counter test
    (16 s floor, words 16-20) replaced by a test of the update client word: with an update
    started, the guard goes straight to the stock poll and never reaches the patch range.
    Same length, literals, branch targets and register use (x16, x17, NZCV); no store.
    Readiness (stock's start-up finished) is decided in the patch by predicates, not by time."""
    w=bytearray(guard_words(entry,build_id))
    g=GUARD;at=lambda i:g+4*i
    stock=at(22)
    lo,mid,hi=UPDATE_CLIENT&0xFFFF,(UPDATE_CLIENT>>16)&0xFFFF,(UPDATE_CLIENT>>32)&0xFFFF
    new=[0xD2800000|(lo<<5)|16,                     # movz x16,#0x3c70
         0xF2A00000|(mid<<5)|16,                    # movk x16,#0x0407,lsl #16
         0xF2C00000|(hi<<5)|16,                     # movk x16,#0xa,lsl #32    -> 0xA04073C70
         0xF9400000|(16<<5)|17,                     # ldr x17,[x16]            update client
         0xB5000000|((((stock-at(20))>>2)&0x7FFFF)<<5)|17]   # cbnz x17,stock
    for k,x in enumerate(new):struct.pack_into('<I',w,4*(16+k),x)
    return bytes(w)


def entry_s2():
    """S2 z63_entry: call the original poll and return. Only the 16-byte frame push."""
    at=PATCH_LO
    return b''.join(u32(x) for x in (0xA9BF7BFD,             # stp x29,x30,[sp,#-16]!
                                     0x910003FD,             # mov x29,sp
                                     branch(at+8,POLL,True), # bl 0x403277C0
                                     0xA8C17BFD,             # ldp x29,x30,[sp],#16
                                     0xD65F03C0))            # ret


def header(span,entry,table=0,count=0):
    """64-byte header over span = bytes [PATCH_LO, HEADER)."""
    require(len(span)==HEADER-PATCH_LO,'header span')
    build_id=struct.unpack('<Q',hashlib.sha256(span).digest()[:8])[0]
    crc=zlib.crc32(span)&0xFFFFFFFF
    data=MAGIC+struct.pack('<QIIQII',build_id,VERSION,crc,entry,table,count)+bytes(24)
    require(len(data)==HEADER_BYTES,'header bytes')
    return data,build_id,crc


def check_stock(image):
    for va,word in STOCK_WORDS.items():
        require(image.words(va,1)[0]==word,f'stock word changed at {va:#x}')
    h=hashlib.sha256(STOCK_SITES_TAG)
    for va in STOCK_SITES:h.update(struct.pack('<QI',va,4)+image.read(va,4))
    require(h.hexdigest()==STOCK_SITES_DIGEST,'stock word changed (digest over the hook return and poll prologue)')
    require(not any(image.read(GUARD_RESERVATION[0],GUARD_RESERVATION[1]-GUARD_RESERVATION[0])),'guard reservation not empty')
    require(not any(image.read(PATCH_LO,PATCH_HI-PATCH_LO)),'patch range not empty in stock')
    require(struct.unpack('<Q',image.read(TRIAL_POINTER,8))[0]==TRIAL_STOCK,'trial pointer changed')
    require(W3[0]<=GUARD and GUARD+GUARD_BYTES<=ANCHOR and ANCHOR+16<=W3[1],'guard outside the copied window')


def compile_s3(compiler='clang',trial_item=0x31B5,s4=False):
    with tempfile.TemporaryDirectory(prefix='z63-arm-') as t:
        p=Path(t)
        defines=[f'-DZ63_CARD_PRESENT={CARD_PRESENT}',f'-DZ63_TRIAL_ITEM={trial_item:#x}u',
                 f'-DZ63_TABLE={TABLE:#x}ull',f'-DZ63_GROUPS={TRIAL_GROUPS}u',*(['-DZ63_S4'] if s4 else [])]
        run=subprocess.run([compiler,*FLAGS,*defines,'-c',str(ROOT/'native/arm_time/arm.c'),'-o',str(p/'arm.o')],
                           capture_output=True,text=True)
        require(run.returncode==0,run.stderr)
        code,sections,symbols,relocations=link((p/'arm.o').read_bytes(),base=PATCH_LO,end=TABLE)
    require(int(symbols['z63_entry']['va'],16)==PATCH_LO,'z63_entry is not first')
    return code,sections,symbols,relocations


def bl_sites(image,target):
    """Every bl to target in the copied text windows W1 and W2."""
    out=[]
    for lo,hi in ((0x40200000,0x4090EBE8),(0x40C00000,0x416692F8)):
        words=image.words(lo,(hi-lo)//4)
        out+=[lo+4*i for i,w in enumerate(words) if w>>26==0x25 and lo+4*i+4*(((w&0x3FFFFFF)^0x2000000)-0x2000000)==target]
    return tuple(out)


def check_stock_s5(image):
    require(struct.unpack('<QH',image.read(ITEMS_POINTER,8)+image.read(ITEMS_COUNT,2))==(ITEMS_STOCK,45),'video menu page changed')
    for call,obj,callback,_,_ in S5_CALLBACKS:
        # movz/movk x0 = object, x1 = callback, then bl register (as compiled in C2.01).
        def constant(reg,value):
            return [0xD2800000|((value&0xFFFF)<<5)|reg]+[0xF2800000|(hw<<21)|(((value>>(16*hw))&0xFFFF)<<5)|reg for hw in (1,2,3)]
        got=list(image.words(call-0x24,10))
        x0,x1=constant(0,obj),constant(1,callback)
        require(got[:8]==[x0[0],x1[0],x0[1],x1[1],x0[2],x1[2],x0[3],x1[3]] and got[9]==branch(call,DISPATCH_REGISTER,True),
                f'stock registration changed at {call:#x}')
    for target,_,_,sites in S5_CALL_SITES:
        require(bl_sites(image,target)==sites,f'stock callers of {target:#x} changed')
    require(not bl_sites(image,0x40E80FCC),'pipeline cancel gained a caller')


def compile_s5(compiler='clang',marker=False,trial_item=0x31B5):
    with tempfile.TemporaryDirectory(prefix='z63-arm-') as t:
        p=Path(t)
        defines=[f'-DZ63_CARD_PRESENT={CARD_PRESENT}',f'-DZ63_TABLE={S5_TABLE:#x}ull',
                 f'-DZ63_GROUPS={S5M_GROUPS if marker else S5_GROUPS}u',f'-DZ63_RECORDS={S5M_RECORDS if marker else S5_RECORDS}u']
        if marker:defines+=['-DZ63_MARKER',f'-DZ63_TRIAL_ITEM={trial_item:#x}u']
        run=subprocess.run([compiler,*FLAGS,*defines,'-c',str(ROOT/'native/arm_time/s5.c'),'-o',str(p/'s5.o')],
                           capture_output=True,text=True)
        require(run.returncode==0,run.stderr)
        code,sections,symbols,relocations=link((p/'s5.o').read_bytes(),base=PATCH_LO,end=S5_TABLE)
    require(int(symbols['z63_entry']['va'],16)==PATCH_LO,'z63_entry is not first')
    return code,sections,symbols,relocations


def compile_s6(compiler='clang',trial_item=0x31B5):
    with tempfile.TemporaryDirectory(prefix='z63-arm-') as t:
        p=Path(t)
        defines=[f'-DZ63_CARD_PRESENT={CARD_PRESENT}',f'-DZ63_TABLE={S6_TABLE:#x}ull',f'-DZ63_GROUPS={S6_GROUPS}u',
                 f'-DZ63_RECORDS={S5M_RECORDS}u',f'-DZ63_TRIAL_ITEM={trial_item:#x}u']
        run=subprocess.run([compiler,*FLAGS,'-ffixed-x18',*defines,'-c',str(ROOT/'native/arm_time/s6.c'),'-o',str(p/'s6.o')],
                           capture_output=True,text=True)
        require(run.returncode==0,run.stderr)
        code,sections,symbols,relocations=link((p/'s6.o').read_bytes(),base=PATCH_LO,end=S6_TABLE)
    require(int(symbols['z63_entry']['va'],16)==PATCH_LO,'z63_entry is not first')
    return code,sections,symbols,relocations


def records_s6(image,symbols):
    """S6 table v2: the S5M records with contributor groups, coupling set, rank, wrapper class
    and flags. P (rank 1) and the refresh handler word with the marker (rank 4) form the
    video-menu set 1; the other records are in no set. Install in table order, restore in
    reverse (a set restores in descending rank)."""
    _,rows=records_s5(image,symbols,marker=True)
    out=[];packed=b''
    for i,r in enumerate(rows):
        address,old,new=int(r['address'],16),int(r['old'],16),int(r['new'],16)
        if r['kind']=='page64':groups,cset,rank,wclass,flags=1,S6_SET_VIDEO_MENU,1,W_NONE,F_NEEDQ
        elif r['cls']=='M':groups,cset,rank,wclass,flags=1<<MARKER_GROUP,S6_SET_VIDEO_MENU,S6_RANK_HANDLERS,W_C,F_NEEDQ
        else:groups,cset,rank,wclass,flags=1,0,0,W_F,0
        kind=dict(data64=K_DATA,code32=K_CODE,page64=K_PAGE)[r['kind']]
        packed+=struct.pack(S6_RECORD,kind,ord(r['cls']),i,groups,cset,rank,wclass,flags,0,0,0,address,old,new)
        r=dict(r);r.pop('group',None)
        out.append(dict(r,groups=groups,set=cset,rank=rank,wrapper_class={W_NONE:'-',W_C:'C',W_F:'F'}[wclass],
                        restore_needs_q=bool(flags&F_NEEDQ),all_or_none=False,tx_pins=0))
    require(len(packed)==S6_RECORD_BYTES*len(out),'S6 record size')
    return packed,out


def records_s5(image,symbols,marker=False):
    """(packed table, manifest rows) in install order; undo is the reverse."""
    va=lambda name:int(symbols[name]['va'],16)
    rows=[dict(kind='page64',cls='D',address=ITEMS_POINTER,old=ITEMS_STOCK,new=0,
               purpose='state page publication: video menu rows pointer -> heap page (copy of the stock rows; granted at arm time)')]
    for target,thunk,what,sites in S5_CALL_SITES:
        for site in sites:
            require(image.words(site,1)[0]==branch(site,target,True),f'call site changed at {site:#x}')
            rows.append(dict(kind='code32',cls='S',address=site,old=branch(site,target,True),new=branch(site,va(thunk),True),
                             purpose=f'{what}: bl {target:#x} -> bl {thunk}'))
    for call,obj,callback,thunk,what in S5_CALLBACKS:
        rows.append(dict(kind='data64',cls='R',address=obj+0x48,old=callback,new=va(thunk),
                         purpose=f'{what} (object {obj:#x} +0x48, registered at {call:#x}): {callback:#x} -> {thunk}'))
    require(len(rows)==S5_RECORDS,'S5 record count')
    if marker:
        require(struct.unpack('<Q',image.read(TRIAL_POINTER,8))[0]==TRIAL_STOCK,'marker pointer changed')
        rows.append(dict(kind='data64',cls='M',group=MARKER_GROUP,address=TRIAL_POINTER,old=TRIAL_STOCK,new=va('z63_marker_refresh'),
                         purpose='marker (group 1): video menu refresh handler -> z63_marker_refresh ("Z63 ARMED" in the N-RAW quality row)'))
    kinds=dict(data64=K_DATA,code32=K_CODE,page64=K_PAGE)
    table=b''.join(struct.pack('<BBHIQQQ',kinds[r['kind']],ord(r['cls']),i,r.get('group',0),r['address'],r['old'],r['new']) for i,r in enumerate(rows))
    listed=[dict(order=i,kind=r['kind'],cls=r['cls'],group=r.get('group',0),address=hex(r['address']),
                 old=f"{r['old']:08x}" if r['kind']=='code32' else hex(r['old']),
                 new=f"{r['new']:08x}" if r['kind']=='code32' else hex(r['new']),purpose=r['purpose']) for i,r in enumerate(rows)]
    return table,listed


def build(original,stage,compiler='clang',trial_item=0x31B5):
    """Return (image bytes, manifest) for one stage."""
    require(stage in STAGES,'unknown stage')
    image=Image(original);check_stock(image)
    span=bytearray(HEADER-PATCH_LO);manifest=dict(schema=1,status='DO_NOT_FLASH',stage=stage,
        design='arm-time safety gate',target='Z6III C2.01',stock_sha256=MAIN_SHA256)
    entry=0;table=0;count=0;symbols={}
    if stage=='s2':
        code=entry_s2();span[:len(code)]=code;entry=PATCH_LO
        manifest['entry_code']=dict(address=hex(PATCH_LO),bytes=len(code),hex=code.hex())
    elif stage in ('s3','s4'):
        code,sections,symbols,relocations=compile_s3(compiler,trial_item,s4=stage=='s4')
        span[:len(code)]=code;entry=PATCH_LO
        adapter=int(symbols['z63_trial_refresh']['va'],16)
        record=struct.pack('<IIQQQ',1,0,TRIAL_POINTER,TRIAL_STOCK,adapter)
        records=[dict(kind='data64',group=0,address=hex(TRIAL_POINTER),old=hex(TRIAL_STOCK),new=hex(adapter),
                      purpose='video menu refresh handler -> z63_trial_refresh')]
        if stage=='s4':
            require(image.words(CODE_SITE,1)[0]==CODE_STOCK==branch(CODE_SITE,0x4040A1AC,True),'S4 code site changed')
            bind=int(symbols['z63_trial_bind']['va'],16);new=branch(CODE_SITE,bind,True)
            record+=struct.pack('<IIQQQ',2,0,CODE_SITE,CODE_STOCK,new)
            records.append(dict(kind='code32',group=0,address=hex(CODE_SITE),old=f'{CODE_STOCK:08x}',new=f'{new:08x}',
                                purpose='video menu refresh: bl TEXT_BIND -> bl z63_trial_bind (installed through the RAM alias)'))
        span[TABLE-PATCH_LO:TABLE-PATCH_LO+len(record)]=record;table=TABLE;count=len(records)
        manifest['code']=dict(address=hex(PATCH_LO),bytes=len(code),sha256=hashlib.sha256(code).hexdigest(),
            sections=sections,relocations=len(relocations),
            symbols={k:v for k,v in symbols.items() if k in ('z63_entry','z63_trial_refresh','z63_trial_bind')})
        manifest['install_table']=dict(address=hex(TABLE),records=records)
        manifest['trial_item']=hex(trial_item)
        manifest['groups']=TRIAL_GROUPS
    elif stage in ('s5','s5m'):
        marker=stage=='s5m'
        check_stock_s5(image)
        code,sections,symbols,relocations=compile_s5(compiler,marker,trial_item)
        span[:len(code)]=code;entry=PATCH_LO
        record,records=records_s5(image,symbols,marker)
        require(S5_TABLE+len(record)<=HEADER,'S5 table does not fit')
        span[S5_TABLE-PATCH_LO:S5_TABLE-PATCH_LO+len(record)]=record;table=S5_TABLE;count=len(records)
        manifest['code']=dict(address=hex(PATCH_LO),bytes=len(code),sha256=hashlib.sha256(code).hexdigest(),
            sections=sections,relocations=len(relocations),symbols={k:symbols[k] for k in (S5M_SYMBOLS if marker else S5_SYMBOLS)})
        manifest['install_table']=dict(address=hex(S5_TABLE),records=records)
        manifest['groups']=S5M_GROUPS if marker else S5_GROUPS
        if marker:manifest['trial_item']=hex(trial_item)
        manifest['armed_hook']=dict(address=hex(HOOK),flashed=f'{branch(HOOK,GUARD,True):08x}',
            armed=f"{branch(HOOK,int(symbols['z63_armed']['va'],16),True):08x}",stock=f'{branch(HOOK,POLL,True):08x}')
    elif stage=='s6':
        check_stock_s5(image)
        code,sections,symbols,relocations=compile_s6(compiler,trial_item)
        span[:len(code)]=code;entry=PATCH_LO
        record,records=records_s6(image,symbols)
        require(len(records)<=S6_MAX_RECORDS and S6_TABLE+len(record)<=S6_TABLE_END,'S6 table does not fit')
        span[S6_TABLE-PATCH_LO:S6_TABLE-PATCH_LO+len(record)]=record;table=S6_TABLE;count=len(records)
        manifest['code']=dict(address=hex(PATCH_LO),bytes=len(code),sha256=hashlib.sha256(code).hexdigest(),
            sections=sections,relocations=len(relocations),symbols={k:symbols[k] for k in S6_SYMBOLS})
        manifest['install_table']=dict(address=hex(S6_TABLE),record_bytes=S6_RECORD_BYTES,capacity=S6_MAX_RECORDS,
                                       end=hex(S6_TABLE_END),records=records)
        manifest['state_page']=S6_PAGE_MAP
        manifest['groups']=S6_GROUPS;manifest['group_ids']={str(k):v for k,v in S6_GROUP_IDS.items()}
        manifest['trial_item']=hex(trial_item)
        manifest['armed_hook']=dict(address=hex(HOOK),flashed=f'{branch(HOOK,GUARD,True):08x}',
            armed=f"{branch(HOOK,int(symbols['z63_armed']['va'],16),True):08x}",stock=f'{branch(HOOK,POLL,True):08x}')
        manifest['free_bytes']=dict(code_to_table=S6_TABLE-PATCH_LO-len(code),table_to_header=HEADER-S6_TABLE-len(record),
                                    range=hex(PATCH_LO)+'..'+hex(PATCH_HI))
    head,build_id,crc=header(bytes(span),entry,table,count)
    manifest['header']=dict(address=hex(HEADER),magic=MAGIC.decode(),build_id=f'{build_id:016x}',version=VERSION,
                            crc32=f'{crc:08x}',entry=hex(entry),table=hex(table),records=count)
    if stage=='s2':image.put(PATCH_LO,bytes(span[:len(code)]),'arm-time z63_entry (S2: call the original poll, return)',zero=True)
    if stage in ('s3','s4','s5','s5m','s6'):
        image.put(PATCH_LO,bytes(span[:len(code)]),f'arm-time native code ({stage.upper()})',zero=True)
        tbytes=(S6_RECORD_BYTES if stage=='s6' else 32)*count
        image.put(table,bytes(span[table-PATCH_LO:table-PATCH_LO+tbytes]),'arm-time install table (one record)' if count==1 else f'arm-time install table ({count} records)',zero=True)
        require(not any(span[len(code):table-PATCH_LO]) and not any(span[table-PATCH_LO+tbytes:]),'undeclared patch-range bytes')
    image.put(HEADER,head,'arm-time patch header',zero=True)
    if stage in ('s2','s3','s4','s5','s5m','s6'):
        guard=guard_words(entry,build_id)
        image.put(GUARD,guard,'arm-time guard (copied text W3)',zero=True)
        hook=u32(branch(HOOK,GUARD,True))
        image.put(HOOK,hook,'arm-time entry hook: card-poll call -> guard',expected=u32(branch(HOOK,POLL,True)))
        manifest['guard']=dict(address=hex(GUARD),bytes=len(guard),sha256=hashlib.sha256(guard).hexdigest())
        manifest['entry_hook']=dict(address=hex(HOOK),old=f'{branch(HOOK,POLL,True):08x}',new=f'{branch(HOOK,GUARD,True):08x}')
        manifest['anchor']=dict(address=hex(ANCHOR),bytes=16,image='zero')
    data=image.finalize()
    manifest['edits']=image.edits
    manifest['stock_digest']=stock_digest(original,manifest.get('install_table',{}).get('records',()),image.edits)
    manifest['output_sha256']=hashlib.sha256(data).hexdigest()
    manifest['regions']=[dict(address=e['address'],bytes=e['bytes'],purpose=e['purpose']) for e in sorted(image.edits,key=lambda e:e['offset'])]
    manifest['changed_bytes']=sum(1 for a,b in zip(data,original) if a!=b)
    return data,manifest


def arm_file(build_id,groups=TRIAL_GROUPS,flags=0):
    body=ARM_MAGIC+struct.pack('<QQI',build_id,groups,flags)+bytes(32)
    require(len(body)==60,'arm file body')
    return body+struct.pack('<I',zlib.crc32(body)&0xFFFFFFFF)


# Like the s7 recipes (gate.py), an S1-S6 recipe stores no stock value.
# The build manifest's stock fields (each record's restore value, the stock hook word, the hash of
# the stock bytes under each edit) are dropped from the recipe; one digest covers them all and is
# recomputed from the user's verified stock MAIN by the builder (--check) and, independently, by
# verify_arm_time. Same layout and tag as gate.stock_digest without sanitisers.
STOCK_DIGEST_TAG=b'z63-stock-digest-v1\0'
RECORD_WIDTH={'code32':4,'data64':8,'page64':8}


def stock_digest(stock,records,edits):
    """SHA-256 over every install record in table order (address u64, width u8, the stock value
    the record restores) and every edit in offset order (address u64, length u32, the stock bytes
    under the edit). One digest per recipe: a hash of a 4-byte word could be reversed."""
    h=hashlib.sha256(STOCK_DIGEST_TAG)
    for r in records:
        w=RECORD_WIDTH[r['kind']]
        h.update(struct.pack('<QB',int(r['address'],16),w)+int(r['old'],16).to_bytes(w,'little'))
    for e in sorted(edits,key=lambda e:e['offset']):
        h.update(struct.pack('<QI',int(e['address'],16),e['bytes'])+bytes(stock[e['offset']:e['offset']+e['bytes']]))
    return h.hexdigest()


def recipe(manifest):
    keys=('guard','entry_hook','code','install_table','entry_code','trial_item','armed_hook','groups','group_ids','state_page','free_bytes')
    out=json.loads(json.dumps({k:manifest[k] for k in keys if k in manifest and (k not in ('armed_hook','groups') or manifest['stage'] in ('s5','s5m','s6'))}))
    for r in out.get('install_table',{}).get('records',()):r.pop('old',None)
    out.get('entry_hook',{}).pop('old',None)
    out.get('armed_hook',{}).pop('stock',None)
    return dict(schema=1,target='Z6III C2.01',status='DO_NOT_FLASH',stage=manifest['stage'],
                output_sha256=manifest['output_sha256'],
                edits=[{k:v for k,v in e.items() if k!='before_sha256'} for e in manifest['edits']],
                header=manifest['header'],stock_digest=manifest['stock_digest'],**out)


def write_json(path,value):
    Path(path).write_text(json.dumps(value,indent=2)+'\n')
