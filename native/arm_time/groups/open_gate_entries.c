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
/* G10 part ENT: the [FX 3:2] entries on the stock Frame size/frame rate page.
 * Included by open_gate.c (same translation unit). Declared by
 * z63patch/gate_groups/open_gate_entries.py, which has the stock analysis this design rests on.
 *
 * The page (object 280, page record 0xA028CB270) already holds the six rows needed: 0x3192,
 * 0x3193, 0x3194 (records: N-RAW, rate 6/7/8, size 6) and 0x3199, 0x319A, 0x319B (rate 6/7/8,
 * size 4). Their mode records equal those of the visible 6K and 4K 30/25/24p rows 0x3203-0x3205
 * and 0x3208-0x320A, their event tables are equal in content, and they sit directly above those
 * rows. Stock hides them at the end of every pass of the page initializer 0x4058F68C with six
 * explicit calls bl 0x403C2F78(ctx, index, 1). So ENT changes no table, count or pointer: the
 * mode table, rows, events and the count 127 stay stock, and the list, selection and summary walk
 * stock data only. Ten BL->BL records, one all-or-none set (ranks in open_gate_entries.py):
 *
 *   g10_ent_lookup  0x405938F0  bl 0x403FEFE0 (selection: row id -> mode record index). LIVE, the
 *                   mode table, an N-RAW record and file type 6: an [FX 3:2] row saves bit 20 = 1,
 *                   any other N-RAW row bit 20 = 0, through z63s_commit (TX0). Returns the stock
 *                   index (the native size and rate are written by stock as for the twin row);
 *                   an [FX 3:2] row whose save did not happen returns -1 (stock's "no record":
 *                   nothing is written; so does one while the file type is not N-RAW or G2 is not
 *                   LIVE, which only a page opened before that change can offer). Every other
 *                   case: the stock result.
 *   g10_ent_cursor  0x4058F824  bl 0x403C0F8C(ctx, id)      cursor scroll to the current row
 *   g10_ent_mark    0x4058F834  bl 0x403C0FFC(ctx, id, 1)   current-row mark
 *                   ENT shown (below) and the STORED word has bit 20: the visible twin id the
 *                   matcher chose (0x3203-5, 0x3208-A) becomes its [FX 3:2] row id. Both ids
 *                   are rows of the stock page, so 0x403C0FFC's unbounded id search ends.
 *   g10_ent_icons   0x4058FD9C  bl 0x4040A1AC(child, icon)  the page's N-RAW icon refresh:
 *   (or _icons_mark)            a VISIBLE [FX 3:2] row's child is bound to its twin's icon
 *   g10_ent_show    0x4058F930, F94C, F968, F9F4, FA10, FA2C   bl 0x403C2F78(ctx, index, 1)
 *   (or _show_mark) ENT shown: state 0 (visible) for the [FX 3:2] row, after its image child
 *                   0x4DC0 and its row widget are set (variant 'slot': twin's icon and the
 *                   [FX 3:2] text slot 286-291; variant 'mark': marked icon and "FX"; see ent_slot).
 *
 * "ENT shown": G10 and G2 LIVE (a choice can be saved), the context is object 280 on the stock
 * page record (rows 0x43CB628C, count 127), and the file type slot reads 6 (N-RAW). Otherwise
 * every adapter makes exactly the stock call with the caller's registers (PASS, DRAINING, any
 * other page state): the page is the stock page and a stored bit 20 is ignored. Phase is read on
 * every call; nothing is cached. No .data, no table, no stored page state: a standby wake that
 * resets .data leaves nothing of ENT to repair, and the restore needs no sanitiser (the six rows'
 * state bytes at 0xA03415A48 + id - 0x298A are rewritten to 1 by stock at the next pass; 0 is
 * also their stock value before the page's first pass after boot).
 *
 * State: +0x12 (OG_ENT_BASE) u8, the result of the last selection save (diagnostic only; nothing
 * reads it). */
#include "../save_api.h"

#define ENT_PAGE_RECORD 0xa028cb270ull         /* object 280's page record */
#define ENT_ROWS        0x43cb628cull          /* its stock rows (44 B, id at +0x18) */
#define ENT_COUNT       127u
#define ENT_TABLE       0x42bd02dcull          /* the mode table: {id, type, rate, size, extra, icon} */
#define ENT_CHILD       0x4dc0u                /* a row's image child (0x403BA900; found with 0x4052C900) */
#define ENT_NONE        0xffffffffull          /* 0x403FEFE0's "not found" (w4 = -1 at the site) */
#define ENT_SAME        5u                     /* save results beyond Z63S_*: nothing to change */
#define ENT_FOREIGN     6u                     /*   the stored record is not one we may re-encode */

u64 stock_ent_lookup(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x403FEFE0 table lookup */
u64 stock_ent_cursor(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x403C0F8C */
u64 stock_ent_mark(u64,u64,u64,u64,u64,u64,u64,u64);      /* 0x403C0FFC */
u64 stock_ent_row_state(u64,u64,u64,u64,u64,u64,u64,u64); /* 0x403C2F78 */
void *stock_ent_row_child(void *ctx,u32 id,u32 child);    /* 0x4052C900 */
void stock_ent_text_store(const char *text,u32 slot,u32 style); /* 0x404066C8 */
u32 stock_ent_set_value(void *widget,u32 type,u32 value); /* 0x40409F44 -> 0x40331D04: 1 = set */
void *stock_ent_find(u32 id,void *list);                  /* 0x403ADA94: the widget of an id */
void stock_ent_clear(void *widget);                       /* 0x40405598 -> 0x4032ACC0 */

#define ENT_ASSET __attribute__((section(".z63asset.401f1000"),used))
ENT_ASSET static const char ent_l0[]="[FX 3:2] 6048x4032;  30p";
ENT_ASSET static const char ent_l1[]="[FX 3:2] 6048x4032;  25p";
ENT_ASSET static const char ent_l2[]="[FX 3:2] 6048x4032;  24p";
ENT_ASSET static const char ent_l3[]="[FX 3:2] 4032x2688;  30p";
ENT_ASSET static const char ent_l4[]="[FX 3:2] 4032x2688;  25p";
ENT_ASSET static const char ent_l5[]="[FX 3:2] 4032x2688;  24p";

/* [FX 3:2] row (the hidden twin) 0..5, else 6. */
static inline u32 ent_fx(u32 id) {
    return id==0x3192u?0u:id==0x3193u?1u:id==0x3194u?2u:id==0x3199u?3u:id==0x319au?4u:id==0x319bu?5u:6u;
}
/* Visible ordinary row of the same record 0..5, else 6. */
static inline u32 ent_twin(u32 id) {
    return id==0x3203u?0u:id==0x3204u?1u:id==0x3205u?2u:id==0x3208u?3u:id==0x3209u?4u:id==0x320au?5u:6u;
}
static inline u32 ent_fx_id(u32 k) {return k<3u?0x3192u+k:0x3196u+k;}

static inline u32 ent_page_ok(u64 ctx) {
    return ctx&&z63g_ld64(ctx+0x50u)==ENT_PAGE_RECORD&&z63g_ld64(ENT_PAGE_RECORD+0x10u)==ENT_ROWS&&
           *(volatile const u16 *)(uintptr_t)(ENT_PAGE_RECORD+0x20u)==ENT_COUNT;
}
/* G10 and G2 LIVE, object 280 on the stock page, file type N-RAW. */
static inline __attribute__((always_inline)) u32 ent_shown(u64 p,u64 ctx) {
    return p&&z63g_phase(p,G10)==Z63_LIVE&&z63g_phase(p,Z63S_G2)==Z63_LIVE&&ent_page_ok(ctx)&&
           z63g_ld32(Z63_SLOT_FILE_TYPE)==6u;
}
/* Open gate in the STORED word (what the user chose last; the menus show the stored word, G2 rule):
 * bit 20 bound to the CURRENT size and rate (z63_settings_decode); 0 after any size or rate change. */
__attribute__((noinline)) static u32 ent_stored_on(u64 p) {return og_chosen(p,z63g_acq32(Z63S_WORD));}

/* The row (part ICONS). The list draws a row as its own widget (0x403BA6F8: type 2, the
 * static string row+0, here 0x448 "Disable") and the child 0x4DC0 (0x403BA900: type 1 image, value
 * row+0x14; drawn by 0x40336D74 through the resource reader 0x40339A60). Stock gives the six hidden
 * rows the icons 0x1641/0x1640/0x163F and 0x162F/0x162D/0x162B ("8.3K"/"4.1K" artwork of other
 * bodies) and the label "Disable", so neither is shown. Two variants, chosen at build time by the
 * switch ROW_LABEL in open_gate_entries.py (it names the adapter of the six show records; both
 * adapters are in every build):
 *
 *  g10_ent_show       'slot' (option 2): the image child gets the visible twin's icon
 *                     (0x40409F44(child, 1, icon)) and the row widget shows the [FX 3:2] text slot:
 *                     the slot text is stored (TEXT_STORE), the widget's static string is released
 *                     by the toolkit's clear 0x40405598 -> 0x4032ACC0 (types 2..4: 0x40337910
 *                     destroys and frees the widget's text-layout cache +0x68 and nulls it; then
 *                     type 0) and set to type 3 with the slot (0x40409F44(row, 3, slot): accepted
 *                     from type 0, returns 1). Fields written: row +0xC (2 -> 0 -> 3), +8 (0x448 ->
 *                     slot), +0x68 (cache -> 0, by stock), +0x3C and the parent's +0x3C (dirty);
 *                     child +8 (icon), +0x3C. The draw 0x40337984 makes a new cache from the type
 *                     and value when +0x68 is 0 (the same free-and-remake it does for a dirty
 *                     cache); the row's teardown 0x4032B7C8 treats types 2..4 alike (release +0x68
 *                     if set, type 0, children), so nothing depends on the original type.
 *  g10_ent_show_mark  'mark' (option 3): the row widget keeps type 2 and takes the stock string
 *                     0x31F "FX" (0x40409F44(row, 2, 0x31F)); the image child carries the meaning:
 *                     with G3 LIVE the extension id 0x7019 + k (the twin's icon with a "3:2" mark,
 *                     derived at build time; G3's readers resolve it), else the twin's stock icon.
 *
 * Both act only on the widget they expect: the row found by 0x403ADA94(id, ctx+0x18) (what
 * 0x4052C900 uses) with the row class's teardown at +0x58 and the stock label (or the variant's own
 * value from an earlier pass of the same page); the child via 0x4052C900. Every check comes before
 * the first change, every setter must return 1, and any refusal keeps the row hidden (stock). The
 * values live on the widgets: the rows are built once when the page opens and shown only here; a
 * later pass hides them again (stock) unless ENT is shown. Each slot is a constant next to its
 * store (verifier text-slot rule); the distinct empty asm keeps the calls from being merged. */
#define ENT_ROW_CLASS   0x4032b7c8ull          /* the row widget's +0x58 method (its teardown) */
#define ENT_STOCK_LABEL 0x448u                 /* row+0 of the six rows: "Disable" */
#define ENT_FX_LABEL    0x31fu                 /* "FX" (the same in all 29 languages) */
#define ENT_MARK_ICON   0x7019u                /* G3 extension ids 0x7019..0x701E (variant 'mark') */
#define G3_GROUP        3u

/* The icon the visible twin shows. For N-RAW the page's icon refresh 0x4058FD24 (vtable
 * 0xA028F5900 +0x20, object 280 only; it runs after the list routine) re-binds every N-RAW row's
 * child from table 0x42BD1094 {id, icon when 0x05000F55 reads 1, icon otherwise}: the twins show
 * 0x182F/0x1830 .. 0x183D/0x183E there, not their row+0x14. Each hidden row's entry sits five
 * entries (0x3C B) before its twin's: rows 0x3192-4 entries 2-4, twins 7-9; 0x3199-B 14-16,
 * twins 19-21. The quality is read from its setting slot (setting_slots.h), as stock's getter. */
#define ENT_ICONS       0x42bd1094ull
#define ENT_ICONS_TWIN  0x3cu
static inline u64 ent_icon_entry(u32 k) {return ENT_ICONS+12u*(k<3u?2u+k:11u+k);}
static inline u32 ent_icon_off(void) {return z63g_ld32(Z63_SLOT_KEY_0F55)==1u?4u:8u;}
static inline u32 ent_twin_icon(u32 k) {return z63g_ld32(ent_icon_entry(k)+ENT_ICONS_TWIN+ent_icon_off());}
/* The row widget of `id`, if it is one (row class). */
static inline __attribute__((always_inline)) u64 ent_row(u64 ctx,u32 id) {
    u64 r=(u64)(uintptr_t)stock_ent_find(id,(void *)(uintptr_t)(ctx+0x18u));
    if(!r||z63g_ld64(r+0x58u)!=ENT_ROW_CLASS)return 0;
    return r;
}
#define ENT_STORE(n,label) do{stock_ent_text_store(label,n,4u);__asm__ volatile("// ent slot " #n);}while(0)
__attribute__((noinline)) static u32 ent_slot(u64 ctx,u32 id,u32 k) {
    void *c=stock_ent_row_child((void *)(uintptr_t)ctx,id,ENT_CHILD);
    if(!c)return 0u;
    u64 r=ent_row(ctx,id);
    if(!r)return 0u;
    u32 slot=286u+k,t=z63g_ld32(r+0xcu),v=z63g_ld32(r+8u);
    if(!(t==2u&&v==ENT_STOCK_LABEL)&&!(t==3u&&v==slot))return 0u;
    if(k==0u)ENT_STORE(286u,ent_l0);
    else if(k==1u)ENT_STORE(287u,ent_l1);
    else if(k==2u)ENT_STORE(288u,ent_l2);
    else if(k==3u)ENT_STORE(289u,ent_l3);
    else if(k==4u)ENT_STORE(290u,ent_l4);
    else ENT_STORE(291u,ent_l5);
    if(!(stock_ent_set_value(c,1u,ent_twin_icon(k))&1u))return 0u;
    if(t==2u)stock_ent_clear((void *)(uintptr_t)r);
    return stock_ent_set_value((void *)(uintptr_t)r,3u,slot)&1u;
}
__attribute__((noinline)) static u32 ent_mark(u64 p,u64 ctx,u32 id,u32 k) {
    void *c=stock_ent_row_child((void *)(uintptr_t)ctx,id,ENT_CHILD);
    if(!c)return 0u;
    u64 r=ent_row(ctx,id);
    if(!r)return 0u;
    u32 v=z63g_ld32(r+8u);
    if(z63g_ld32(r+0xcu)!=2u||(v!=ENT_STOCK_LABEL&&v!=ENT_FX_LABEL))return 0u;
    u32 icon=z63g_phase(p,G3_GROUP)==Z63_LIVE?ENT_MARK_ICON+k:ent_twin_icon(k);
    if(!(stock_ent_set_value(c,1u,icon)&1u))return 0u;
    return stock_ent_set_value((void *)(uintptr_t)r,2u,ENT_FX_LABEL)&1u;
}

/* The six forced-hide calls: an [FX 3:2] row is shown (state 0) once its widgets are set; otherwise
 * it stays hidden (stock), so it is never visible with the other body's icon or "Disable". */
static inline __attribute__((always_inline)) u32 ent_hidden_row(u64 p,u64 ctx,u64 index,u64 state,u32 *id) {
    if((u32)state!=1u||!ent_shown(p,ctx)||(u32)index>=ENT_COUNT)return 6u;
    *id=z63g_ld32(ENT_ROWS+44u*(u32)index+0x18u);
    return ent_fx(*id);
}
u64 g10_ent_show(OG_ARGS) {
    u64 p=z63g_page();
    u32 id=0,k=ent_hidden_row(p,a0,a1,a2,&id);
    if(k<6u&&ent_slot(a0,id,k))a2=0;
    return stock_ent_row_state(OG_PASS);
}
u64 g10_ent_show_mark(OG_ARGS) {
    u64 p=z63g_page();
    u32 id=0,k=ent_hidden_row(p,a0,a1,a2,&id);
    if(k<6u&&ent_mark(p,a0,id,k))a2=0;
    return stock_ent_row_state(OG_PASS);
}
/* The icon refresh's bind . 0x4058FD24 binds the stock icon of each N-RAW row's child
 * through `bl TEXT_BIND 0x4040A1AC` at 0x4058FD9C, after the list routine has shown the rows, so
 * without this the six rows draw the foreign 8.3K / 4.1K artwork again (emulator).
 * At the bind x24 is the table entry, x19 the page window and x20 the quality (callee-saved). A
 * hidden row's own entry (exact address), its child, G10 LIVE or DRAINING, object 280 on the stock
 * page record, file type 6, the row VISIBLE (stock's hidden byte 0xA03415A48 + id - 0x298A is 0,
 * written by 0x403C2F78) and w1 equal to that entry's stock icon for one quality: w1 becomes the
 * twin entry's icon for the same quality ('mark': 0x7019 + k while G3 is LIVE). Anything else,
 * PASS included, binds exactly what stock passed; a hidden row is never touched. No store. */
#define ENT_ROW_HIDDEN  0xa03415a48ull
u64 stock_ent_text_bind(u64 widget,u64 icon);              /* 0x4040A1AC (reads x0, w1 only) */
static inline u32 ent_icon_k(u64 e) {
    return e==ent_icon_entry(0)?0u:e==ent_icon_entry(1)?1u:e==ent_icon_entry(2)?2u:
           e==ent_icon_entry(3)?3u:e==ent_icon_entry(4)?4u:e==ent_icon_entry(5)?5u:6u;
}
__attribute__((used,noinline)) u64 g10_ent_icons_c(u64 child,u64 icon,u64 entry,u64 ctx,u64 mark) {
    u64 p=z63g_page();
    u32 k=ent_icon_k(entry);
    if(k<6u&&child&&p&&z63g_phase(p,G10)&&ent_page_ok(ctx)&&z63g_ld32(Z63_SLOT_FILE_TYPE)==6u&&
       *(volatile const u8 *)(uintptr_t)(ENT_ROW_HIDDEN+ent_fx_id(k)-0x298au)==0u) {
        u32 q=(u32)icon==z63g_ld32(entry+4u)?4u:(u32)icon==z63g_ld32(entry+8u)?8u:0u;
        if(q)icon=mark&&z63g_phase(p,G3_GROUP)==Z63_LIVE?ENT_MARK_ICON+k:z63g_ld32(entry+ENT_ICONS_TWIN+q);
    }
    return stock_ent_text_bind(child,icon);
}
__asm__(
".text\n"
".p2align 2\n"
".global g10_ent_icons\n"
".type g10_ent_icons,%function\n"
"g10_ent_icons:\n"
OG_LOAD_P
"  mov x2,x24\n"
"  mov x3,x19\n"
"  mov x4,#0\n"
"  b g10_ent_icons_c\n"
".size g10_ent_icons,.-g10_ent_icons\n"
".p2align 2\n"
".global g10_ent_icons_mark\n"
".type g10_ent_icons_mark,%function\n"
"g10_ent_icons_mark:\n"
OG_LOAD_P
"  mov x2,x24\n"
"  mov x3,x19\n"
"  mov x4,#1\n"
"  b g10_ent_icons_c\n"
".size g10_ent_icons_mark,.-g10_ent_icons_mark\n"
);

/* Cursor and mark: the current row is the [FX 3:2] row when the stored word has bit 20. */
static inline __attribute__((always_inline)) u64 ent_current(u64 p,u64 ctx,u64 id) {
    if(ent_shown(p,ctx)&&ent_stored_on(p)) {
        u32 k=ent_twin((u32)id);
        if(k<6u)return ent_fx_id(k);
    }
    return id;
}
u64 g10_ent_cursor(OG_ARGS) {
    u64 p=z63g_page();
    a1=ent_current(p,a0,a1);
    return stock_ent_cursor(OG_PASS);
}
u64 g10_ent_mark(OG_ARGS) {
    u64 p=z63g_page();
    a1=ent_current(p,a0,a1);
    return stock_ent_mark(OG_PASS);
}

/* The word with bit 20 = want over `old`, bound to the picked record's native size and rate when
 * want is 1 (: the record's check covers the pair, z63_settings_encode_bound),
 * plain with bit 20 clear when want is 0 (decode and encode in their own frame, so the commit's
 * frame is not stacked on the encoder's: verifier bound 256 B per wrapper path; the wrapper calls
 * ent_next and then ent_commit). Bit 32 set: the stored record is not one we may re-encode. Equal
 * to `old`: nothing to change (compared as whole words: a bit 20 bound to another pair, or a
 * an older plain bit 20 (before the binding), is re-encoded, never taken as "the same"). */
__attribute__((noinline)) static u64 ent_next(u32 old,u32 want,u32 size,u32 rate) {
    u32 next=0;
    struct z63_feature_settings v;
    if(z63_settings_decode(old,&v)==Z63_RECORD_FOREIGN)return 1ull<<32;
    v.open_gate=want;
    if(!z63_settings_encode_bound(old,&v,size,rate,&next))return 1ull<<32;
    return next;
}
/* The commit in its own frame, beside (not below) the encoder's. */
__attribute__((noinline)) static u32 ent_commit(u64 p,u32 old,u32 next) {return z63s_commit(p,G10,old,next);}
static inline __attribute__((always_inline)) u32 ent_save(u64 p,u32 want,u32 size,u32 rate) {
    u32 old=z63g_acq32(Z63S_WORD);
    u64 next=ent_next(old,want,size,rate);
    if(next>>32)return ENT_FOREIGN;
    if((u32)next==old)return ENT_SAME;
    return ent_commit(p,old,(u32)next);
}
/* Selection: the lookup once, then (LIVE only) the save of the choice. */
u64 g10_ent_lookup(OG_ARGS) {
    u64 p=z63g_page();
    u64 r=stock_ent_lookup(OG_PASS);
    if(!p||z63g_phase(p,G10)!=Z63_LIVE)return r;
    if(a0!=ENT_TABLE||(u32)a1!=ENT_COUNT||(u32)a2!=24u||(u32)r>=ENT_COUNT)return r;
    if(z63g_ld32(ENT_TABLE+24u*(u32)r+4u)!=6u)return r;          /* N-RAW records only */
    u32 fx=ent_fx((u32)a3)<6u;
    if(z63g_ld32(Z63_SLOT_FILE_TYPE)!=6u||z63g_phase(p,Z63S_G2)!=Z63_LIVE)return fx?ENT_NONE:r;
    /* The record's native size (+12) and rate (+8): what stock writes for this row next. */
    u32 s=ent_save(p,fx,z63g_ld32(ENT_TABLE+24u*(u32)r+12u),z63g_ld32(ENT_TABLE+24u*(u32)r+8u));
    og_put8(p,OG_ENT_BASE,s);
    /* No pick token: the binding in the record's check replaces the design's writer hooks. An
     * [FX 3:2] pick that is saved or already stored drops the stale marks (og_pick_marks). */
    if(fx&&(s==Z63S_SAVED||s==ENT_SAME))og_pick_marks(p);
    if(s==Z63S_SAVED||s==ENT_SAME)return r;
    return fx?ENT_NONE:r;
}
