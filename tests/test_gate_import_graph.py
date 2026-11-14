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
"""The gate path (arm-time-all / s7-all) loads no old-layout module that holds stock bytes.

Each entry point runs in a fresh interpreter, which reports the z63patch modules in sys.modules at
exit. None may be an old-layout module (FORBIDDEN), and every one must be in GATE_MODULES, the set
a source tree for the deliverable needs (the public source tree). Without inputs only the
imports and --help run; with Z63_STOCK_MAIN and clang the s7-all build and both verifiers run in
full; with Z63_STOCK_PACKAGE and Z63_PACKAGE_KEY the keyed package build and its verification do."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

STOCK = os.environ.get('Z63_STOCK_MAIN')
NEEDS_MAIN = STOCK and Path(STOCK).is_file() and shutil.which('clang')
REAL = (os.environ.get('Z63_STOCK_PACKAGE') and os.environ.get('Z63_PACKAGE_KEY') and
        importlib.util.find_spec('cryptography') and shutil.which('clang'))
OLD_LAYOUT = importlib.util.find_spec('z63patch.verify') and importlib.util.find_spec('z63patch.log_record')
S7_ALL = '34f6e79ff23f198c03086dbed752fbb472a0f1fb7a1c5038e52bca90284927a4'

# Old-layout modules with literal stock bytes (and edit_digest, which only verify uses).
FORBIDDEN = {'z63patch.verify', 'z63patch.edit_digest', 'z63patch.log_record'}
GATE_MODULES = {'z63patch'} | {'z63patch.' + name for name in (
    'arm_time', 'deflate', 'gate', 'gate_icons', 'gate_linker', 'image', 'linker', 'log_icons',
    'package', 'package_contract', 'package_gate', 'package_keys', 'package_verify', 'publish',
    'verify_arm_time', 'verify_gate', 'video_icons', 'video_lv_icons', 'gate_groups',
    'gate_groups.desqueeze', 'gate_groups.h265', 'gate_groups.indicators', 'gate_groups.log',
    'gate_groups.marker', 'gate_groups.menu', 'gate_groups.nraw', 'gate_groups.open_gate',
    'gate_groups.open_gate_af', 'gate_groups.open_gate_entries', 'gate_groups.open_gate_geom',
    'gate_groups.open_gate_imenu', 'gate_groups.open_gate_play', 'gate_groups.open_gate_rec',
    'gate_groups.prores', 'gate_groups.viewassist', 'gate_groups.sites', 'gate_groups.sites.lv_display',
    'gate_groups.sites.nctg_class', 'gate_groups.sites.nraw_budget', 'gate_groups.sites.nraw_copy',
    'gate_groups.sites.pb_preview', 'gate_groups.sites.prores_copy', 'gate_groups.sites.video_refresh')}

RUNNER = textwrap.dedent('''
    import atexit, json, runpy, sys
    out, root, kind, target = sys.argv[1:5]
    def dump():
        with open(out, 'w') as f:
            json.dump(sorted(n for n in sys.modules if n == 'z63patch' or n.startswith('z63patch.')), f)
    atexit.register(dump)
    sys.path[:0] = [root]
    if kind == 'module':
        __import__(target)
    else:
        sys.argv = [target] + sys.argv[5:]
        sys.path.insert(0, str(__import__('pathlib').Path(target).parent))
        runpy.run_path(target, run_name='__main__')
''')


def loaded(kind, target, *args):
    """(returncode, z63patch modules loaded, output) for one entry point in a fresh interpreter."""
    with tempfile.TemporaryDirectory() as t:
        out = Path(t) / 'modules.json'
        run = subprocess.run([sys.executable, '-c', RUNNER, str(out), str(ROOT), kind, target, *map(str, args)],
                             cwd=ROOT, capture_output=True, text=True)
        return run.returncode, set(json.loads(out.read_text())), run.stdout + run.stderr


class GateImportGraph(unittest.TestCase):
    def check(self, kind, target, *args):
        code, modules, output = loaded(kind, target, *args)
        self.assertEqual(code, 0, output[-2000:])
        self.assertFalse(modules & FORBIDDEN, target)
        self.assertFalse(modules - GATE_MODULES, target)
        return modules, output

    def test_imports(self):
        for module in ('z63patch.package_gate', 'z63patch.package_contract', 'z63patch.verify_gate',
                       'z63patch.verify_arm_time', 'z63patch.gate', 'z63patch.gate_icons', 'z63patch.log_icons',
                       'z63patch.package', 'z63patch.package_verify', 'z63patch.package_keys'):
            with self.subTest(module=module):
                self.check('module', module)

    def test_cli_help(self):
        for script in ('package.py', 'tools/build_arm_time.py', 'tools/verify_arm_time.py'):
            with self.subTest(script=script):
                code, modules, output = loaded('script', str(ROOT / script), '--help')
                self.assertEqual(code, 0, output[-2000:])
                self.assertFalse(modules & FORBIDDEN, script)
                self.assertFalse(modules - GATE_MODULES, script)

    @unittest.skipUnless(OLD_LAYOUT, 'old-layout modules not in this tree')
    def test_old_layout_still_reaches_its_verifier(self):
        """The old-layout branch of verify_main still calls z63patch.verify.verify, as before."""
        from unittest import mock
        import z63patch.package_contract as PC
        with mock.patch('z63patch.verify.verify') as verify:
            PC.verify_main(b'a', b'b', 'h265-quality')
        verify.assert_called_once_with(b'a', b'b', idle_config=True, native_settings=True, retention_probe=False,
                                       precapture_only=False, settings_v2=True, h265_quality=True,
                                       metadata_recovery=False, video_menu_icons=False)
        with mock.patch('z63patch.verify.verify') as verify:
            PC.verify_main(b'a', b'a', 'stock')
        verify.assert_not_called()

    @unittest.skipUnless(OLD_LAYOUT, 'old-layout modules not in this tree')
    def test_log_record_icons_equal_log_icons(self):
        """log_record.py keeps its own copy (its SHA-256 is pinned as a control source by old-layout
        N-RAW profiles), so the two copies must stay equal. It reads the vendored upstream folder,
        log_icons the shipped assets/log-icons: the bytes must be the same."""
        from z63patch import log_icons as I, log_record as L
        for name in ('LV_ICON_IDS', 'LV_ICON_PNGS', 'LV_ICON_FILES'):
            self.assertEqual(getattr(L, name), getattr(I, name), name)
        self.assertEqual(L.lv_icons(), I.lv_icons())

    def test_log_icon_dir(self):
        from z63patch import log_icons as I
        self.assertEqual(I.ICON_DIR, ROOT / 'assets/log-icons')
        default, source = [len(raw) for raw in I.lv_icons()], I.icon_dir()
        self.assertEqual(default, [460, 638, 483, 656])
        with tempfile.TemporaryDirectory() as t:
            from unittest import mock
            with mock.patch.dict(os.environ, {'Z63_LOG_ICON_DIR': t}):
                self.assertEqual(I.icon_dir(), Path(t))
                with self.assertRaisesRegex(ValueError, 'missing Log icon slog3_fill.png'):
                    I.lv_icons()
                for name, _, _ in I.LV_ICON_FILES:
                    shutil.copyfile(source / (name + '.png'), Path(t) / (name + '.png'))
                self.assertEqual([len(raw) for raw in I.lv_icons()], default)
                raw = bytearray((Path(t) / 'log3g10_fill.png').read_bytes()); raw[-1] ^= 1
                (Path(t) / 'log3g10_fill.png').write_bytes(raw)
                with self.assertRaisesRegex(ValueError, 'upstream Log icon differs: log3g10_fill'):
                    I.lv_icons()

    @unittest.skipUnless(NEEDS_MAIN and OLD_LAYOUT, 'needs Z63_STOCK_MAIN, clang and the old-layout modules')
    def test_lv_assets_layout_matches_old_layout(self):
        from z63patch import gate_icons as GI, video_lv_icons as LV
        stock = Path(STOCK).read_bytes()
        self.assertEqual(GI._lv_assets_with_logs(stock), LV.assets(stock, log_icons=True))

    @unittest.skipUnless(NEEDS_MAIN, 'needs Z63_STOCK_MAIN and clang')
    def test_s7_all_build_and_verifiers(self):
        with tempfile.TemporaryDirectory() as t:
            out = Path(t) / 'out'
            _, output = self.check('script', str(ROOT / 'tools/build_arm_time.py'), STOCK, '--stage', 's7-all',
                                   '--check', '--output', out)
            self.assertIn(S7_ALL, output)
            self.check('script', str(ROOT / 'tools/verify_arm_time.py'), STOCK, out / 'MAIN.DO_NOT_FLASH.raw',
                       '--stage', 's7-all')

    @unittest.skipUnless(REAL, 'needs Z63_STOCK_PACKAGE, Z63_PACKAGE_KEY, cryptography and clang')
    def test_keyed_package_build_and_verify(self):
        original, key = os.environ['Z63_STOCK_PACKAGE'], os.environ['Z63_PACKAGE_KEY']
        with tempfile.TemporaryDirectory() as t:
            out = Path(t) / 'pkg'
            self.check('script', str(ROOT / 'package.py'), original, '--key-file', key, '--recipe', 'arm-time-all',
                       '--build-gate', '--output', out)
            self.check('script', str(ROOT / 'package.py'), original, '--key-file', key, '--recipe', 'arm-time-all',
                       '--verify-package', out / 'Z6_3_0201.bin.DO_NOT_FLASH')


if __name__ == '__main__':
    unittest.main()
