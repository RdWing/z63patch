// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: 2026 NikonFirmwarePatch contributors
// SPDX-FileCopyrightText: 2026 RdWing
/**
 * z63patch - Nikon Z6III Modifications
 * Adapted from NikonFirmwarePatch (GPL-3.0-only); see NOTICE.md.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * @package z63patch
 * @license GPLv3 License (https://www.gnu.org/licenses/gpl-3.0.html)
 *
 *   Copyright (C) 2026 NikonFirmwarePatch contributors
 *   Copyright (C) 2026 RdWing (modifications)
 *
 */
/* C2.01 budget method derived from pinned GPL Z8 z8opengate/quality.py.
 * Use target coded dimensions and exact integer arithmetic. No Z8 rate row
 * or open-gate sensor contract is imported. */
#include "nraw_quality.h"
_Static_assert(sizeof(struct z63_nraw_row)==24,"N-RAW record extent");

int z63_nraw_budget(const struct z63_nraw_row *h,const struct z63_nraw_row *n,
                    uint32_t tier,struct z63_nraw_row *out) {
    if(!h || !n || !out || tier>3 || h->dimensions!=n->dimensions ||
       h->numerator!=n->numerator || h->denominator!=n->denominator ||
       h->flags!=1 || n->flags!=1)return 0;
    uint32_t w=h->dimensions&65535u,ht=h->dimensions>>16,num=h->numerator,den=h->denominator;
    if(!((w==6060 && ht==3410) || (w==4040 && ht==2276) || (w==3996 && ht==2248)))return 0;
    if(!((den==1001 && (num==120000 || num==60000 || num==30000 || num==24000)) ||
         (den==1000 && (num==100000 || num==50000 || num==25000))))return 0;
    if(w!=3996 && num>60000)return 0;
    if(!h->bits_per_second || !n->bits_per_second ||
       h->bits_per_second>4000000000ull || n->bits_per_second>2000000000ull)return 0;
    uint64_t pixels=(uint64_t)w*ht*num,divisor=2ull*den;
    uint64_t high=h->bits_per_second*divisor,normal=n->bits_per_second*divisor;
    if(high<=6*pixels || normal<=3*pixels)return 0;
    uint64_t a=high-6*pixels,b=normal-3*pixels,over=a>b?a:b;
    uint64_t unit=divisor*1000000ull;
    uint64_t fine=((4*pixels+over+unit-1)/unit)*1000000ull;
    uint64_t basic=((2*pixels+over+unit-1)/unit)*1000000ull;
    if(!(h->bits_per_second>fine && fine>n->bits_per_second && n->bits_per_second>basic))return 0;
    struct z63_nraw_row value=*h;
    value.bits_per_second=tier==0?h->bits_per_second:tier==1?fine:tier==2?n->bits_per_second:basic;
    *out=value;return 1;
}

int z63_nraw_configure(const struct z63_nraw_row *h,const struct z63_nraw_row *n,
                      uint32_t tier,struct z63_nraw_configuration *out) {
    struct z63_nraw_configuration value;
    if(!out || !z63_nraw_budget(h,n,tier,&value.rate))return 0;
    static const uint32_t indices[4]={2,1,7,0};
    value.encoder_index=indices[tier];value.image_quality=tier<2;
    *out=value;return 1;
}
