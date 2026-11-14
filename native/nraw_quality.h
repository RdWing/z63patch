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
#ifndef Z63_NRAW_QUALITY_H
#define Z63_NRAW_QUALITY_H
#include <stdint.h>
struct z63_nraw_row {
    uint32_t dimensions, numerator, denominator, flags;
    uint64_t bits_per_second;
};
struct z63_nraw_configuration {
    struct z63_nraw_row rate;
    uint32_t encoder_index, image_quality;
};
/* High/Fine/Normal/Basic. Input records are the matching stock pair. */
int z63_nraw_budget(const struct z63_nraw_row *high,
                    const struct z63_nraw_row *normal,
                    uint32_t tier, struct z63_nraw_row *out);
/* One selection supplies the rate, TENC index and two-tier frame enum.
 * Fine uses native High (1), and Basic uses native Normal (0). */
int z63_nraw_configure(const struct z63_nraw_row *high,
                    const struct z63_nraw_row *normal,
                    uint32_t tier, struct z63_nraw_configuration *out);
#endif
