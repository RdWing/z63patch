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
/* G2, shared menu and settings saves, on the arm-time gate. Declared by
 * z63patch/gate_groups/menu.py, whose docstring is the contract with G4-G8; linked alone at
 * 0x401B7000 with its tables in the asset 0x401A6000, so its bytes do not depend on the
 * composition. Ported from native/prores.c (z63_video_entry, z63_video_refresh, z63_sub_entry,
 * z63_open_*, z63_pick_grade, text), native/desqueeze_menu.c, native/log_menu.c,
 * native/native_settings.c (save and observer, now save_api.h) and the N-RAW tier page of
 * experimental/nraw/nraw_native_menu.inc. Changes against the old layout:
 *  - every stock call is a direct BL to a declared stock function (no PTR casts);
 *  - SETTING_GET and the size/rate getters are gone (they reach the flash set): the file type
 *    and the N-RAW class keys are read from their RAM slots (setting_slots.h);
 *  - the menu shows the stored settings word, never the published one; G2 does not publish;
 *  - "Reset patch settings" is the last option of the quality sub-page, no 49th row;
 *  - a row or option of another group is live only while that group is (z63g_phase != 0);
 *  - inserted item ids are ones no stock row uses;
 *  - H.265 options are percentages only (no size/rate getter).
 *
 * The stock-derived tables come from a header generated at build time from the user's MAIN and
 * force-included by the compiler (Z63_G2_STOCK); they are never in the repository. */
#include <stdint.h>
#define Z63_SETTINGS_SHARED_V3
#include "../gate_api.h"
#include "../settings_record.c"
#include "../../nv_quality.c"
#include "../save_api.h"
#include "../setting_slots.h"
#include "../sites/video_refresh.h"
#ifndef Z63_G2_STOCK
#error "G2 needs the stock tables its declaration generates from the user's MAIN (Group.stock_header, force-included by z63patch/gate.py link_group)"
#endif
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;

#define G2 2u
#define G4 4u
#define G5 5u
#define G6 6u
#define G7 7u
#define G8 8u
#define SUB_PAGE      0xa028cb2c0ull
#define SUB_ELEMENTS  (SUB_PAGE+8u)
#define SUB_ITEMS     (SUB_PAGE+16u)
#define SUB_EVENTS    (SUB_PAGE+24u)
#define SUB_COUNT     (SUB_PAGE+32u)
#define STOCK_ELEMENTS 0x43cb7c58ull
#define STOCK_ITEMS    0x43cb7d68ull
#define STOCK_EVENTS   0x43cb7dc0ull
#define STYLE_WORDS   0xa03429680ull           /* text-slot descriptors, 24 B each, style index at +16 */
#define FONT_CACHE    0xa0342b2a0ull
#define FONT_RECORD   (Z63_STATE_G2+0x60u)     /* 16 B private font record, 16-aligned in the page */
#define TITLE_WIDGET  0x82cu
#define VALUE_WIDGET  0x4dbeu
#define NRAW_LABEL    2185u                    /* stock N-RAW quality label: the font the rows inherit */

#define ITEM_VIDEO 10675u
#define ITEM_OPT_0 11601u
#define ITEM_OPT_1 11603u
#define ITEM_OPT_2 11671u
#define ITEM_OPT_3 11673u
#define ITEM_OPT_4 11675u
#define ITEM_RESET 11717u
#define ITEM_DESQ  11677u
#define ITEM_DQ_0  11679u
#define ITEM_DQ_1  11681u
#define ITEM_DQ_2  11683u
#define ITEM_DQ_3  11685u
#define ITEM_DQ_4  11719u
#define ITEM_DQ_5  11749u
#define ITEM_DQ_6  11751u
#define ITEM_DQ_7  11761u
#define ITEM_LOG   11763u
#define ITEM_LG_0  11605u
#define ITEM_LG_1  11780u
#define ITEM_LG_2  11720u

/* Text slots (TEXT_SLOTS[2] = 252-284). */
#define S_Q_LABEL 252u
#define S_Q_VALUE 253u
#define S_OPT_0   254u
#define S_OPT_1   255u
#define S_OPT_2   256u
#define S_OPT_3   257u
#define S_TITLE   258u
#define S_OPT_4   259u
#define S_DQ_LABEL 260u
#define S_DQ_VALUE 261u
#define S_DQ_0    262u                          /* 262..269 */
#define S_LG_LABEL 276u
#define S_LG_VALUE 277u
#define S_LG_0    278u                          /* 278..280 */
#define S_RESET   281u

struct nav {void *window;u32 code,next;};

u64 stock_video_entry(void *window);
u64 stock_sub_entry(void *window);
u32 stock_save_worker(u32 mode);
void *stock_row_value(void *window,u32 item,u32 widget);
void stock_text_store(const char *text,u32 slot,u32 style);
void stock_text_bind(void *widget,u32 slot);
void *stock_widget_find(u32 id,void *list);
void stock_widget_show(void *widget,u32 on);
void stock_row_enable(void *row,u32 disabled);
u32 stock_entry_guard(void);
void stock_set_cursor(void *window,u32 item);
u64 stock_set_mark(void *window,u32 item,u32 on);
u32 stock_parent_guard(void);
u64 stock_font_cache(void);
u32 stock_language(void);
u32 stock_font_face(u32 language,u32 label);
u32 stock_font_size(u32 language,u32 label);
void stock_font_align(u32 language,u32 label,u8 *horizontal,u8 *vertical);

/* Every entry point, declared before the tables that hold their addresses. */
u64 z63_g2_video_entry(void *window);
u64 z63_g2_sub_entry(void *window);
u32 z63_g2_save_result(u32 mode);
void z63_g2_open_nraw(struct nav *n);
void z63_g2_open_quality(struct nav *n);
void z63_g2_open_desqueeze(struct nav *n);
void z63_g2_open_log(struct nav *n);
u32 z63_g2_guard_desqueeze(void);
u32 z63_g2_guard_desqueeze_parent(void);
u32 z63_g2_guard_log(void);
u32 z63_g2_guard_log_parent(void);
void z63_g2_pick_quality(struct nav *n);
void z63_g2_pick_reset(struct nav *n);
void z63_g2_pick_desqueeze(struct nav *n);
void z63_g2_pick_log(struct nav *n);

/* ---- tables (asset 0x401A6000) ---- */
#define ASSET __attribute__((section(".z63asset.401a6000"),used,aligned(16)))
#define FN(f) ((u64)(uintptr_t)(f))
#define ROW(a,b,c,d,e,f,g,h,i,j,k) {a,b,c,d,e,f,g,h,i,j,k},
/* An inserted row: the stock N-RAW quality row (index 9) with its label slot, value slot and
 * item id, and flag 0x200 set (prores.py build_assets). */
#define INSERT(label,value,item) (a,b,c,d,e,f,g,h,i,j,k) {label,b,c,d,(e)|0x200u,value,item,h,i,j,k},
#define ROW_Q(...)  INSERT_(S_Q_LABEL,S_Q_VALUE,ITEM_VIDEO,__VA_ARGS__)
#define ROW_DQ(...) INSERT_(S_DQ_LABEL,S_DQ_VALUE,ITEM_DESQ,__VA_ARGS__)
#define ROW_LG(...) INSERT_(S_LG_LABEL,S_LG_VALUE,ITEM_LOG,__VA_ARGS__)
#define INSERT_(label,value,item,a,b,c,d,e,f,g,h,i,j,k) {label,b,c,d,(e)|0x200u,value,item,h,i,j,k},
ASSET const u32 z63_g2_rows[48][11]={
    Z63_G2_ROW_0(ROW) Z63_G2_ROW_1(ROW) Z63_G2_ROW_2(ROW) Z63_G2_ROW_3(ROW) Z63_G2_ROW_4(ROW)
    Z63_G2_ROW_5(ROW) Z63_G2_ROW_6(ROW) Z63_G2_ROW_7(ROW) Z63_G2_ROW_8(ROW) Z63_G2_ROW_9(ROW)
    Z63_G2_ROW_9(ROW_Q) Z63_G2_ROW_9(ROW_DQ) Z63_G2_ROW_9(ROW_LG)
    Z63_G2_ROW_10(ROW) Z63_G2_ROW_11(ROW) Z63_G2_ROW_12(ROW) Z63_G2_ROW_13(ROW) Z63_G2_ROW_14(ROW)
    Z63_G2_ROW_15(ROW) Z63_G2_ROW_16(ROW) Z63_G2_ROW_17(ROW) Z63_G2_ROW_18(ROW) Z63_G2_ROW_19(ROW)
    Z63_G2_ROW_20(ROW) Z63_G2_ROW_21(ROW) Z63_G2_ROW_22(ROW) Z63_G2_ROW_23(ROW) Z63_G2_ROW_24(ROW)
    Z63_G2_ROW_25(ROW) Z63_G2_ROW_26(ROW) Z63_G2_ROW_27(ROW) Z63_G2_ROW_28(ROW) Z63_G2_ROW_29(ROW)
    Z63_G2_ROW_30(ROW) Z63_G2_ROW_31(ROW) Z63_G2_ROW_32(ROW) Z63_G2_ROW_33(ROW) Z63_G2_ROW_34(ROW)
    Z63_G2_ROW_35(ROW) Z63_G2_ROW_36(ROW) Z63_G2_ROW_37(ROW) Z63_G2_ROW_38(ROW) Z63_G2_ROW_39(ROW)
    Z63_G2_ROW_40(ROW) Z63_G2_ROW_41(ROW) Z63_G2_ROW_42(ROW) Z63_G2_ROW_43(ROW) Z63_G2_ROW_44(ROW)
};
_Static_assert(sizeof(z63_g2_rows)==48u*44u,"48 rows of 44 bytes");

/* Parent transitions: the stock N-RAW row's table with its sub-page transitions (flag 0x119)
 * calling `open`, guard `g0` where stock has none and `gp` where stock has the parent guard
 * 0x403C2D14; event 0x2AE gets guard 0x403C2D0C (prores.py entry_rows). */
#define PARENT(open,g0,gp,e,h,g,f) {e,(f)==0x119u?FN(open):(h),(e)==0x2aeu?0x403c2d0cull:(f)==0x119u?((g)?(gp):(g0)):(g),f},
#define T_NRAW(...) PARENT(z63_g2_open_nraw,0ull,0x403c2d14ull,__VA_ARGS__)
#define T_Q(...)    PARENT(z63_g2_open_quality,0ull,0x403c2d14ull,__VA_ARGS__)
#define T_DQ(...)   PARENT(z63_g2_open_desqueeze,FN(z63_g2_guard_desqueeze),FN(z63_g2_guard_desqueeze_parent),__VA_ARGS__)
#define T_LG(...)   PARENT(z63_g2_open_log,FN(z63_g2_guard_log),FN(z63_g2_guard_log_parent),__VA_ARGS__)
ASSET static const u64 g2_trans_nraw[Z63_G2_NRAW_ROWS][4]={Z63_G2_NRAW(T_NRAW)};
ASSET static const u64 g2_trans_quality[Z63_G2_NRAW_ROWS][4]={Z63_G2_NRAW(T_Q)};
ASSET static const u64 g2_trans_desqueeze[Z63_G2_NRAW_ROWS][4]={Z63_G2_NRAW(T_DQ)};
ASSET static const u64 g2_trans_log[Z63_G2_NRAW_ROWS][4]={Z63_G2_NRAW(T_LG)};

ASSET const u64 z63_g2_events[48]={
    Z63_G2_EVENT_0,Z63_G2_EVENT_1,Z63_G2_EVENT_2,Z63_G2_EVENT_3,Z63_G2_EVENT_4,
    Z63_G2_EVENT_5,Z63_G2_EVENT_6,Z63_G2_EVENT_7,Z63_G2_EVENT_8,FN(g2_trans_nraw),
    FN(g2_trans_quality),FN(g2_trans_desqueeze),FN(g2_trans_log),
    Z63_G2_EVENT_10,Z63_G2_EVENT_11,Z63_G2_EVENT_12,Z63_G2_EVENT_13,Z63_G2_EVENT_14,
    Z63_G2_EVENT_15,Z63_G2_EVENT_16,Z63_G2_EVENT_17,Z63_G2_EVENT_18,Z63_G2_EVENT_19,
    Z63_G2_EVENT_20,Z63_G2_EVENT_21,Z63_G2_EVENT_22,Z63_G2_EVENT_23,Z63_G2_EVENT_24,
    Z63_G2_EVENT_25,Z63_G2_EVENT_26,Z63_G2_EVENT_27,Z63_G2_EVENT_28,Z63_G2_EVENT_29,
    Z63_G2_EVENT_30,Z63_G2_EVENT_31,Z63_G2_EVENT_32,Z63_G2_EVENT_33,Z63_G2_EVENT_34,
    Z63_G2_EVENT_35,Z63_G2_EVENT_36,Z63_G2_EVENT_37,Z63_G2_EVENT_38,Z63_G2_EVENT_39,
    Z63_G2_EVENT_40,Z63_G2_EVENT_41,Z63_G2_EVENT_42,Z63_G2_EVENT_43,Z63_G2_EVENT_44,
};

/* Option transitions: the stock sub-page table with its OK action replaced by a pick. */
#define PICK(pick,e,h,g,f) {e,(h)==Z63_G2_SUB_OK?FN(pick):(h),(e)==0x2aeu?0x403c2d0cull:(g),f},
#define P_Q(...)  PICK(z63_g2_pick_quality,__VA_ARGS__)
#define P_R(...)  PICK(z63_g2_pick_reset,__VA_ARGS__)
#define P_DQ(...) PICK(z63_g2_pick_desqueeze,__VA_ARGS__)
#define P_LG(...) PICK(z63_g2_pick_log,__VA_ARGS__)
ASSET static const u64 g2_ok_quality[Z63_G2_SUB_ROWS][4]={Z63_G2_SUB_EVENTS(P_Q)};
ASSET static const u64 g2_ok_reset[Z63_G2_SUB_ROWS][4]={Z63_G2_SUB_EVENTS(P_R)};
ASSET static const u64 g2_ok_desqueeze[Z63_G2_SUB_ROWS][4]={Z63_G2_SUB_EVENTS(P_DQ)};
ASSET static const u64 g2_ok_log[Z63_G2_SUB_ROWS][4]={Z63_G2_SUB_EVENTS(P_LG)};

/* Sub-page elements: stock, with the title element on text slot 258 (prores.py). */
#define ELEMENT(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q) {(b)==1u?S_TITLE:(a),(b)==1u?2u:(b),c,d,e,f,g,h,i,j,k,l,m,n,o,p,q},
ASSET static const u32 g2_elements[4][17]={Z63_G2_ELEMENTS(ELEMENT)};

/* Option rows: the stock sub-page row 0 with its label slot, item id and flag 0x200. */
#define OPTION(slot,id,a,b,c,d,e,f,g,h,i,j,k) {slot,b,c,d,(e)|0x200u,f,id,h,i,j,k},
#define OPT(slot,id) Z63_G2_SUB_ITEM(OPTION,slot,id)
/* Quality page A (ProRes or N-RAW: four options, then Reset) and B (H.265: five, then Reset);
 * the Reset-only page is the last row of A. */
ASSET static const u32 g2_items_qa[5][11]={OPT(S_OPT_0,ITEM_OPT_0) OPT(S_OPT_1,ITEM_OPT_1) OPT(S_OPT_2,ITEM_OPT_2)
                                          OPT(S_OPT_3,ITEM_OPT_3) OPT(S_RESET,ITEM_RESET)};
ASSET static const u32 g2_items_qb[6][11]={OPT(S_OPT_0,ITEM_OPT_0) OPT(S_OPT_1,ITEM_OPT_1) OPT(S_OPT_2,ITEM_OPT_2)
                                          OPT(S_OPT_3,ITEM_OPT_3) OPT(S_OPT_4,ITEM_OPT_4) OPT(S_RESET,ITEM_RESET)};
ASSET static const u32 g2_items_dq[8][11]={OPT(S_DQ_0,ITEM_DQ_0) OPT(S_DQ_0+1u,ITEM_DQ_1) OPT(S_DQ_0+2u,ITEM_DQ_2)
                                          OPT(S_DQ_0+3u,ITEM_DQ_3) OPT(S_DQ_0+4u,ITEM_DQ_4) OPT(S_DQ_0+5u,ITEM_DQ_5)
                                          OPT(S_DQ_0+6u,ITEM_DQ_6) OPT(S_DQ_0+7u,ITEM_DQ_7)};
ASSET static const u32 g2_items_lg[3][11]={OPT(S_LG_0,ITEM_LG_0) OPT(S_LG_0+1u,ITEM_LG_1) OPT(S_LG_0+2u,ITEM_LG_2)};
ASSET static const u64 g2_events_qa[5]={FN(g2_ok_quality),FN(g2_ok_quality),FN(g2_ok_quality),FN(g2_ok_quality),FN(g2_ok_reset)};
ASSET static const u64 g2_events_qb[6]={FN(g2_ok_quality),FN(g2_ok_quality),FN(g2_ok_quality),FN(g2_ok_quality),FN(g2_ok_quality),
                                        FN(g2_ok_reset)};
ASSET static const u64 g2_events_dq[8]={FN(g2_ok_desqueeze),FN(g2_ok_desqueeze),FN(g2_ok_desqueeze),FN(g2_ok_desqueeze),
                                        FN(g2_ok_desqueeze),FN(g2_ok_desqueeze),FN(g2_ok_desqueeze),FN(g2_ok_desqueeze)};
ASSET static const u64 g2_events_lg[3]={FN(g2_ok_log),FN(g2_ok_log),FN(g2_ok_log)};
#define ITEMS_QA    ((u64)(uintptr_t)g2_items_qa)
#define ITEMS_QB    ((u64)(uintptr_t)g2_items_qb)
#define ITEMS_RESET ((u64)(uintptr_t)g2_items_qa[4])
#define ITEMS_DQ    ((u64)(uintptr_t)g2_items_dq)
#define ITEMS_LG    ((u64)(uintptr_t)g2_items_lg)

/* ---- reads ---- */
enum {M_NONE=0,M_PRORES=1,M_H265=2,M_NRAW=3};
enum {PG_NONE=0,PG_QA,PG_QB,PG_RESET,PG_DQ,PG_LG};
static u32 live(u64 p,u32 group){return p&&z63g_phase(p,group)?1u:0u;}
/* The quality page's mode: the file type (its RAM slot) and whether its owner group is live. */
static u32 mode(u64 p) {
    u32 type=z63g_ld32(Z63_SLOT_FILE_TYPE);
    if(type==4u)return live(p,G4)?M_PRORES:M_NONE;
    if(type==2u||type==3u)return live(p,G5)?M_H265:M_NONE;
    if(type==6u)return live(p,G6)?M_NRAW:M_NONE;
    return M_NONE;
}
/* The stored settings record (never the published copy). 0: foreign (shown as "CFG error"). */
static u32 stored(struct z63_feature_settings *v) {
    return z63_settings_decode(z63g_acq32(Z63S_WORD),v)!=Z63_RECORD_FOREIGN;
}
/* N-RAW tier as G6 applies it: the v3 field, else Nikon's class (High 0 when key 0x0F55 is 1). */
static u32 nraw_tier(void) {
    struct z63_feature_settings v;
    if(z63_settings_decode(z63g_acq32(Z63S_WORD),&v)==Z63_RECORD_V3)return v.nraw;
    return z63g_ld32(Z63_SLOT_KEY_0F55)==0u?2u:0u;
}
static u32 page_of(u64 items) {
    if(items==ITEMS_QA)return PG_QA;
    if(items==ITEMS_QB)return PG_QB;
    if(items==ITEMS_RESET)return PG_RESET;
    if(items==ITEMS_DQ)return PG_DQ;
    if(items==ITEMS_LG)return PG_LG;
    return PG_NONE;
}

/* ---- text ---- */
/* The private font record (prores.c text()): the inserted rows take the face, size and
 * alignment of the stock N-RAW quality label. Our slots only; style 4 only. */
__attribute__((noinline)) static void g2_font(u64 p,u32 slot) {
    if(!p||slot<S_Q_LABEL||slot>S_RESET)return;
    u64 cache=stock_font_cache();
    u64 record=p+FONT_RECORD;
    if(cache!=FONT_CACHE||record<cache||((record-cache)&15u)||(record-cache)/16u>0x7fffffffu)return;
    u32 language=stock_language();
    if(language>=32u)return;
    u32 face=stock_font_face(language,NRAW_LABEL),size=stock_font_size(language,NRAW_LABEL);
    u8 horizontal=0,vertical=0;
    stock_font_align(language,NRAW_LABEL,&horizontal,&vertical);
    if(face>=24u||!size||size>64u||!horizontal||vertical!=100u)return;
    volatile u64 *to=(volatile u64 *)(uintptr_t)record;
    const volatile u64 *from=(const volatile u64 *)(uintptr_t)(cache+4u*16u);
    u64 lo=from[0],hi=from[1];
    lo=(lo&~0xffffffull)|face|(u64)size<<8|(u64)horizontal<<16;
    to[0]=lo;to[1]=hi;
    __atomic_store_n((u32 *)(uintptr_t)(STYLE_WORDS+slot*24u+16u),(u32)((record-cache)/16u),__ATOMIC_RELEASE);
}
#define TEXT(p,t,slot) do{stock_text_store((t),(slot),4u);g2_font((p),(slot));}while(0)

/* Every text and id table in the asset: the verifier scans the group's rodata as code. */
#define STR(name,text) ASSET static const char name[]=text;
STR(t_hq,"HQ") STR(t_std,"Standard") STR(t_lt,"LT") STR(t_proxy,"Proxy")
STR(t_100,"100%") STR(t_60,"60%") STR(t_45,"45%") STR(t_30,"30%") STR(t_20,"20%")
STR(t_high,"High") STR(t_fine,"Fine") STR(t_normal,"Normal") STR(t_basic,"Basic")
STR(t_dq1,"1.25x") STR(t_dq2,"1.30x") STR(t_dq3,"1.33x") STR(t_dq4,"1.50x") STR(t_dq5,"1.60x")
STR(t_dq6,"1.80x") STR(t_dq7,"2.00x")
STR(t_nlog,"N-Log") STR(t_slog3,"S-Log3") STR(t_log3g10,"Log3G10")
STR(off_text,"Off") STR(cfg_error,"CFG error") STR(save_failed,"Save failed") STR(reset_text,"Reset patch settings")
STR(t_title_prores,"Video quality (ProRes)") STR(t_title_h265,"Video quality (H.265)") STR(t_title_nraw,"N-RAW tier")
STR(t_title_none,"Patch settings") STR(t_desqueeze,"Desqueeze") STR(t_log,"Log type")
ASSET static const char *const prores_labels[4]={t_hq,t_std,t_lt,t_proxy};
ASSET static const char *const h265_labels[5]={t_100,t_60,t_45,t_30,t_20};
ASSET static const char *const nraw_labels[4]={t_high,t_fine,t_normal,t_basic};
ASSET static const char *const dq_labels[8]={off_text,t_dq1,t_dq2,t_dq3,t_dq4,t_dq5,t_dq6,t_dq7};
ASSET static const char *const lg_labels[3]={t_nlog,t_slog3,t_log3g10};
ASSET static const u32 ids_quality[6]={ITEM_OPT_0,ITEM_OPT_1,ITEM_OPT_2,ITEM_OPT_3,ITEM_OPT_4,ITEM_RESET};
ASSET static const u32 ids_dq[8]={ITEM_DQ_0,ITEM_DQ_1,ITEM_DQ_2,ITEM_DQ_3,ITEM_DQ_4,ITEM_DQ_5,ITEM_DQ_6,ITEM_DQ_7};
ASSET static const u32 ids_lg[3]={ITEM_LG_0,ITEM_LG_1,ITEM_LG_2};
ASSET static const u32 ids_reset_qa[5]={0,0,0,0,ITEM_RESET};
ASSET static const u32 ids_reset_qb[6]={0,0,0,0,0,ITEM_RESET};

static const char *quality_title(u32 m) {
    return m==M_PRORES?t_title_prores:m==M_H265?t_title_h265:m==M_NRAW?t_title_nraw:t_title_none;
}
/* Labels of the quality page's options for the mode (slots 254-257, 259, 281). */
static void quality_options(u64 p,u32 m) {
    const char *const *l=m==M_H265?h265_labels:m==M_NRAW?nraw_labels:prores_labels;
    TEXT(p,l[0],S_OPT_0);TEXT(p,l[1],S_OPT_1);TEXT(p,l[2],S_OPT_2);TEXT(p,l[3],S_OPT_3);
    TEXT(p,h265_labels[4],S_OPT_4);
    TEXT(p,reset_text,S_RESET);
}
/* Row labels and values (slots 252, 253, 260, 261, 276, 277). */
__attribute__((noinline)) static void row_texts(u64 p) {
    struct z63_feature_settings v;
    u32 ok=stored(&v),m=mode(p);
    TEXT(p,quality_title(m),S_Q_LABEL);
    const char *q=off_text;
    if(!ok||z63g_acq32(p+Z63S_FATAL))q=cfg_error;
    else if(m==M_PRORES)q=prores_labels[v.prores&3u];
    else if(m==M_H265)q=h265_labels[v.h265<5u?v.h265:0u];
    else if(m==M_NRAW)q=nraw_labels[nraw_tier()&3u];
    TEXT(p,q,S_Q_VALUE);
    TEXT(p,t_desqueeze,S_DQ_LABEL);
    TEXT(p,!live(p,G7)?off_text:!ok?cfg_error:dq_labels[v.desqueeze&7u],S_DQ_VALUE);
    TEXT(p,t_log,S_LG_LABEL);
    TEXT(p,!live(p,G8)?off_text:!ok?cfg_error:lg_labels[v.log_colour<3u?v.log_colour:0u],S_LG_VALUE);
}
static void bind_row(void *window,u32 item,u32 slot,u32 disabled) {
    void *w=stock_row_value(window,item,VALUE_WIDGET);
    if(w)stock_text_bind(w,slot);
    void *row=stock_widget_find(item,(u8 *)window+0x18);
    if(row)stock_row_enable(row,disabled);
}

/* ---- handler words ---- */
/* Entry 0xA028F55E0: the texts (LIVE or DRAINING), then the stock entry. */
u64 z63_g2_video_entry(void *window) {
    u64 p=z63g_page();
    if(p&&z63g_phase(p,G2))row_texts(p);
    return stock_video_entry(window);
}
/* Site video_refresh, after the stock refresh (the core adapter calls it only in LIVE or
 * DRAINING with G2 selected and not broken, phase unchanged across the stock call). */
void z63h_video_refresh_g2(u64 page,u32 phase,void *window) {
    (void)phase;
    row_texts(page);
    bind_row(window,ITEM_VIDEO,S_Q_VALUE,0u);
    bind_row(window,ITEM_DESQ,S_DQ_VALUE,live(page,G7)?0u:1u);
    bind_row(window,ITEM_LOG,S_LG_VALUE,live(page,G8)?0u:1u);
}
/* Sub-page entry 0xA028F5928: a G2 page (descriptor items on one of G2's tables) in LIVE or
 * DRAINING; any other page, or any other phase, the stock entry. The title, cursor and mark
 * only: stock has already built the option rows from their text slots when it calls this
 * handler, so the option labels are stored by the open callbacks below. */
u64 z63_g2_sub_entry(void *window) {
    u64 p=z63g_page();
    u32 pg=p&&z63g_phase(p,G2)?page_of(z63g_ld64(SUB_ITEMS)):PG_NONE;
    if(pg==PG_NONE)return stock_sub_entry(window);
    struct z63_feature_settings v;
    u32 ok=stored(&v),id;
    if(pg==PG_DQ) {
        stock_text_store(t_desqueeze,S_TITLE,3u);
        id=ids_dq[ok?v.desqueeze&7u:0u];
    } else if(pg==PG_LG) {
        stock_text_store(t_log,S_TITLE,3u);
        id=ids_lg[ok&&v.log_colour<3u?v.log_colour:0u];
    } else {
        u32 m=pg==PG_RESET?M_NONE:mode(p);
        stock_text_store(quality_title(m),S_TITLE,3u);
        id=m==M_PRORES&&ok?ids_quality[v.prores&3u]:m==M_H265&&ok?ids_quality[v.h265<5u?v.h265:0u]:
           m==M_NRAW?ids_quality[nraw_tier()&3u]:m==M_NONE?ITEM_RESET:ids_quality[0];
    }
    void *title=stock_widget_find(TITLE_WIDGET,(u8 *)window+0x18);
    if(title) {stock_text_bind(title,S_TITLE);stock_widget_show(title,1u);}
    if(stock_entry_guard()!=1u)stock_set_cursor(window,id);
    return stock_set_mark(window,id,1u);
}

/* ---- event-table callbacks (reached only through G2's tables) ---- */
/* Not inlined: an open callback then builds only the publication word's address before its
 * first access (the verifier tracks that address through movz/movk only). */
__attribute__((noinline)) static void descriptor(u64 elements,u64 items,u64 events,u16 count) {
    *(volatile u64 *)(uintptr_t)SUB_ELEMENTS=elements;
    *(volatile u64 *)(uintptr_t)SUB_ITEMS=items;
    *(volatile u64 *)(uintptr_t)SUB_EVENTS=events;
    *(volatile u16 *)(uintptr_t)SUB_COUNT=count;
}
/* The stock N-RAW quality row: always the stock sub-page (the descriptor a G2 page left). */
void z63_g2_open_nraw(struct nav *n) {
    (void)n;(void)z63g_page();
    descriptor(STOCK_ELEMENTS,STOCK_ITEMS,STOCK_EVENTS,2u);
}
/* The open callbacks also store the page's option labels (LIVE or DRAINING only): stock reads
 * each option row's text slot while it builds the page, after this callback and before the
 * page's entry handler, so a label stored later is drawn only at the next opening. */
/* The quality row: the page of the mode, always with Reset last; Reset alone without a mode or
 * outside LIVE/DRAINING (where every pick refuses). */
void z63_g2_open_quality(struct nav *n) {
    (void)n;
    u64 p=z63g_page();
    u32 live2=p&&z63g_phase(p,G2)?1u:0u,m=live2?mode(p):M_NONE;
    u64 el=(u64)(uintptr_t)g2_elements;
    if(m==M_H265)descriptor(el,ITEMS_QB,(u64)(uintptr_t)g2_events_qb,6u);
    else if(m==M_NONE)descriptor(el,ITEMS_RESET,(u64)(uintptr_t)&g2_events_qa[4],1u);
    else descriptor(el,ITEMS_QA,(u64)(uintptr_t)g2_events_qa,5u);
    if(live2)quality_options(p,m);
}
void z63_g2_open_desqueeze(struct nav *n) {
    (void)n;
    u64 p=z63g_page();
    descriptor((u64)(uintptr_t)g2_elements,ITEMS_DQ,(u64)(uintptr_t)g2_events_dq,8u);
    if(!p||!z63g_phase(p,G2))return;
    TEXT(p,dq_labels[0],S_DQ_0);TEXT(p,dq_labels[1],S_DQ_0+1u);TEXT(p,dq_labels[2],S_DQ_0+2u);TEXT(p,dq_labels[3],S_DQ_0+3u);
    TEXT(p,dq_labels[4],S_DQ_0+4u);TEXT(p,dq_labels[5],S_DQ_0+5u);TEXT(p,dq_labels[6],S_DQ_0+6u);TEXT(p,dq_labels[7],S_DQ_0+7u);
}
void z63_g2_open_log(struct nav *n) {
    (void)n;
    u64 p=z63g_page();
    descriptor((u64)(uintptr_t)g2_elements,ITEMS_LG,(u64)(uintptr_t)g2_events_lg,3u);
    if(!p||!z63g_phase(p,G2))return;
    TEXT(p,lg_labels[0],S_LG_0);TEXT(p,lg_labels[1],S_LG_0+1u);TEXT(p,lg_labels[2],S_LG_0+2u);
}
/* Parent guards of the desqueeze and log rows: open only while the owner group is live. */
u32 z63_g2_guard_desqueeze(void){u64 p=z63g_page();return live(p,G7);}
u32 z63_g2_guard_desqueeze_parent(void){u64 p=z63g_page();return live(p,G7)&&stock_parent_guard();}
u32 z63_g2_guard_log(void){u64 p=z63g_page();return live(p,G8);}
u32 z63_g2_guard_log_parent(void){u64 p=z63g_page();return live(p,G8)&&stock_parent_guard();}

/* The row picked on a G2 page: the window is on the sub-page, the descriptor holds `items` with
 * `count` rows, and the row under the cursor carries the expected item id. ~0u otherwise. */
static u32 picked(struct nav *n,u64 items,u32 count,const u32 *ids) {
    if(!n||!n->window)return ~0u;
    u64 w=(u64)(uintptr_t)n->window;
    if(z63g_ld64(w+0x50)!=SUB_PAGE||z63g_ld64(SUB_ITEMS)!=items||*(volatile u16 *)(uintptr_t)SUB_COUNT!=count)return ~0u;
    u32 row=(u32)*(volatile u16 *)(uintptr_t)(w+0x32)+(u32)*(volatile u16 *)(uintptr_t)(w+0x58);
    if(row>=count||z63g_ld32(items+row*44u+24u)!=ids[row])return ~0u;
    return row;
}
/* Outcome of a pick: nothing (not a pick of ours, or nothing to change), saved, or refused /
 * failed (shown as "Save failed" on the row's value). */
enum {PICK_NONE=0,PICK_SAVED=1,PICK_FAILED=2};
/* `next` over the stored `old` with `field` (0 prores, 1 h265, 2 nraw, 3 desqueeze, 4 log) set
 * to `value`, saved for `owner` (TX0). Separate from the texts to keep the stack bound. */
__attribute__((noinline)) static u32 save_field(u64 p,u32 owner,u32 field,u32 value) {
    u32 old=z63g_acq32(Z63S_WORD),next=0;
    struct z63_feature_settings v;
    if(z63_settings_decode(old,&v)==Z63_RECORD_FOREIGN)return PICK_FAILED;
    if(field==0u)v.prores=value;
    else if(field==1u)v.h265=value;
    else if(field==2u)v.nraw=value;
    else if(field==3u)v.desqueeze=value;
    else v.log_colour=value;
    if(!z63_settings_encode(old,&v,&next))return PICK_FAILED;
    if(field==2u) {                              /* an explicit N-RAW tier makes the record v3 */
        u32 payload=((next>>8)&0x1fffu)|0xc000u;  /* the check term (open-gate binding) is kept */
        next=(next&255u)|payload<<8|(settings_crc(payload)^settings_term(next))<<24;
    }
    return z63s_commit(p,owner,old,next)==Z63S_SAVED?PICK_SAVED:PICK_FAILED;
}
/* The pick's texts: values again after a save, "Save failed" after a refusal. */
static void outcome(u64 p,u32 r,u32 slot) {
    if(r==PICK_SAVED)row_texts(p);
    else if(r==PICK_FAILED)TEXT(p,save_failed,slot);
}
/* The quality pick as (owner << 16 | field << 8 | row), or PICK_NONE / PICK_FAILED; the entry
 * then saves it (one call level, for the stack bound: settings_encode alone takes 128 B). */
#define SAVE(owner,field,row) ((owner)<<16|(field)<<8|(row))
__attribute__((noinline)) static u32 pick_quality(u64 p,struct nav *n) {
    u32 m=mode(p),row=m==M_H265?picked(n,ITEMS_QB,6u,ids_quality):m!=M_NONE?picked(n,ITEMS_QA,5u,ids_quality):~0u;
    if(row>=(m==M_H265?5u:4u))return PICK_NONE;
    if(m==M_PRORES)return SAVE(G4,0u,row);
    if(m==M_H265)return SAVE(G5,1u,row);
    /* N-RAW: a tier inside Nikon's class only (G2 never calls the stock setter). */
    u32 high=z63g_ld32(Z63_SLOT_KEY_0F55);
    if(high>1u||z63g_ld32(Z63_SLOT_KEY_201B)!=high||(row<2u?1u:0u)!=high)return PICK_FAILED;
    return SAVE(G6,2u,row);
}
void z63_g2_pick_quality(struct nav *n) {
    u64 p=z63g_page();
    if(!p||z63g_phase(p,G2)!=Z63_LIVE)return;
    u32 r=pick_quality(p,n);
    if(r>>16)r=save_field(p,r>>16,(r>>8)&255u,r&255u);
    outcome(p,r,S_Q_VALUE);
}
/* "Reset patch settings": bits 8-31 cleared through the same save; Nikon's low byte
 * kept. A record the decoder does not accept (foreign) is left alone. */
__attribute__((noinline)) static u32 pick_reset(u64 p,struct nav *n) {
    if(picked(n,ITEMS_RESET,1u,&ids_quality[5])!=0u&&picked(n,ITEMS_QA,5u,ids_reset_qa)!=4u&&picked(n,ITEMS_QB,6u,ids_reset_qb)!=5u)
        return PICK_NONE;
    u32 old=z63g_acq32(Z63S_WORD);
    struct z63_feature_settings v;
    if(z63_settings_decode(old,&v)==Z63_RECORD_FOREIGN)return PICK_FAILED;
    if(!(old>>8))return PICK_SAVED;              /* already clear: nothing to save, values shown again */
    return z63s_commit(p,G2,old,old&255u)==Z63S_SAVED?PICK_SAVED:PICK_FAILED;
}
void z63_g2_pick_reset(struct nav *n) {
    u64 p=z63g_page();
    if(!p||z63g_phase(p,G2)!=Z63_LIVE)return;
    outcome(p,pick_reset(p,n),S_Q_VALUE);
}
void z63_g2_pick_desqueeze(struct nav *n) {
    u64 p=z63g_page();
    if(!p||z63g_phase(p,G2)!=Z63_LIVE||!live(p,G7))return;
    u32 row=picked(n,ITEMS_DQ,8u,ids_dq);
    if(row>=8u)return;
    outcome(p,save_field(p,G7,3u,row),S_DQ_VALUE);
}
void z63_g2_pick_log(struct nav *n) {
    u64 p=z63g_page();
    if(!p||z63g_phase(p,G2)!=Z63_LIVE||!live(p,G8))return;
    u32 row=picked(n,ITEMS_LG,3u,ids_lg);
    if(row>=3u)return;
    outcome(p,save_field(p,G8,4u,row),S_LG_VALUE);
}

/* ---- the save-result observer (record 0x41003A7C) ---- */
/* The worker's save call, made exactly once with its argument; the result is recorded in the
 * save block only for the generation of a save G2 admitted (save_api.h). Returned unchanged. */
u32 z63_g2_save_result(u32 mode_) {
    u64 p=z63g_page();
    u32 generation=z63s_observe_begin(p);
    u32 result=stock_save_worker(mode_);
    z63s_observe_end(p,generation,result);
    return result;
}
