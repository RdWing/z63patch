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
"""Publish a staged directory atomically without replacing any destination."""
import ctypes
import errno
import os
import sys

def publish(source,destination):
    if sys.platform=='win32':
        # Windows rename rejects every existing destination, including a directory.
        # Do not use replace or a copy/delete fallback.
        os.rename(source,destination)
        return
    libc=ctypes.CDLL(None,use_errno=True)
    try:
        if sys.platform=='darwin':
            rename=libc.renamex_np
            rename.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint]
            rc=rename(os.fsencode(source),os.fsencode(destination),4) # RENAME_EXCL
        elif sys.platform.startswith('linux'):
            rename=libc.renameat2
            rename.argtypes=[ctypes.c_int,ctypes.c_char_p,ctypes.c_int,ctypes.c_char_p,ctypes.c_uint]
            rc=rename(-100,os.fsencode(source),-100,os.fsencode(destination),1) # NOREPLACE
        else:raise AttributeError('exclusive rename unavailable')
    except AttributeError as e:
        raise OSError(errno.ENOTSUP,'atomic exclusive publication unavailable') from e
    if rc:
        code=ctypes.get_errno()
        raise OSError(code,os.strerror(code),str(destination))
