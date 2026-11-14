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
"""Packaging of the single deliverable gate recipe (arm-time-all).

1. Synthetic containers (always run; no firmware, no key, no cryptography): a five-member body in
   the C2.01 layout with a toy keystream stands in for AES-CTR, so the reseal, the self-check and
   every container refusal run without any Nikon bytes.
2. Packager interlocks on the real MAIN (Z63_STOCK_MAIN and clang): the reviewed all-groups image
   passes; each interlock refuses its own mutation.
3. The real path (Z63_STOCK_PACKAGE, Z63_PACKAGE_KEY, cryptography, clang): the user's encrypted
   C2.01 file and key file give the reviewed image, resealed and self-checked, twice identically.
   Nothing is written outside a temporary directory; key material is never printed.
Nothing here is evidence about the camera."""
import binascii
import contextlib
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from z63patch import package as P
from z63patch import package_verify as PV
from z63patch import package_gate as PG
from z63patch.package_keys import Material
import package as CLI

STOCK = os.environ.get('Z63_STOCK_MAIN')
NEEDS_MAIN = STOCK and Path(STOCK).is_file() and shutil.which('clang')
REAL = (os.environ.get('Z63_STOCK_PACKAGE') and os.environ.get('Z63_PACKAGE_KEY') and
        importlib.util.find_spec('cryptography') and shutil.which('clang'))


# ---- synthetic container ----------------------------------------------------------------------
def sha(b):
    return hashlib.sha256(b).hexdigest()


def keystream(material, n):
    out = bytearray()
    i = 0
    while len(out) < n:
        out += hashlib.sha256(material.key + material.iv + i.to_bytes(8, 'big')).digest()
        i += 1
    return bytes(out[:n])


def toy_cipher(body, material):
    """Stands in for AES-CTR: the first 32 bytes clear, the rest XOR a keystream by body offset."""
    ks = keystream(material, len(body))
    return bytearray(body[:32] + bytes(a ^ b for a, b in zip(body[32:], ks[32:])))


def toy_decipher(encrypted, material):
    ks = keystream(material, len(encrypted) + 32)[32:]
    return bytes(a ^ b for a, b in zip(encrypted, ks))


def rule_tail(digest):
    """The header-tail rule, written once more for the tests."""
    s, out = digest[0], []
    for i in range(32):
        s = (214013 * s + 2531011) % 2 ** 32
        if i >= 20:
            out.append((s >> 16) & 0xFF)
    return bytes(out)


KEY = Material(bytes(range(32)), bytes(range(16)), bytes(range(8)))
NAMES = ('ex_synth.bi', '_tpj_synth.bin', 'eg_synth.bi', 'vr_synth.bi', 'li_synth.bi')
SIZES = (300, 120, 4096, 200, 500)


def synthetic(seed=1):
    """(raw package, profile, main payload) of a small container in the C2.01 layout."""
    rnd = hashlib.sha256(bytes([seed])).digest()
    payloads = [hashlib.sha256(rnd + bytes([i])).digest() * (s // 32 + 1) for i, s in enumerate(SIZES)]
    payloads = [p[:s - 2] for p, s in zip(payloads, SIZES)]
    members = [p + binascii.crc_hqx(p, 0).to_bytes(2, 'big') for p in payloads]
    body = bytearray(b' ' * 32 + struct.pack('>4I', 5, 208, 0, 0))
    offset = 208
    rows = []
    for name, m in zip(NAMES, members):
        body += struct.pack('>16s4I', name.encode().ljust(16, b'\0'), offset, len(m), 0, 0)
        rows.append(dict(name=name, offset=offset, bytes=len(m), payload_sha256=sha(m[:-2]), member_sha256=sha(m)))
        offset += len(m)
    for m in members:
        body += m
    body += binascii.crc_hqx(bytes(body), 0).to_bytes(2, 'big') + bytes(14)
    prefix = hashlib.sha256(b'prefix').digest()
    header = bytearray(prefix + hashlib.sha256(b'slots').digest() * 16 + prefix[::-1] + bytes(32))
    assert len(header) == 608
    header[576:596] = hashlib.sha1(bytes(body) + KEY.suffix).digest()
    header[596:608] = rule_tail(header[576:596])
    raw = bytes(header + toy_cipher(bytes(body), KEY))
    profile = dict(package_bytes=len(raw), original_sha256=sha(raw), body_bytes=len(body), body_sha256=sha(body), members=rows)
    return raw, profile, payloads[2]


def allowed(lo, hi):
    """A test MAIN check: the candidate may differ from stock only inside [lo, hi)."""
    def check(stock, candidate, recipe):
        if len(candidate) != len(stock) or stock[:lo] != candidate[:lo] or stock[hi:] != candidate[hi:]:
            raise ValueError('test interlock: change outside the declared range')
        return dict(test=True)
    return check


class Synthetic(unittest.TestCase):
    def setUp(self):
        self.raw, self.profile, self.main = synthetic()
        self.ident = (len(self.main), sha(self.main))
        self.kw = dict(profile=self.profile, cipher=toy_cipher, decipher=toy_decipher, main_identity=self.ident)

    def patched(self):
        m = bytearray(self.main)
        m[1000:1010] = b'PATCHED!!!'
        return bytes(m)

    def test_the_suffix_is_found_by_the_stock_digest_without_a_fingerprint(self):
        from z63patch import package_keys as K
        fp = {'key': (32, sha(KEY.key)), 'iv': (16, sha(KEY.iv))}
        check = P.suffix_check(self.raw, cipher=toy_cipher)
        decoys = [hashlib.sha256(bytes([i])).digest()[:8].hex() for i in range(5)]
        text = '\n'.join(decoys[:3] + [KEY.suffix[::-1].hex(), KEY.iv.hex(), KEY.key.hex()] + decoys[3:])
        found = K.find_material(text.encode(), fp, check)
        self.assertEqual((found.key, found.iv, found.suffix), (KEY.key, KEY.iv, KEY.suffix))
        damaged = bytearray(self.raw)
        damaged[576] ^= 1                                    # a stock digest no suffix reproduces
        with self.assertRaisesRegex(ValueError, 'suffix'):
            K.find_material(text.encode(), fp, P.suffix_check(bytes(damaged), cipher=toy_cipher))

    def test_no_edit_reproduces_the_original_exactly(self):
        packet, report = P.build(self.raw, self.main, KEY, 'stock', check_main=allowed(0, 0), **self.kw)
        self.assertEqual(packet, self.raw)
        self.assertEqual(report['status'], 'DO_NOT_FLASH')

    def test_reseal_recomputes_only_the_main_crc_body_crc_and_digest(self):
        new = self.patched()
        packet, report = P.build(self.raw, new, KEY, 'stock', check_main=allowed(1000, 1010), **self.kw)
        self.assertEqual(len(packet), len(self.raw))
        self.assertEqual(packet[:576], self.raw[:576])                 # prefix, RSA slots, mirror
        self.assertNotEqual(packet[576:596], self.raw[576:596])        # digest recomputed
        self.assertEqual(packet[596:608], rule_tail(packet[576:596]))  # tail regenerated from it
        body, members = PV.open_package(packet, KEY, self.profile, toy_decipher)
        self.assertEqual(members[2][:-2], new)
        self.assertEqual(binascii.crc_hqx(members[2], 0), 0)
        self.assertEqual(binascii.crc_hqx(body[:-14], 0), 0)
        self.assertEqual(hashlib.sha1(body + KEY.suffix).digest(), packet[576:596])
        _, old = PV.open_package(self.raw, KEY, self.profile, toy_decipher)
        for i in (0, 1, 3, 4):
            self.assertEqual(members[i], old[i])
        self.assertEqual(report['main_sha256'], sha(new))

    def test_gate_flow_manifest_and_self_check(self):
        new = self.patched()
        packet, manifest = PG.build_gate_package(self.raw, KEY, builder=lambda stock: new,
                                                 check_main=allowed(1000, 1010), **self.kw)
        again, manifest2 = PG.build_gate_package(self.raw, KEY, builder=lambda stock: new,
                                                 check_main=allowed(1000, 1010), **self.kw)
        self.assertEqual(packet, again)                                # reproducible
        self.assertEqual(manifest, manifest2)
        self.assertEqual(manifest['status'], 'DO_NOT_FLASH')
        self.assertEqual(manifest['input_sha256'], sha(self.raw))
        self.assertEqual(manifest['output_sha256'], sha(packet))
        self.assertEqual(manifest['main_sha256'], sha(new))
        self.assertEqual([m['role'] for m in manifest['members']].count('byte-identical with stock'), 4)
        for m in manifest['members']:
            if m['role'] != 'MAIN (replaced)':
                self.assertEqual(m['stock_sha256'], m['output_sha256'])
        self.assertFalse(any(manifest['qualification'].values()))
        self.assertNotIn(KEY.key.hex(), repr(manifest))
        self.assertNotIn(KEY.suffix.hex(), repr(manifest))

    # ---- refusals --------------------------------------------------------------------------------
    def test_refuses_another_file_before_reading_the_key(self):
        class Untouchable:
            def __getattr__(self, name):
                raise AssertionError('key material read before the input was identified')
        for raw in (self.raw[:-1], self.raw[:-1] + b'\0', b''):
            with self.assertRaisesRegex(ValueError, 'not the stock Nikon Z6III C2.01 update file'):
                P.checked_original(raw, Untouchable(), profile=self.profile, cipher=toy_cipher, main_identity=self.ident)

    def test_tail_rule_on_the_input_and_in_the_verifier(self):
        bad = bytearray(self.raw)
        bad[600] ^= 1
        profile = dict(self.profile, original_sha256=sha(bytes(bad)))
        with self.assertRaisesRegex(ValueError, 'tail does not follow'):
            P.checked_original(bytes(bad), KEY, profile=profile, cipher=toy_cipher, main_identity=self.ident)
        new = self.patched()
        packet, report = P.build(self.raw, new, KEY, 'stock', check_main=allowed(1000, 1010), **self.kw)
        self.assertTrue(report['header_tail_from_digest'])
        stale = bytearray(packet)
        stale[596:608] = self.raw[596:608]                             # the stock tail kept: refused
        check = dict(profile=self.profile, decipher=toy_decipher, main_identity=self.ident, check_main=allowed(1000, 1010))
        with self.assertRaisesRegex(ValueError, 'tail'):
            PV.verify_package(self.raw, bytes(stale), KEY, 'stock', **check)
        for digest in (bytes(20), bytes([255]) * 20, hashlib.sha1(b'x').digest()):
            self.assertEqual(P.header_tail(digest), rule_tail(digest))
            self.assertEqual(PV.expected_tail(digest), rule_tail(digest))

    def test_refuses_a_wrong_key(self):
        wrong = Material(bytes(32), KEY.iv, KEY.suffix)
        with self.assertRaisesRegex(ValueError, 'does not decrypt'):
            P.checked_original(self.raw, wrong, profile=self.profile, cipher=toy_cipher, main_identity=self.ident)
        wrong = Material(KEY.key, KEY.iv, bytes(8))
        with self.assertRaisesRegex(ValueError, 'digest'):
            P.checked_original(self.raw, wrong, profile=self.profile, cipher=toy_cipher, main_identity=self.ident)

    def test_refuses_when_the_main_interlock_refuses(self):
        new = self.patched()
        with self.assertRaisesRegex(ValueError, 'test interlock'):
            PG.build_gate_package(self.raw, KEY, builder=lambda stock: new, check_main=allowed(0, 10), **self.kw)
        with self.assertRaisesRegex(ValueError, 'length'):
            PG.build_gate_package(self.raw, KEY, builder=lambda stock: new + b'x', check_main=lambda *a: {}, **self.kw)

    def test_refuses_when_the_packaged_main_is_not_the_built_image(self):
        new = self.patched()
        real = P.reseal
        def swap(raw, profile, body, candidate, material, cipher):
            c = bytearray(candidate)
            c[1005] ^= 1
            return real(raw, profile, body, bytes(c), material, cipher)
        with mock.patch.object(P, 'reseal', swap), self.assertRaisesRegex(ValueError, 'packaged MAIN differs'):
            PG.build_gate_package(self.raw, KEY, builder=lambda stock: new, check_main=allowed(1000, 1010), **self.kw)

    def test_verifier_refuses_resealed_non_main_and_header_tampering(self):
        _, body = P.checked_original(self.raw, KEY, profile=self.profile, cipher=toy_cipher, main_identity=self.ident)
        check = dict(profile=self.profile, decipher=toy_decipher, main_identity=self.ident, check_main=allowed(0, 0))
        # A correctly resealed change in a non-MAIN member (CRCs and digest repaired).
        body = bytearray(body)
        row = self.profile['members'][0]
        s, e = row['offset'], row['offset'] + row['bytes']
        body[s + 10] ^= 1
        body[e - 2:e] = binascii.crc_hqx(bytes(body[s:e - 2]), 0).to_bytes(2, 'big')
        body[-16:-14] = binascii.crc_hqx(bytes(body[:-16]), 0).to_bytes(2, 'big')
        header = bytearray(self.raw[:608])
        header[576:596] = hashlib.sha1(bytes(body) + KEY.suffix).digest()
        header[596:608] = rule_tail(header[576:596])
        with self.assertRaisesRegex(ValueError, 'non-target'):
            PV.verify_package(self.raw, bytes(header + toy_cipher(bytes(body), KEY)), KEY, 'stock', **check)
        for offset in (0, 40, 300, 544, 580, 600, 608, 700, len(self.raw) - 1):
            changed = bytearray(self.raw)
            changed[offset] ^= 1
            with self.assertRaises(ValueError):
                PV.verify_package(self.raw, bytes(changed), KEY, 'stock', **check)
        with self.assertRaises(ValueError):
            PV.verify_package(self.raw, self.raw[:-1], KEY, 'stock', **check)


class Cli(unittest.TestCase):
    def run_cli(self, *args):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit) as caught:
                CLI.main(list(args))
        return caught.exception.code, err.getvalue()

    def test_option_rules(self):
        with tempfile.TemporaryDirectory() as t:
            out = str(Path(t) / 'new')
            code, err = self.run_cli('x', '--key-file', 'k', '--recipe', 'stock', '--build-gate', '--output', out)
            self.assertEqual(code, 2); self.assertIn('gate recipe' if CLI.OLD_LAYOUT else 'invalid choice', err)
            code, err = self.run_cli('x', '--key-file', 'k', '--recipe', 'arm-time-all', '--main', 'm', '--output', out)
            self.assertEqual(code, 2); self.assertIn('--build-gate', err)
            code, err = self.run_cli('x', '--key-file', 'k', '--recipe', 'arm-time-all', '--build-gate', '--output', out,
                                     '--installable-name', 'yes')
            self.assertEqual(code, 2); self.assertIn(CLI.INSTALLABLE_CONSENT, err)
            self.assertFalse(Path(out).exists())

    def test_old_layout_recipes_are_offered_only_with_the_old_layout_modules(self):
        """The public tree has no z63patch.verify: there the CLI offers the gate recipe alone."""
        import importlib
        try:
            with mock.patch('importlib.util.find_spec', return_value=None):
                importlib.reload(CLI)
            self.assertFalse(CLI.OLD_LAYOUT)
            code, err = self.run_cli('x', '--key-file', 'k', '--recipe', 'stock', '--build-gate', '--output', 'o')
            self.assertEqual(code, 2); self.assertIn('invalid choice', err)
            code, err = self.run_cli('x', '--key-file', 'k', '--recipe', 'arm-time-all', '--build-gate', '--main', 'm',
                                     '--output', 'o')
            self.assertEqual(code, 2); self.assertIn('unrecognized arguments', err)
        finally:
            importlib.reload(CLI)

    def test_wrong_input_is_named_before_the_key_file_is_opened(self):
        with tempfile.TemporaryDirectory() as t:
            other = Path(t) / 'Z6_3_0200.bin'
            other.write_bytes(b'not the C2.01 file')
            code, err = self.run_cli(str(other), '--key-file', str(Path(t) / 'missing-key.json'), '--recipe', 'arm-time-all',
                                     '--build-gate', '--output', str(Path(t) / 'new'))
            self.assertEqual(code, 2)
            self.assertIn('not the stock Nikon Z6III C2.01 update file', err)
            self.assertNotIn('material', err)
            self.assertFalse((Path(t) / 'new').exists())

    def test_safe_name_is_not_matched_by_the_update_scan(self):
        """The scan pattern Z6_3_???.BIN against the name model (exFAT: the uppercased
        first eight characters plus the last dot and three characters)."""
        import fnmatch
        def scanned(name):
            stem, dot, ext = name.upper().rpartition('.')
            return fnmatch.fnmatchcase(stem[:8] + '.' + ext[:3], 'Z6_3_???.BIN')
        self.assertTrue(scanned(CLI.INSTALLABLE_NAME))
        self.assertTrue(scanned('Z6_3_0201.DO_NOT_FLASH.bin'))       # the previous name would be seen
        self.assertFalse(scanned(CLI.SAFE_NAME))


# ---- interlocks on the real MAIN -----------------------------------------------------------------
@unittest.skipUnless(NEEDS_MAIN, 'needs Z63_STOCK_MAIN and clang')
class Interlocks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stock = Path(STOCK).read_bytes()
        cls.image = PG.build_main(cls.stock)
        from z63patch import verify_gate as VG
        cls.recipe = VG.recipe(PG.STAGE)

    def mutate(self, va, data=None):
        c = bytearray(self.image)
        o = va - PG.MAIN_BASE
        c[o:o + 4] = data if data is not None else bytes(x ^ 0xFF for x in c[o:o + 4])
        return bytes(c)

    def test_reviewed_image_passes_every_interlock(self):
        self.assertEqual(hashlib.sha256(self.image).hexdigest(), '34f6e79ff23f198c03086dbed752fbb472a0f1fb7a1c5038e52bca90284927a4')
        out = PG.check_gate_main(self.stock, self.image)
        self.assertTrue(all(out['checks'].values()))
        self.assertEqual(out['records'], 273)
        self.assertEqual(out['composition'], ['marker', 'menu', 'indicators', 'prores', 'h265', 'nraw', 'desqueeze', 'log', 'viewassist', 'open_gate'])
        self.assertEqual(sum(r['changed_bytes'] for r in out['changed_ranges']), out['changed_bytes'])

    def test_verify_gate_refusal_refuses_the_package(self):
        for va in (0x401A0010, 0x401F6008, 0x401FFF08):        # core, install table, header
            with self.assertRaises(ValueError):
                PG.check_gate_main(self.stock, self.mutate(va))

    def test_each_interlock_refuses_its_own_mutation(self):
        with self.assertRaisesRegex(ValueError, 'outside the flashed set'):
            PG.flash_rule(self.stock, self.mutate(0x40300000))
        with self.assertRaisesRegex(ValueError, 'anchor'):
            PG.inert_until_armed(self.stock, self.mutate(0x40194FF0), self.recipe)
        site = next(int(r['address'], 16) for r in self.recipe['install_table']['records'] if r['kind'] == 'code32')
        with self.assertRaisesRegex(ValueError, 'pre-installed'):
            PG.inert_until_armed(self.stock, self.mutate(site), self.recipe)
        for va in PG.DENY_WORDS:
            with self.assertRaisesRegex(ValueError, 'deny word'):
                PG.update_deny_stock(self.stock, self.mutate(va))
        with self.assertRaisesRegex(ValueError, 'protected update range'):
            PG.update_deny_stock(self.stock, self.mutate(0x41015000))
        with self.assertRaisesRegex(ValueError, 'first block'):
            PG.loader_inputs_stock(self.stock, self.mutate(0x40168400))
        with self.assertRaisesRegex(ValueError, 'first block'):
            PG.loader_inputs_stock(self.stock, self.mutate(0x40160000))
        with self.assertRaisesRegex(ValueError, 'start-up range'):
            PG.loader_inputs_stock(self.stock, self.mutate(0x41801140))
        # Even with the verifier answering "accepted", the package interlocks still refuse.
        from z63patch import verify_gate as VG
        accepted = dict(accepted=True, install_table=dict(records=273), groups=0x7ff, composition=[])
        with mock.patch.object(VG, 'verify', return_value=accepted):
            for va in (0x40300000, 0x410143EC, 0x40168400):
                with self.assertRaises(ValueError):
                    PG.check_gate_main(self.stock, self.mutate(va))

    def test_built_image_must_equal_the_reviewed_recipe(self):
        from z63patch import gate
        data, manifest = gate.build(self.stock, PG.STAGE)
        for change in (dict(output_sha256='0' * 64), dict(edits=manifest['edits'][1:]), dict(stock_digest='0' * 64)):
            with mock.patch.object(gate, 'build', return_value=(data, dict(manifest, **change))):
                with self.assertRaisesRegex(ValueError, 'reviewed recipe'):
                    PG.build_main(self.stock)


# ---- the real decoded body in a clear wrapper (no AES, no key) ---------------------------------------
BODY = os.environ.get('Z63_STOCK_BODY')


@unittest.skipUnless(NEEDS_MAIN and BODY and Path(BODY).is_file(), 'needs Z63_STOCK_MAIN, Z63_STOCK_BODY (decoded C2.01 body) and clang')
class RealBodyClearWrapper(unittest.TestCase):
    """The whole gate flow on the real decoded stock body, in memory: the real profile's members,
    the real interlocks and the pinned expected body; only the cipher is the identity and the header
    is a stand-in (so the digest suffix is a dummy). Covers everything but AES and the real suffix."""
    def test_full_flow_on_the_real_body(self):
        body = Path(BODY).read_bytes()
        profile = P.load_profile()
        self.assertEqual(sha(body), profile['body_sha256'])
        clear = lambda b, m: bytearray(b)
        decipher = lambda e, m: bytes(e)
        material = Material(bytes(32), bytes(16), b'dummysfx')
        prefix = bytes(range(32))
        header = bytearray(prefix + bytes(512) + prefix[::-1] + bytes(32))
        header[576:596] = hashlib.sha1(body + material.suffix).digest()
        header[596:608] = rule_tail(header[576:596])
        raw = bytes(header) + body
        wrapped = dict(profile, original_sha256=sha(raw))
        packet, manifest = PG.build_gate_package(raw, material, profile=wrapped, cipher=clear, decipher=decipher)
        self.assertEqual(manifest['main_sha256'], profile['gate_package']['arm-time-all']['main_sha256'])
        self.assertEqual(manifest['body_sha256'], profile['gate_package']['arm-time-all']['body_sha256'])
        self.assertEqual(manifest['gate']['records'], 273)
        self.assertEqual(packet[608:608 + 581844], raw[608:608 + 581844])
        self.assertEqual(packet[596:608], rule_tail(packet[576:596]))
        for m in manifest['members']:
            if m['role'] != 'MAIN (replaced)':
                self.assertEqual(m['stock_sha256'], m['output_sha256'])
        print(f"\nARM-TIME-ALL clear body {manifest['body_sha256']} MAIN member CRC {manifest['members'][2]['crc16']}")


@unittest.skipUnless(os.environ.get('Z63_STOCK_PACKAGE'), 'needs Z63_STOCK_PACKAGE')
class StockHeaderTail(unittest.TestCase):
    def test_stock_header_tail_follows_the_rule(self):
        """The header is clear text: no key is needed to check the rule on the user's file."""
        with open(os.environ['Z63_STOCK_PACKAGE'], 'rb') as f:
            head = f.read(608)
        self.assertEqual(head[596:608], rule_tail(head[576:596]))
        self.assertEqual(P.header_tail(head[576:596]), head[596:608])
        self.assertEqual(PV.expected_tail(head[576:596]), head[596:608])


# ---- the real path -------------------------------------------------------------------------------
@unittest.skipUnless(REAL, 'needs Z63_STOCK_PACKAGE, Z63_PACKAGE_KEY, cryptography and clang')
class RealPackage(unittest.TestCase):
    def test_user_inputs_give_the_reviewed_image_resealed_and_self_checked(self):
        from z63patch.package_keys import load_material
        raw = Path(os.environ['Z63_STOCK_PACKAGE']).read_bytes()
        material = load_material(os.environ['Z63_PACKAGE_KEY'], suffix_check=P.suffix_check(raw))
        packet, manifest = PG.build_gate_package(raw, material)
        again, _ = PG.build_gate_package(raw, material)
        self.assertEqual(packet, again)
        self.assertEqual(manifest['main_sha256'], '34f6e79ff23f198c03086dbed752fbb472a0f1fb7a1c5038e52bca90284927a4')
        self.assertEqual(manifest['status'], 'DO_NOT_FLASH')
        self.assertTrue(all(manifest['checks'].values()))
        self.assertNotIn(material.key.hex(), repr(manifest))
        self.assertEqual(packet[596:608], rule_tail(packet[576:596]))
        self.assertEqual(packet[:576], raw[:576])
        expected = P.load_profile().get('gate_package', {}).get('arm-time-all', {}).get('output_sha256')
        if expected:
            self.assertEqual(manifest['output_sha256'], expected)
        print(f"\nARM-TIME-ALL package {manifest['output_sha256']} body {manifest['body_sha256']}")

    def test_cli_writes_only_the_safe_name(self):
        with tempfile.TemporaryDirectory() as t:
            out = Path(t) / 'new'
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(CLI.main([os.environ['Z63_STOCK_PACKAGE'], '--key-file', os.environ['Z63_PACKAGE_KEY'],
                                           '--recipe', 'arm-time-all', '--build-gate', '--output', str(out)]), 0)
            self.assertEqual(sorted(p.name for p in out.iterdir()), ['Z6_3_0201.bin.DO_NOT_FLASH', 'manifest.json'])


if __name__ == '__main__':
    unittest.main()
