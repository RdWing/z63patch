// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-FileCopyrightText: 2026 RdWing
/**
 * z63patch - Nikon Z6III Modifications
 * AGPLv3 Open Source. Use is subject to license terms.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * @package z63patch
 * @license AGPLv3 License (https://opensource.org/licenses/AGPL-3.0)
 *
 *   Copyright (C) 2026 RdWing
 *
 */
#ifndef Z63_FEATURE_SETTINGS_H
#define Z63_FEATURE_SETTINGS_H
#include <stdint.h>

/* New four-group format. The legacy recipes still use nv_quality.c.
 * This host-tested codec does not establish ownership or native durability. */
struct z63_feature_settings {
    uint32_t prores, h265, desqueeze, log_colour, nraw, open_gate;
};
enum z63_record_kind { Z63_RECORD_FOREIGN=-1, Z63_RECORD_EMPTY=0,
                       Z63_RECORD_LEGACY=1, Z63_RECORD_V2=2, Z63_RECORD_V3=3 };
int z63_settings_valid(const struct z63_feature_settings *s);
/* With Z63_SETTINGS_SHARED_V3 (every unit from video-publish-debounce on and
 * every N-RAW build; see feature_settings.c) marker 0xa000 is v2 and 0xc000 is
 * v3: both carry the same six fields and CRC. Without it only 0xa000 is read.
 * Any other marker or a bad CRC is foreign. */
int z63_settings_decode(uint32_t word, struct z63_feature_settings *out);
/* Preserve the ordinary low byte and, with the shared reader, the marker of a
 * v3 original (otherwise write v2). Reject unknown records, corrupt records, unknown enum values and
 * an ordinary value outside the audited 0/1 domain. */
int z63_settings_encode(uint32_t original, const struct z63_feature_settings *s,
                        uint32_t *out);
#endif
