#!/usr/bin/env python3
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
"""Independent check of an arm-time safety-gate candidate (S1 to S6, or a composed stage such as
s7-h265 through z63patch/verify_gate.py) against stock C2.01.

    tools/verify_arm_time.py STOCK_MAIN CANDIDATE_MAIN --stage s2
Exit 0 and a JSON summary when accepted; exit 2 with the reason when rejected."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from z63patch.verify_arm_time import STAGES,verify
from z63patch import verify_gate
from z63patch.gate import STAGES as COMPOSED

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('original',type=Path);p.add_argument('candidate',type=Path)
p.add_argument('--stage',choices=STAGES+COMPOSED,required=True);a=p.parse_args()
try:print(json.dumps((verify_gate.verify if a.stage in COMPOSED else verify)(a.original.read_bytes(),a.candidate.read_bytes(),a.stage),indent=2))
except (OSError,ValueError) as e:p.exit(2,str(e)+'\n')
