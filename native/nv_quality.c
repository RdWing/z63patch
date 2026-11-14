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
#include "nv_quality.h"
static uint32_t check(uint32_t q){
 uint32_t crc=0;
 for(uint32_t n=0;n<2;n++){
  crc^=n?q:0x63u;
  for(uint32_t k=0;k<8;k++)crc=((crc<<1)^((crc&128)?7u:0u))&255u;
 }return crc;
}
int z63_nv_decode(uint32_t word,uint32_t*q){
 if(!q)return 0;*q=0;
 uint32_t v=(word>>16)&255u;
 if(((word>>8)&255u)!=0x63u||v>3||(word>>24)!=check(v))return 0;
 *q=v;return 1;
}
int z63_nv_encode(uint32_t old,uint32_t q,uint32_t*out){
 uint32_t prior;
 if(!out||q>3||(old&255u)>1u||((old>>8)&&!z63_nv_decode(old,&prior)))return 0;
 *out=(old&255u)|0x6300u|(q<<16)|(check(q)<<24);return 1;
}
