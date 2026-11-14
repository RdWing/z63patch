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
#include "h265_quality.h"

int z63_h265_quality(uint32_t index,uint32_t tier,uint32_t *row,uint32_t bytes) {
    static const uint32_t shares[5]={100,60,45,30,20};
    if(!row || bytes!=36 || tier>=5 || index<13 || index>60 || row[0]!=1) return 0;
    uint32_t local=(index-13)%24;
    /* The table retains 8K entries; their presence does not enable this body. */
    if(local>=5 && local<=7) return 0;
    uint32_t width=local<5?5376:local<15?3840:1920;
    uint32_t height=local<5?3024:local<15?2160:1080;
    uint32_t depth=index<37?8:10;
    if(row[1]!=(width<<16|depth) || row[2]!=height || !row[7]) return 0;
    row[7]=(uint32_t)((uint64_t)row[7]*shares[tier]/100);
    return 1;
}

/* C2.01 native menu sizes differ from the encoder selector's size domain. */
uint32_t z63_h265_menu_index(uint32_t type,uint32_t size,uint32_t rate) {
    if(type!=2 && type!=3)return 0;
    uint32_t base=13+(type-2)*24;
    if(size==7 && rate>=4 && rate<=8)return base+rate-4;
    if(size==2 && rate>=2 && rate<=8)return base+8+rate-2;
    if(size==1 && rate<=8)return base+15+rate;
    return 0;
}
int z63_h265_label(uint32_t tier,uint32_t bps,char *out,uint32_t capacity) {
    static const char *const labels[5]={"100%","60%","45%","30%","20%"};
    if(tier>=5 || !out || capacity<24)return 0;
    const char *in=labels[tier];uint32_t n=0;
    while(*in)out[n++]=*in++;
    if(bps && bps<=1000000000u && bps%10000==0){
        out[n++]=' ';out[n++]='(';uint32_t value=bps/1000000,divisor=1000;
        while(divisor>1 && value<divisor)divisor/=10;
        do {out[n++]=(char)('0'+value/divisor);value%=divisor;divisor/=10;} while(divisor);
        uint32_t fraction=bps%1000000/10000;
        if(fraction){out[n++]='.';out[n++]=(char)('0'+fraction/10);if(fraction%10)out[n++]=(char)('0'+fraction%10);}
        in=" Mbps)";while(*in)out[n++]=*in++;
    }
    out[n]=0;return 1;
}
#ifdef Z63_H265_TARGET_LABELS
/* Target-figure labels (explicit --h265-target-labels unit), as the Z8 patch
 * shows them: the encoder target is the tier-scaled nominal / 1.2, rounded to
 * whole Mbps, m = ((bps / 1200) + 500) / 1000. Tier rows read
 * "<share>%  <m> Mbps" (two spaces) and the parent row value "<m>M". Without
 * a nominal (not an H.265 quality row) the tier row is the share alone and no
 * value is produced, so the caller keeps the share. */
static uint32_t z63_h265_target_mbps(uint32_t bps) {
    return bps && bps<=1000000000u?(bps/1200u+500u)/1000u:0;
}
static uint32_t z63_h265_put_number(char *out,uint32_t n,uint32_t value) {
    uint32_t divisor=1000;
    while(divisor>1 && value<divisor)divisor/=10;
    do {out[n++]=(char)('0'+value/divisor);value%=divisor;divisor/=10;} while(divisor);
    return n;
}
int z63_h265_target_label(uint32_t tier,uint32_t bps,char *out,uint32_t capacity) {
    static const char *const labels[5]={"100%","60%","45%","30%","20%"};
    if(tier>=5 || !out || capacity<24)return 0;
    const char *in=labels[tier];uint32_t n=0,mbps=z63_h265_target_mbps(bps);
    while(*in)out[n++]=*in++;
    if(mbps){
        out[n++]=' ';out[n++]=' ';n=z63_h265_put_number(out,n,mbps);
        in=" Mbps";while(*in)out[n++]=*in++;
    }
    out[n]=0;return 1;
}
int z63_h265_target_value(uint32_t bps,char *out,uint32_t capacity) {
    uint32_t mbps=z63_h265_target_mbps(bps),n;
    if(!out || capacity<8 || !mbps)return 0;
    n=z63_h265_put_number(out,0,mbps);out[n++]='M';out[n]=0;return 1;
}
#endif
