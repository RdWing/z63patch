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
#ifndef Z63_SETTINGS_BIND_H
#define Z63_SETTINGS_BIND_H
#include <stdint.h>
/* Size/rate binding of the open-gate choice. Pure functions,
 * registers only, no table: used by the gate units' record reader (settings_record.c) and by the
 * hand-written field readers of groups/prores.c and groups/log.c.
 *
 * Record: bits 8-23 payload (unchanged layout; payload bit 12 = word bit 20 = open gate), bits
 * 24-31 check. The check term d = check ^ crc8(payload):
 *   d == 0                 plain record (every record written before the binding, and every record with
 *                          bit 20 clear);
 *   d == term(i), bit 20   open gate chosen for the eligible pair i (N-RAW size 6 or 4 x rate 6, 7
 *                          or 8: 6K/4K 30/25/24p), i = 3 * (size == 4) + rate - 6;
 *   anything else          foreign.
 * The six terms 0x0F 0x33 0x3C 0x55 0x5A 0x66 are distinct, nonzero, at Hamming distance >= 4 from
 * each other and from 0, and no XOR of two of {0, terms} is a single bit or the CRC syndrome of a
 * single payload bit: so any one-bit error in bits 8-31 of any valid record (plain or bound) is
 * foreign, as before the binding. */
#define Z63_OG_BIND_TERMS 0x665a553c330full   /* term(i) = byte i */
#define Z63_OG_BIND_NONE 6u

/* The pair index of (size, rate), 0..5, or Z63_OG_BIND_NONE when the pair is not eligible. */
static inline uint32_t z63_og_bind_index(uint32_t size,uint32_t rate) {
    return (size==6u||size==4u)&&rate>=6u&&rate<=8u?(size==4u?3u:0u)+rate-6u:Z63_OG_BIND_NONE;
}
/* The check term of pair i (0 for i >= 6). */
static inline uint32_t z63_og_bind_term(uint32_t i) {
    return i<6u?(uint32_t)(Z63_OG_BIND_TERMS>>(8u*i))&255u:0u;
}
/* The pair a nonzero term names, Z63_OG_BIND_NONE for none (branch-free zero-byte search, so no
 * switch table can be generated). */
static inline uint32_t z63_og_bind_find(uint32_t d) {
    d&=255u;
    if(!d)return Z63_OG_BIND_NONE;
    uint64_t x=Z63_OG_BIND_TERMS^((uint64_t)d*0x010101010101ull);
    uint64_t t=(x-0x010101010101ull)&~x&0x808080808080ull;
    return t?(uint32_t)(__builtin_ctzll(t)>>3):Z63_OG_BIND_NONE;
}
/* A check term the record format accepts for this payload. */
static inline uint32_t z63_og_term_ok(uint32_t payload,uint32_t d) {
    d&=255u;
    return !d||(((payload>>12)&1u)&&z63_og_bind_find(d)<6u);
}
#endif
