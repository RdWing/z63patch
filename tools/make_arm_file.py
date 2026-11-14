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
"""Write the arm file Z63ARM.BIN that switches the patch on.

With this file in the top-level folder of the memory card, the patched firmware switches every
feature on after power-on. Without it, the camera behaves as stock. There is no choice of
individual features: the file always switches on the whole patch.

The file is 64 bytes. It names the build it belongs to (the build ID of the reviewed arm-time-all
recipe in profiles/), so it switches on only the firmware this patcher builds from Nikon's C2.01
file. It needs no firmware and no key material. Refuses to overwrite an existing file.

    tools/make_arm_file.py --output /path/to/card/Z63ARM.BIN
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from z63patch import gate

NAME = 'Z63ARM.BIN'
STAGE = 's7-all'          # the gate stage of the package recipe arm-time-all


def arm_file():
    """The 64-byte arm file of the deliverable recipe, with every group of the build selected."""
    stage = STAGE
    recipe = json.loads((ROOT / 'profiles' / f'z6iii-c201-arm-time-{stage}-recipe.json').read_text())
    if recipe.get('status') != 'DO_NOT_FLASH' or recipe.get('stage') != stage:
        raise ValueError('the reviewed recipe in profiles/ is not the expected one')
    return gate.arm_file(recipe), recipe['header']['build_id']


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--output', type=Path, required=True, help=f'where to write the file; on the card it must be named {NAME}')
    a = p.parse_args(argv)
    try:
        data, build_id = arm_file()
        if a.output.exists() or a.output.is_symlink():
            raise ValueError('output already exists')
        with a.output.open('xb') as f:
            f.write(data)
        print(json.dumps(dict(output=str(a.output), bytes=len(data), build_id=build_id,
                              sha256=hashlib.sha256(data).hexdigest()), indent=2))
        if a.output.name.upper() != NAME:
            print(f'note: the camera looks for the name {NAME}; rename the file before copying it to the card',
                  file=sys.stderr)
        return 0
    except (OSError, ValueError, KeyError) as e:
        p.exit(2, str(e) + '\n')


if __name__ == '__main__':
    sys.exit(main())
