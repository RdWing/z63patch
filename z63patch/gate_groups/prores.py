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
"""G4, ProRes grades (native/arm_time/groups/prores.c).

Five records of its own, one coupling set ranked consumers first (plan 5.2: parser, playback
label, then the type producer, the sample description and the shooting-menu label; restore in
reverse), and two shared-site hooks (prores_copy: the four rate-copy call sites; nctg_class).
No allocation, no M set, no asset; 8 B of state (the playback grade the parser saw).

The grade is the ProRes field of the published settings word. G2 owns the menu rows and the
save; requires=(2,) (the tests still set the stored word directly).

Movie-view publication: movie_publish=True.
- Anchor: record 3 (the call 0x40D22B7C after the type store) runs once per recording start inside
  the movie dispatcher callback 0x40D22770 (bit 0, movie state 0; it sets movie state 1 at
  0x40D22D08), ACT-counted by the thunk; it takes the anchor when the start took the type front
  end 0x40D22A2C, as the old record there did. It takes the composition's anchor hold and records its grade in the
  value word (state bytes 4..7): publication is refused until 8 consecutive native-idle polls
  pass with no anchor read, or at once under Q. The rate-copy
  site 0x40D22834 runs in the same callback and so reads the same word.
- Followers: record 4 (sample description 0x40CEFD38) and the NCTG class hook use the last anchor
  value, so the file agrees with the type the recording started with.
- Publication callback z63p_g4: the global rate row 0xA03325C10 (filled at movie-subsystem start
  by task 0x40D2518C) gets the rate for the newly published grade when it holds a stock ProRes row
  or one of its scaled grades. No constant-address read of its rate field (+0x10) was found.
- Fresh readers left: the estimate copies 0x40D20DD8 (bitrate for NCTG metadata, caller
  0x40703CE0) and 0x40D22178 (remaining capacity). Their timing relative to the recording is not
  established; the emulator watch list in README section 6 covers them.

Branch-only record words: record 1 moved from the FourCC compare word
0x40CEB330 to the read BL 0x40CEB2C0 before it. Record 3 moved (branch gate/prores-qm) from
the type compare 0x40D22A2C (cmp w8,#2, a non-branch word) to the first call after the type
store, BL 0x40D22B7C: the old record changed only the value the store 0x40D22AE4 writes to
0xA03324DF4, which nothing reads before the copy 0x40CB3D5C later on the same path, so the
adapter stores the same value after the fact (TYPE_RESITE). Every G4 code record is now a B/BL
word, so G4 installs at a movie-view arming like every other group (GG.qm_ok)."""
from . import Group, Code, Shared, bl_word

FOURCC_CALL = 0x40CEB2C0     # bl BE32: the FourCC the parser tests (old record 0x40CEB330 stays stock)
BE32 = 0x40CE5E04            # frameless big-endian u32 reader; writes x0, x8, x9, x10 only
OLD_FOURCC = 0x40CEB330
OLD_TYPE = 0x40D22A2C        # the old record 3 (cmp w8,#2, a non-branch word; stock again)
TYPE_SITE = 0x40D22B7C       # record 3: bl 0x40D220D4, the first call after the type store 0x40D22AE4
TYPE_NEXT = 0x40D220D4       # its stock callee: a leaf copying 32 B from 0xA03324008 to x0
TYPE_RESITE = ('the old record 0x40D22A2C (cmp w8,#2) only made the store 0x40D22AE4 write the grade instead of 0 '
               'into 0xA03324DF4 (start block 0xA03324DDC + 0x18); every path from that store reaches the BL '
               '0x40D22B7C without reading it, and its only reader is the copy 0x40CB3D5C reached by BL '
               '0x40D22BF0 after it; the adapter at 0x40D22B7C re-tests the path (the request words, and x8 = '
               '0xA03324E00 left only on the paths through the store) and stores the grade there')

STAGES = {'s7-prores': ('marker', 'menu', 'prores')}

SET = 'grade'

GROUP = Group(
    id=4, name='prores', title='ProRes grades',
    source='native/arm_time/groups/prores.c',
    slot=(0x401BF000, 0x401C1000),
    records=(
        # The FourCC read BL before the old record 0x40CEB330 (mov w8,#0x6368, now stock).
        Code(address=FOURCC_CALL, old=bl_word(FOURCC_CALL, BE32), symbol='g4_fourcc', link=True, cls='C', wrapper='C',
             set=SET, rank=1, needs_q=True, stores=True,
             purpose='FourCC parser: bl 0x40CE5E04 -> bl g4_fourcc (the stock read, then on the path to the accept '
                     'test 0x40CEB330 apcn/apcs/apco become apch and the grade goes to G4 state)'),
        Code(address=0x4067C56C, old=bl_word(0x4067C56C, 0x40676378), symbol='g4_play_label', link=True, cls='A', wrapper='C',
             set=SET, rank=2, needs_q=False, stores=True,
             purpose='playback quality label: bl 0x40676378 -> bl g4_play_label (the stock buffer x1 = caller sp+0x128 '
                     '"ProRes 422 HQ" becomes the parsed grade\'s label)'),
        # The call after the type store, not the old record 0x40D22A2C (cmp w8,#2, now stock).
        Code(address=TYPE_SITE, old=bl_word(TYPE_SITE, TYPE_NEXT), symbol='g4_type', link=True, cls='C', wrapper='C',
             set=SET, rank=3, needs_q=True, stores=True,
             purpose='movie type: bl 0x40D220D4 -> bl g4_type (on the type front end path the anchor read; when '
                     'type 0 was stored at 0xA03324DF4, the published grade 1..3 is stored there; then the stock '
                     'call by a tail b)'),
        Code(address=0x40CEFD38, old=bl_word(0x40CEFD38, 0x40CE5C60), symbol='g4_template_call', link=True, cls='A', wrapper='C',
             set=SET, rank=4, needs_q=False, stores=False,
             purpose='MOV sample description: bl 0x40CE5C60 -> bl g4_template_call (stack copy of 0x42B7145A '
                     'with the grade\'s FourCC and name)'),
        Code(address=0x40519A8C, old=bl_word(0x40519A8C, 0x40399624), symbol='g4_shoot_label', link=True, cls='A', wrapper='C',
             set=SET, rank=5, needs_q=False, stores=False,
             purpose='shooting-menu codec label: bl 0x40399624 -> bl g4_shoot_label (0x42BEB148 becomes the '
                     'grade\'s label)'),
    ),
    shared=(Shared(site='prores_copy'), Shared(site='nctg_class')),
    requires=(2,),
    stock_calls={'stock_be32': BE32, 'stock_play_text': 0x40676378, 'stock_template': 0x40CE5C60, 'stock_shoot_label': 0x40399624,
                 'stock_type_next': TYPE_NEXT},
    state=(0x8B0, 8), hold=0x8B4,   # Anchor value word in state bytes 4..7
    publish=True,                   # z63p_g4: the global rate row in line with a newly published grade
    hook_stores=True,
    replay_tests=('tests/test_arm_time_s7_prores.py', 'tests/test_arm_time_s7_movie_publish.py'),
    movie_publish=True)
