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
/* The patch settings record as every arm-time gate unit reads and writes it: the
 * shared v2/v3 reader of native/feature_settings.c (Z63_SETTINGS_SHARED_V3; same
 * markers, field layout and CRC-8) plus one record-format step, the size/rate binding of the
 * open-gate bit (settings_bind.h). A gate unit includes this file in place of
 * ../../feature_settings.c, after gate_api.h. native/feature_settings.c itself is unchanged, so the
 * old-layout units and their reviewed bytes are untouched (z63patch/units.py source identities).
 *
 * Decode: a record is valid when its marker is v2 (0xa000) or v3 (0xc000), its fields are in
 * range and its check term d = check ^ crc8(payload) is 0, or, with bit 20 set, one of the six
 * pair terms. open_gate = 1 only for bit 20 with the term of the CURRENT N-RAW size and rate (the
 * setting slots of keys 0x05000F4E / 0x05000F50, one aligned load each, read only when d != 0).
 * A record bound to another pair, and a record written before the binding (bit 20 with a plain check),
 * decode as valid records with open gate off: every other field is kept.
 * Encode keeps the original's binding when open_gate is set (refused if the original has none) and
 * writes a plain check otherwise; encode_bound binds to an explicit pair (G10's picks). */
#include <stdint.h>
#include "../feature_settings.h"
#include "../nv_quality.h"
#include "settings_bind.h"
#ifndef Z63_SETTINGS_SHARED_V3
#define Z63_SETTINGS_SHARED_V3
#endif
#ifndef Z63_OG_CUR_SIZE
#define Z63_OG_CUR_SIZE() z63g_ld32(0xa03f0f110ull)   /* key 0x05000F4E, N-RAW frame size (slot proven by replay) */
#define Z63_OG_CUR_RATE() z63g_ld32(0xa03f0f118ull)   /* key 0x05000F50, N-RAW frame rate */
#endif

int z63_settings_encode_bound(uint32_t original, const struct z63_feature_settings *s,
                              uint32_t size, uint32_t rate, uint32_t *out);

static uint32_t settings_crc(uint32_t payload) {
    uint32_t crc=0;
    for(uint32_t i=0;i<2;i++) {
        crc^=(payload>>(i*8))&255u;
        for(uint32_t b=0;b<8;b++) crc=((crc<<1)^((crc&128u)?7u:0u))&255u;
    }
    return crc;
}
/* The check term of a record: check ^ crc8(payload); 0 for a plain record. */
static inline uint32_t settings_term(uint32_t word) {
    return ((word>>24)^settings_crc((word>>8)&65535u))&255u;
}

int z63_settings_valid(const struct z63_feature_settings *s) {
    return s && s->prores<4 && s->h265<5 && s->desqueeze<8 &&
        s->log_colour<3 && s->nraw<4 && s->open_gate<2;
}

int z63_settings_decode(uint32_t word, struct z63_feature_settings *out) {
    if(!out) return Z63_RECORD_FOREIGN;
    *out=(struct z63_feature_settings){0,0,0,0,0,0};
    if(!(word>>8)) return Z63_RECORD_EMPTY;
    uint32_t q=0;
    if(z63_nv_decode(word,&q)) {out->prores=q;return Z63_RECORD_LEGACY;}
    uint32_t payload=(word>>8)&65535u,d=settings_term(word);
    if(((payload&0xe000u)!=0xa000u && (payload&0xe000u)!=0xc000u) || !z63_og_term_ok(payload,d))
        return Z63_RECORD_FOREIGN;
    struct z63_feature_settings s={payload&3u,(payload>>2)&7u,
        (payload>>5)&7u,(payload>>8)&3u,(payload>>10)&3u,0u};
    if(!z63_settings_valid(&s)) return Z63_RECORD_FOREIGN;
    if(d) {
        uint32_t size=Z63_OG_CUR_SIZE(),rate=Z63_OG_CUR_RATE();
        s.open_gate=d==z63_og_bind_term(z63_og_bind_index(size,rate));
    }
    *out=s;
    return (payload&0xe000u)==0xc000u?Z63_RECORD_V3:Z63_RECORD_V2;
}

/* term: the check term written when s->open_gate is set (must be a pair term); ignored otherwise.
 * A v3 original stays v3 (its N-RAW zero is explicit); anything else is written v2. */
static int settings_encode_term(uint32_t original, const struct z63_feature_settings *s,
                                uint32_t term, uint32_t *out) {
    struct z63_feature_settings previous;
    if(!out || !z63_settings_valid(s) || (original&255u)>1 ||
       z63_settings_decode(original,&previous)==Z63_RECORD_FOREIGN) return 0;
    if(s->open_gate && z63_og_bind_find(term)>=6u) return 0;
    uint32_t payload=(((original>>8)&0xe000u)==0xc000u?0xc000u:0xa000u)|s->prores|(s->h265<<2)|(s->desqueeze<<5)|
        (s->log_colour<<8)|(s->nraw<<10)|(s->open_gate<<12);
    *out=(original&255u)|(payload<<8)|((settings_crc(payload)^(s->open_gate?term:0u))<<24);
    return 1;
}
/* Re-encode keeping the open-gate binding the original has: with s->open_gate the original must
 * carry a pair term (refused otherwise: there is no pair to bind to); without it the record is
 * plain with bit 20 clear (this is how every other group's save clears a stale binding). */
int z63_settings_encode(uint32_t original, const struct z63_feature_settings *s,
                        uint32_t *out) {
    uint32_t m=(original>>8)&0xe000u;
    return settings_encode_term(original,s,m==0xa000u||m==0xc000u?settings_term(original):0u,out);
}
/* G10's open-gate pick: with s->open_gate bind to (size, rate), which must be an eligible pair. */
int z63_settings_encode_bound(uint32_t original, const struct z63_feature_settings *s,
                              uint32_t size, uint32_t rate, uint32_t *out) {
    return settings_encode_term(original,s,z63_og_bind_term(z63_og_bind_index(size,rate)),out);
}
