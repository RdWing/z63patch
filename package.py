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
"""Build or audit a complete C2.01 package for offline checks. Every output is DO_NOT_FLASH.

The deliverable is the gate recipe:

    package.py Z6_3_0201.bin --key-file KEYS.json --recipe arm-time-all --build-gate --output NEW-DIR

It needs only the user's own inputs: the stock Nikon Z6III C2.01 update file and the user's key
file. Where the old-layout modules are present (the research tree), the old-layout test recipes
are offered too and take a reviewed MAIN with --main. See docs/using-the-patcher.md.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys
from z63patch.package_contract import RECIPES,GATE_RECIPES,DELIVERABLE
from z63patch.package_keys import load_material

# The old-layout recipes need the old-layout verifier; without it (the public tree) only the gate
# recipe and --build-gate are offered.
OLD_LAYOUT=importlib.util.find_spec('z63patch.verify') is not None

# Not a name the camera's update scan matches: its pattern is Z6_3_???.BIN, and on exFAT the name it
# matches is the first eight characters plus the LAST dot and three characters, so a name ending in
# ".bin" (even "Z6_3_0201.DO_NOT_FLASH.bin", which reads as Z6_3_020.BIN) would be recognised.
SAFE_NAME='Z6_3_0201.bin.DO_NOT_FLASH'
INSTALLABLE_NAME='Z6_3_0201.bin'
INSTALLABLE_CONSENT='I-UNDERSTAND-THIS-IS-UNTESTED-AND-CAN-BRICK-THE-CAMERA'


def write_outputs(output,packet,report,name):
    # Reserve the new directory only after all byte and integrity checks.
    output.mkdir(mode=0o700)
    try:
        with (output/name).open('xb') as dest:dest.write(packet)
        with (output/'manifest.json').open('x') as dest:json.dump(report,dest,indent=2);dest.write('\n')
    except BaseException:
        # Only task-created filenames in our newly reserved directory.
        for n in (name,'manifest.json'):
            (output/n).unlink(missing_ok=True)
        output.rmdir();raise


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('original',type=Path,help='the stock Nikon Z6III C2.01 update file, unmodified')
    parser.add_argument('--key-file',type=Path,required=True,help="the user's own key material file, in any layout (never printed)")
    parser.add_argument('--recipe',choices=RECIPES if OLD_LAYOUT else tuple(GATE_RECIPES),required=True)
    group=parser.add_mutually_exclusive_group(required=True)
    if OLD_LAYOUT:group.add_argument('--main',type=Path,help='old-layout recipes: a reviewed MAIN image')
    group.add_argument('--build-gate',action='store_true',help=f'gate recipes ({DELIVERABLE}): build MAIN from the input itself')
    group.add_argument('--verify-package',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--installable-name',metavar='CONSENT',
                        help=f'write the package as {INSTALLABLE_NAME} instead of {SAFE_NAME}; the value must be exactly '
                             f'{INSTALLABLE_CONSENT}. The package stays DO_NOT_FLASH: nothing here is camera tested')
    parser.add_argument('--compiler',default='clang')
    args=parser.parse_args(argv)
    if not OLD_LAYOUT:args.main=None
    building=bool(args.main or args.build_gate)
    if building!=bool(args.output):parser.error('building requires --output; verification has no output directory')
    if args.build_gate and args.recipe not in GATE_RECIPES:parser.error(f'--build-gate takes a gate recipe: {", ".join(GATE_RECIPES)}')
    if args.main and args.recipe in GATE_RECIPES:parser.error('a gate recipe builds its MAIN from the input: use --build-gate')
    name=SAFE_NAME
    if args.installable_name is not None:
        if args.installable_name!=INSTALLABLE_CONSENT or not building:
            parser.error(f'--installable-name needs a build and the exact value {INSTALLABLE_CONSENT}')
        name=INSTALLABLE_NAME
    try:
        if args.output and os.path.lexists(args.output):raise FileExistsError('output path already exists')
        from z63patch.package import check_identity,suffix_check
        original=args.original.read_bytes()
        check_identity(original)                     # before any key material is read
        material=load_material(args.key_file,suffix_check=suffix_check(original))
        if args.verify_package:
            from z63patch.package_verify import verify_package
            report=verify_package(original,args.verify_package.read_bytes(),material,args.recipe)
        elif args.build_gate:
            from z63patch.package_gate import build_gate_package,build_main
            packet,report=build_gate_package(original,material,recipe=args.recipe,
                                             builder=lambda stock:build_main(stock,args.compiler))
            report['file_name']=name
            write_outputs(args.output,packet,report,name)
        else:
            from z63patch.package import build
            packet,report=build(original,args.main.read_bytes(),material,args.recipe)
            report['file_name']=name
            write_outputs(args.output,packet,report,name)
        print(json.dumps(report,indent=2))
        if building and name==INSTALLABLE_NAME:
            print(f'\n*** WARNING: {args.output/name} has a name the camera update scan recognises. It is DO_NOT_FLASH: '
                  'native admission, programming, boot and recovery are untested, and a MAIN that does not boot has no '
                  'known recovery. ***',file=sys.stderr)
        return 0
    except ImportError:
        parser.exit(2,'install requirements-container.txt in an isolated environment\n')
    except (OSError,ValueError) as error:parser.exit(2,str(error)+'\n')


if __name__=='__main__':sys.exit(main())
