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
"""The arm file written by tools/make_arm_file.py (no firmware or key material needed)."""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import make_arm_file as M


class ArmFile(unittest.TestCase):
    def setUp(self):
        self.recipe = json.loads((ROOT / 'profiles/z6iii-c201-arm-time-s7-all-recipe.json').read_text())

    def test_layout_names_the_build_and_every_group(self):
        data, build_id = M.arm_file()
        self.assertEqual(len(data), 64)
        self.assertEqual(data[:8], b'Z63ARM\0\1')
        got_id, groups, flags = struct.unpack_from('<QQI', data, 8)
        self.assertEqual(f'{got_id:016x}', self.recipe['header']['build_id'])
        self.assertEqual(build_id, self.recipe['header']['build_id'])
        self.assertEqual(groups, self.recipe['groups'])                 # on/off only: every group
        self.assertEqual(flags, 0)
        self.assertEqual(data[28:60], bytes(32))
        self.assertEqual(struct.unpack_from('<I', data, 60)[0], zlib.crc32(data[:60]) & 0xFFFFFFFF)

    def test_cli_writes_once_and_refuses_an_existing_file(self):
        with tempfile.TemporaryDirectory() as t:
            out = Path(t) / 'Z63ARM.BIN'
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(M.main(['--output', str(out)]), 0)
            self.assertEqual(out.read_bytes(), M.arm_file()[0])
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                M.main(['--output', str(out)])
            self.assertEqual(caught.exception.code, 2)
            self.assertEqual(out.read_bytes(), M.arm_file()[0])


if __name__ == '__main__':
    unittest.main()
