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
"""Build one arm-time safety-gate stage (S1 to S6, or a composed stage S7 on such as s7-h265,
z63patch/gate.py) as an emulator MAIN. DO_NOT_FLASH.

Writes NEW-OUTPUT/MAIN.DO_NOT_FLASH.raw and NEW-OUTPUT/manifest.json; refuses an existing
output. With --check, the output must equal the reviewed recipe in profiles/. Never emits an
installable container.

The independent verifier runs on every build before anything is written: against the build's own
recipe (every structural rule; the hashes are not pinned), and with --check also against the
reviewed recipe (pinned). A refusal fails the build and writes no output. --no-verify skips it, for
emulator experiments only; the summary then says "verified": false.

    tools/build_arm_time.py STOCK_MAIN --stage s2 --output NEW-DIR [--check] [--no-verify]
"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from z63patch.arm_time import STAGES,build,recipe,write_json
from z63patch import gate
from z63patch import verify_arm_time
from z63patch import verify_gate
from z63patch.publish import publish

ROOT=Path(__file__).resolve().parents[1]


def recipe_path(stage):return ROOT/'profiles'/f'z6iii-c201-arm-time-{stage}-recipe.json'


def verify(stock,data,manifest,stage,pinned):
    """The independent verifier on a built image; raises ValueError on a refusal."""
    composed=stage in gate.STAGES
    check=verify_gate.verify if composed else verify_arm_time.verify
    try:
        out=check(stock,data,stage,recipe_data=(gate.recipe if composed else recipe)(manifest),pinned=False)
        if pinned:out=check(stock,data,stage)
    except OSError as e:raise ValueError(f'verifier refused the build: {e}') from e
    except ValueError as e:raise ValueError(f'verifier refused the build: {e}') from e
    if not out.get('accepted'):raise ValueError('verifier refused the build: not accepted')
    return out


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('main',type=Path)
    p.add_argument('--stage',choices=STAGES+gate.STAGES,required=True)
    p.add_argument('--output',type=Path)
    p.add_argument('--compiler',default='clang')
    p.add_argument('--check',action='store_true',help='require the reviewed recipe output')
    p.add_argument('--write-recipe',action='store_true',help='(review step) write the recipe for this stage')
    p.add_argument('--no-verify',action='store_true',help='skip the independent verifier (emulator experiments only)')
    a=p.parse_args()
    try:
        composed=a.stage in gate.STAGES
        stock=a.main.read_bytes()
        data,manifest=(gate.build if composed else build)(stock,a.stage,a.compiler)
        if a.no_verify:print('build_arm_time: verifier skipped (--no-verify); emulator experiments only',file=sys.stderr)
        else:verify(stock,data,manifest,a.stage,a.check)
        if a.check:
            expected=json.loads(recipe_path(a.stage).read_text())
            got=(gate.recipe if composed else recipe)(manifest)
            if (expected['output_sha256']!=got['output_sha256'] or expected['edits']!=got['edits'] or
                    expected.get('stock_digest')!=got.get('stock_digest')):
                raise ValueError('output differs from the reviewed recipe')
        if a.write_recipe:write_json(recipe_path(a.stage),(gate.recipe if composed else recipe)(manifest))
        if a.output:
            if a.output.exists() or a.output.is_symlink():raise ValueError('output already exists')
            a.output.parent.mkdir(parents=True,exist_ok=True)
            with tempfile.TemporaryDirectory(prefix='.z63-arm-',dir=a.output.parent) as t:
                staged=Path(t)/'out';staged.mkdir()
                (staged/'MAIN.DO_NOT_FLASH.raw').write_bytes(data)
                write_json(staged/'manifest.json',manifest)
                publish(staged,a.output)
        print(json.dumps({**{k:manifest[k] for k in ('stage','output_sha256','changed_bytes','regions','header')},
                          'verified':not a.no_verify},indent=2))
    except (OSError,ValueError) as e:p.exit(2,str(e)+'\n')


if __name__=='__main__':main()
