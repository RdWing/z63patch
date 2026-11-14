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
"""Key material is identified whatever the file layout: the key and counter block by the SHA-256
of the bytes themselves, the 8-byte suffix by the stock file's digest (no suffix fingerprint).

Synthetic material only: random values, their fingerprints and a synthetic digest. One env-gated
case reads the user's real files (Z63_PACKAGE_KEY, Z63_STOCK_PACKAGE) and checks nothing but that
the material is found."""
import base64
import hashlib
import importlib.util
import json
import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from z63patch import package_keys as K

KEY, IV, SUF = os.urandom(32), os.urandom(16), os.urandom(8)
FP = {'key': (32, hashlib.sha256(KEY).hexdigest()), 'iv': (16, hashlib.sha256(IV).hexdigest())}
BODY = os.urandom(96)
DIGEST = hashlib.sha1(BODY + SUF).digest()


def CHECK(key, iv):
    """Synthetic stand-in for package.suffix_check: the digest of a fixed body plus the candidate."""
    assert (key, iv) == (KEY, IV)
    return lambda candidate: hashlib.sha1(BODY + candidate).digest() == DIGEST


def layouts():
    k, i, s = KEY.hex(), IV.hex(), SUF.hex()
    yield 'schema-1 shared material', json.dumps({'schema': 1, 'materials': {'aes-ctr': {'key': k, 'iv': i, 'self_check': {'key': 'x', 'iv': 'y'}}},
                                          'keys': [{'scheme': 'aes-ctr', 'model': 'Z6III', 'firmware': ['2.01'], 'k8': s}]}).encode()
    yield 'other json names', json.dumps({'aes256': k.upper(), 'counter': '0x' + i, 'digest_tail': s}).encode()
    yield 'hex lines, any order', f'suffix = {s}\nIV: {i.upper()}\nKEY={k}\n'.encode()
    yield 'bare hex lines', f'{i}\n{s}\n{k}\n'.encode()
    yield 'one concatenated hex string', (k + i + s).encode()
    yield 'concatenated, other order, odd prefix', ('f' + s + k + i).encode()
    yield 'base64', b'\n'.join(base64.b64encode(v) for v in (KEY, IV, SUF))
    yield 'base64 url-safe, no padding', b' '.join(base64.urlsafe_b64encode(v).rstrip(b'=') for v in (SUF, KEY, IV))
    sep = lambda v, j: j.join(f'{b:02x}' for b in v)
    yield 'colon-separated hex', f'key {sep(KEY, ":")}\niv {sep(IV, ":")}\nsuffix {sep(SUF, ":")}\n'.encode()
    yield 'space-separated upper hex', f'{sep(KEY, " ").upper()}\n{sep(IV, " ").upper()}\n{sep(SUF, " ").upper()}'.encode()
    yield 'C arrays', ('static const uint8_t key[32] = {' + ', '.join(f'0x{b:02x}' for b in KEY) + '};\n'
                       'uint8_t iv[] = {' + ','.join(f'0x{b:02X}' for b in IV) + '};\n'
                       'suffix = {' + ', '.join(f'0x{b:02x}' for b in SUF) + '}').encode()
    yield 'escaped byte string', ('b"' + ''.join(f'\\x{b:02x}' for b in KEY + IV + SUF) + '"').encode()
    yield 'decimal byte lists', json.dumps({'key': list(KEY), 'iv': list(IV), 'suffix': list(SUF)}).encode()
    yield 'decimal list, one per line', ('\n'.join(str(b) for b in KEY) + '\n--\n' + ' '.join(str(b) for b in IV) + '\n;\n' + ','.join(str(b) for b in SUF)).encode()
    yield 'base32', b'\n'.join(base64.b32encode(v) for v in (KEY, IV, SUF))
    wrapped = base64.encodebytes(os.urandom(40) + KEY + IV + SUF + os.urandom(50))
    yield 'line-wrapped base64 (PEM-style body)', b'-----BEGIN KEYS-----\n' + wrapped + b'-----END KEYS-----\n'
    yield 'reversed byte order', (KEY[::-1].hex() + '\n' + IV[::-1].hex() + '\n' + SUF[::-1].hex()).encode()
    yield 'raw binary', KEY + IV + SUF
    yield 'raw binary with padding', os.urandom(7) + SUF + os.urandom(100) + KEY + b'\0' * 9 + IV + os.urandom(3)


class AnyLayout(unittest.TestCase):
    def test_every_layout_yields_the_same_material(self):
        for name, data in layouts():
            m = K.find_material(data, FP, CHECK)
            self.assertEqual((m.key, m.iv, m.suffix), (KEY, IV, SUF), name)

    def test_a_wrong_or_missing_value_is_named_and_never_shown(self):
        bad = bytearray(KEY);bad[5] ^= 1
        for data, missing in ((bytes(bad).hex() + IV.hex() + SUF.hex(), 'key'),
                              (KEY.hex() + SUF.hex(), 'iv'),
                              (KEY.hex() + IV.hex(), 'suffix'),
                              ('', 'iv, key, suffix')):
            with self.assertRaises(ValueError) as c:
                K.find_material(data.encode(), FP, CHECK)
            text = str(c.exception)
            self.assertIn(missing, text)
            for secret in (KEY, IV, SUF, bytes(bad)):
                self.assertNotIn(secret.hex(), text.lower())

    def test_material_has_no_repr_of_its_values(self):
        m = K.find_material(KEY + IV + SUF, FP, CHECK)
        self.assertNotIn(KEY.hex(), repr(m));self.assertNotIn(str(KEY), repr(m))

    def test_missing_and_oversized_files_are_refused(self):
        with self.assertRaises(ValueError):K.load_material(ROOT / 'no-such-file')
        import tempfile
        with tempfile.NamedTemporaryFile() as f:
            f.write(b'0' * (K.MAX_FILE + 1));f.flush()
            with self.assertRaises(ValueError):K.load_material(f.name)

    def test_without_a_suffix_check_the_suffix_is_reported_missing(self):
        with self.assertRaisesRegex(ValueError, 'suffix'):
            K.find_material(KEY + IV + SUF, FP)

    def test_pinned_fingerprints_are_well_formed(self):
        # Only the 32- and 16-byte values are pinned: a hash of 8 bytes or fewer could be reversed.
        self.assertEqual({n: s for n, (s, _) in K.FINGERPRINTS.items()}, {'key': 32, 'iv': 16})
        for _, digest in K.FINGERPRINTS.values():
            self.assertEqual(len(bytes.fromhex(digest)), 32)


@unittest.skipUnless(os.environ.get('Z63_PACKAGE_KEY') and os.environ.get('Z63_STOCK_PACKAGE')
                     and importlib.util.find_spec('cryptography'),
                     'needs Z63_PACKAGE_KEY and Z63_STOCK_PACKAGE (the user\'s own files) and cryptography')
class RealFile(unittest.TestCase):
    def test_the_users_file_is_recognised_in_other_layouts_too(self):
        from z63patch.package import suffix_check
        check = suffix_check(Path(os.environ['Z63_STOCK_PACKAGE']).read_bytes())
        m = K.load_material(os.environ['Z63_PACKAGE_KEY'], suffix_check=check)
        for data in (m.key + m.iv + m.suffix, (m.suffix.hex() + '\n' + m.key.hex() + '\n' + m.iv.hex()).encode()):
            n = K.find_material(data, suffix_check=check)
            self.assertEqual((n.key, n.iv, n.suffix), (m.key, m.iv, m.suffix))


if __name__ == '__main__':
    unittest.main()
