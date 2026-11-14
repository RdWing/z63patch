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
"""G10 part 10a: geometry records; adapters in native/arm_time/groups/open_gate_geom.c.

One all-or-none set `geom` (52 records, needs_q): a partly installed geometry would be the
half-switched state the design warns about. Install and restore happen under Q (settled photo live
view), where the open-gate decision is 0 (movie word 0xA048BB160 != 1), so every adapter acts as
stock while the set is in transition.

 - plan function 0x4105D07C: post-fix at its two call sites (BL 0x4105B368, tail B 0x4105AE3C);
 - the two readers of the plan record's patched fields (emulator: another CPU's stock
   stores land between this CPU's post-fix and the read): the dims query 0x4105B32C at its five
   callers' BLs (DIMS_SITES, its rows output fixed after the call) and the sensor setup's second
   memcpy BL 0x410AD7AC (both copies of the record fixed after the copy);
 - size function 0x4105D550: the five old value sites (stock word a movz: a non-branch word
   may not be replaced while another core can execute it, and this function runs from the
   live-view step) re-sited onto the three B words that end their straight-line blocks
   (TAIL_SITES, B->B: the adapter sets the site registers and leaves by b to the stock target);
 - leaves 0x4105EB40 (5 BL), 0x4105EBC0 (2 BL), 0x4105EC38 (3 BL), function 0x4105EE00 (9 BL,
   3 B): post-fix at every direct caller (no data pointer to them);
 - classifier 0x41175E5C: 17 BL post-checks (option A; the B-tail option replaced a
   mov w0 word, which the design does not allow).
Re-plan: set `replan`, one
record `g10_replan` (BL->BL at the applier call 0x40F681FC). Every geometry adapter above decides
from the plan latch (og_plan_kind), which this record raises or switches in movie view (and the
publication callback z63p_g10 in settled photo view); the plan post-fix only lowers it. Option C (`g10_twin_cmp`, BL->BL at the SECOND compare call 0x40F67FC4) is
declared here as TWIN_RECORDS and used only by the variant open_gate_twin (open_gate.TWIN_COMPARE).
Dropped from the design here: the B-tail adapters at 0x4105EF20 and 0x41175F48 (stock words are
mov, not B/BL). Left to other parts: container sites (2.5) and NCTG/proxy descriptors (2.6): 10b;
2.7 entry re-sites: ENT/10b; 2.8/2.10: 10c; 2.11: 10d."""
from . import Code, bl_word

PLAN, PLAN_BL, PLAN_B = 0x4105D07C, 0x4105B368, 0x4105AE3C
LEAF_EB40, LEAF_EBC0, LEAF_EC38, LEAF_EE00, CLASSIFY = 0x4105EB40, 0x4105EBC0, 0x4105EC38, 0x4105EE00, 0x41175E5C
EB40_SITES = (0x41054608, 0x41054854, 0x41055290, 0x41055520, 0x410566F4)
EBC0_SITES = (0x41055304, 0x41055530)
EC38_SITES = (0x41054AB4, 0x41054ADC, 0x4105F00C)
EE00_BL = (0x410557E4, 0x41056F84, 0x41057234, 0x41057324, 0x410574A0, 0x4105761C, 0x410577B4, 0x4105794C, 0x4105A4AC)
EE00_B = (0x4105F288, 0x4105F344, 0x4105F3B8)
CLS_SITES = (0x41160364, 0x41160738, 0x411670F0, 0x4116BCA4, 0x4116BD90, 0x4116BEA0, 0x4116C31C, 0x41175D68, 0x411760D8,
             0x41176344, 0x41178418, 0x4117896C, 0x41178B48, 0x41178D24, 0x41179140, 0x41179368, 0x41179718)
# (address, stock word, adapter, register, stock value, open value, size) - old open_gate.S sites 24, 14, 15, 3, 4.
# The value sites themselves stay stock; the adapter named here is the tail record
# below that sets the register (the old Q-BL value adapters g10_v_* are gone). The stock word is
# `movz w<register>, #<stock value>`, derived from the two fields (no literal stock word).
def movz_w(register, value):
    return 0x52800000 | (value << 5) | register


VALUE_SITES = tuple((at, movz_w(reg, stock), adapter, reg, stock, open_, size) for at, adapter, reg, stock, open_, size in (
    (0x4105D784, 'g10_t_d78c', 20, 3432, 4040, 6),
    (0x4105D7F0, 'g10_t_d804', 23, 3432, 4040, 4),
    (0x4105D7F8, 'g10_t_d804', 20, 2288, 2692, 4),
    (0x4105D9F0, 'g10_t_da18', 20, 3432, 4040, 6),
    (0x4105DA0C, 'g10_t_da18', 13, 3402, 4032, 6)))
# Tail records: (B word, its stock target, adapter, indices into VALUE_SITES). Each B ends the
# straight-line block that holds its value sites; static proof (tests/test_arm_time_s7_opengate_a.py
# Static.test_tail_sites): no other instruction, branch, jump-table entry (the five tables of
# 0x4105D550: 0xA02BC7728, 0xA02BC7758, 0xA02BC779A, 0xA02BC7788, 0xA02BC77AC), literal, pointer or
# address build in the stock image reaches an instruction after a value site up to its B; the
# block between does not read or write the site registers; NZCV is dead at the B (written before
# any read on every path from its target); x30 is kept by the adapter.
TAIL_SITES = ((0x4105D78C, 0x4105DD44, 'g10_t_d78c', (0,)),
              (0x4105D804, 0x4105E078, 'g10_t_d804', (1, 2)),
              (0x4105DA18, 0x4105E184, 'g10_t_da18', (3, 4)))
DIMS, MEMCPY = 0x4105B32C, 0x4163B5C0
DIMS_SITES = (0x4105B2C4, 0x4105B3C4, 0x4105B44C, 0x4105B4D4, 0x4105B55C)   # every caller of the dims query
SENS_COPY = 0x410AD7AC                            # sensor setup: second memcpy of the plan record
APPLIER, APPLIER_BL = 0x40F6F014, 0x40F681FC      # the applier's only direct call (both routes)
COMPARE, COMPARE_1, COMPARE_2 = 0x40F6E8F4, 0x40F67EC4, 0x40F67FC4   # option C: the second call only
MODE8_ROUTE = (0x40F681C4, 0x40F681F8)            # straight line into the applier call (no branch)
STOCK_CALLS = {'stock_og_sz_dd44': 0x4105DD44, 'stock_og_sz_e078': 0x4105E078, 'stock_og_sz_e184': 0x4105E184,   # tail targets
               'stock_og_applier': APPLIER, 'stock_og_compare': COMPARE, 'stock_og_plan': PLAN, 'stock_og_leaf_eb40': LEAF_EB40, 'stock_og_leaf_ebc0': LEAF_EBC0,
               'stock_og_leaf_ec38': LEAF_EC38, 'stock_og_leaf_ee00': LEAF_EE00, 'stock_og_classify': CLASSIFY,
               'stock_og_dims': DIMS, 'stock_og_memcpy': MEMCPY}


def b_word(at, callee):
    return 0x14000000 | (((callee - at) >> 2) & 0x3FFFFFF)


def _records():
    out = []

    def add(at, old, symbol, link, cls, purpose):
        out.append(Code(address=at, old=old, symbol=symbol, link=link, cls=cls, wrapper='C', purpose=purpose,
                        set='geom', rank=len(out) + 1, needs_q=True, all_or_none=True, stores=True))
    add(PLAN_BL, bl_word(PLAN_BL, PLAN), 'g10_plan', True, 'A',
        'plan call: bl 0x4105D07C -> bl g10_plan (stock plan, then the 6K/4K record rows 4040 and the photo frontend block on the area-4 movie path)')
    add(PLAN_B, b_word(PLAN_B, PLAN), 'g10_plan', False, 'A',
        'plan tail: b 0x4105D07C -> b g10_plan (call-and-return; same post-fix)')
    for at in DIMS_SITES:
        add(at, bl_word(at, DIMS), 'g10_dims', True, 'A',
            'dims query call: bl 0x4105B32C -> bl g10_dims (then, on the latched 6K path under the hold, the rows it handed out 3432 -> 4040)')
    add(SENS_COPY, bl_word(SENS_COPY, MEMCPY), 'g10_sens', True, 'A',
        'sensor setup record copy: bl 0x4163B5C0 -> bl g10_sens (then, under the hold, both copies of the latched record to the open rows and block)')
    for at, target, sym, sites in TAIL_SITES:
        what = '; '.join(f'w{VALUE_SITES[k][3]} {VALUE_SITES[k][4]} -> {VALUE_SITES[k][5]} for {VALUE_SITES[k][6]}K '
                         f'(value site {VALUE_SITES[k][0]:#x})' for k in sites)
        add(at, b_word(at, target), sym, False, 'A', f'size function block tail: b {target:#x} -> b {sym} ({what}; then b {target:#x})')
    for at in EB40_SITES:
        add(at, bl_word(at, LEAF_EB40), 'g10_eb40', True, 'A', 'leaf 0x4105EB40 call: 4K reduced rows 2288 -> 2692 (old site 0x4105EB84)')
    for at in EBC0_SITES:
        add(at, bl_word(at, LEAF_EBC0), 'g10_ebc0', True, 'A', 'leaf 0x4105EBC0 call: coded rows 2276 -> 2692 (4K) / 3410 -> 4040 (6K)')
    for at in EC38_SITES:
        add(at, bl_word(at, LEAF_EC38), 'g10_ec38', True, 'A', 'leaf 0x4105EC38 call: 4K active rows 2268 -> 2688 (old site 0x4105ECEC)')
    for at in EE00_BL:
        add(at, bl_word(at, LEAF_EE00), 'g10_ee00', True, 'A', 'function 0x4105EE00 call: size class 7 active rows 3402 -> 4032 (old site 0x4105EF20)')
    for at in EE00_B:
        add(at, b_word(at, LEAF_EE00), 'g10_ee00', False, 'A', 'function 0x4105EE00 tail: call-and-return, same post-fix')
    for at in CLS_SITES:
        add(at, bl_word(at, CLASSIFY), 'g10_cls', True, 'A', 'classifier 0x41175E5C call: type 5 movie result 10 -> 8 (old site 0x41175F48)')
    return tuple(out)


def _replan():
    return (Code(address=APPLIER_BL, old=bl_word(APPLIER_BL, APPLIER), symbol='g10_replan', link=True, cls='A', wrapper='C',
                 purpose='live-view applier call: bl 0x40F6F014 -> bl g10_replan (stored choice -> og_sync tables and plan latch, '
                         'LIVE, G2 LIVE, not recording, no ACT/TX; then the stock applier with x0-x7)',
                 set='replan', rank=1, needs_q=True, stores=True),)


def _twin():
    return (Code(address=COMPARE_2, old=bl_word(COMPARE_2, COMPARE), symbol='g10_twin_cmp', link=True, cls='A', wrapper='C',
                 purpose='second live-view compare call: bl 0x40F6E8F4 -> bl g10_twin_cmp ("equal" -> "differ" once when the '
                         'stored open-gate kind differs from the plan latch; option C, variant open_gate_twin only)',
                 set='twin', rank=1, needs_q=True, stores=True),)


RECORDS = _records() + _replan()
TWIN_RECORDS = _twin()
