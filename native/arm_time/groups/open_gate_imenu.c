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
/* G10 part IM: the six [FX 3:2] options in the i-menu frame-size control for N-RAW.
 * Included by open_gate.c after open_gate_entries.c (same translation unit: it
 * uses ENT's save helpers ent_next / ent_commit / ent_save and ent_stored_on). Declared by
 * z63patch/gate_groups/open_gate_imenu.py, which has the stock analysis this design rests on.
 *
 * The on-screen control of the i-menu tile 0xB3 is the horizontal strip, stepped by the
 * sub-command dial (dial 0x40505CC4, strip 0x404802F8). The six options are appended after the
 * 17 regular ones as strip positions 17..22 (6K 30/25/24p, 4K 30/25/24p); position 17 + k
 * is its twin row k -> stock index 2, 3, 4, 7, 8, 9 (the same native size and rate), shown with
 * the twin's strip icon and label. Stock never reads a row past 16: every row the dial applies is
 * a stock row (the twin's), and the [FX 3:2] meaning is carried by the saved choice.
 * The first build's 23-cell LIST (layout, option map, current cell, 12 group slots and the title
 * list) is withdrawn: its six cells were drawn blank and could not be reached (emulator).
 * Only B/BL code words are replaced. Adapters:
 *
 *  strip (built when "extended": G10 and G2 LIVE, file type N-RAW, the strip record as stock
 *  stored it for N-RAW; else the stock call):
 *   g10_im_strip    0x40480568 bl 0x404C5E28(strip object): the record 0xA03482D18 gets count 23,
 *                   the 23-entry icon list of its quality (asset) and the current position (17 + k
 *                   when the stored choice is on for the current pair); count last.
 *   g10_im_slabels  0x40480570 bl 0x404793E4(labels) (S, every phase): the N-RAW label list ->
 *                   the 23-entry list while the record is this part's (count 23 and an asset list).
 *  dial (one call of 0x40505CC4; state OG_DIAL / OG_DIAL_NEW):
 *   g10_im_dcur     0x40505D08 bl 0x40506010(&cur): G10 LIVE or DRAINING, file type 6 (the
 *                   caller's w22 and the slot) and the strip record this part's: the current
 *                   position (17 + k when chosen); OG_DIAL = 0x80 | position. Always rewrites
 *                   OG_DIAL / OG_DIAL_NEW when G10 is not PASS. 3:2 targets need LIVE (im_live).
 *   g10_im_dprev    0x40505EB0 bl 0x40504C14 (S, every phase) previous / next index (start, 17,
 *   g10_im_dnext    0x40505EC4 bl 0x40504C88  N-RAW conditions, cur, wrap): with OG_DIAL for this
 *                   cur and extended, stock's search over 23 entries (asset conditions: 17..22 the
 *                   twins'); a 3:2 result returns its twin index and is recorded in OG_DIAL_NEW;
 *                   not extended (or a cur 17..22 without the state): the regular entries only,
 *                   from the position next to the 3:2 block. Stock's "no move" (the cur) and a
 *                   3:2 result whose twin is the cur return the cur (nothing applied).
 *   g10_im_dapply   0x40505F68 bl 0x403FFCEC (A): LIVE N-RAW: a recorded 3:2 move saves the choice
 *                   bound to the twin's size and rate, any other pick saves open gate off, BEFORE
 *                   stock's write; a pick whose save did not happen writes nothing. One save per
 *                   step onto or off a 3:2 position (Task C's deferred save to the strip's close
 *                   is withdrawn: the item close 0x404FC478 is not on the strip's exit routes,
 *                   emulator; no single strip teardown was found in stock code).
 *  title (Task A):
 *   g10_im_title    0x40486A04 bl 0x404054FC (S, every phase) title widget 0x900 / 0x901 bind:
 *   g10_im_title2   0x40486A98                our type-3 title put back to the label first; LIVE
 *                   and the cursor on 17 + k: the [FX 3:2] text slot 286 + k after the bind.
 *   g10_im_dnew     0x40505F70 bl 0x40506010(&new): with OG_DIAL, the new position is reported so
 *                   that stock's step (mod 17) equals the step over 23 positions; state cleared.
 *  list (17 regular entries; consumers kept, they map 17..22 should such a position reach them):
 *   g10_im_avail    0x40508B8C b 0x403AF5C8 (S): N-RAW condition array, index 17..22 -> the twin's
 *                   (the strip's per-cell availability callback 0x404805AC reaches it).
 *   g10_im_codes    0x404FB2C4 bl 0x403FEF58 (S): N-RAW code tables, index 17..22 -> the twin's.
 *   g10_im_lapply   0x404FB4C8 bl 0x403FFE38: the list's regular pick saves open gate off before
 *                   stock's write; the session start is recorded.
 *   g10_im_rollback 0x403FFEF8 bl 0x410E14C0(0): the session-start choice saved back.
 *
 * State (OG_ENT_BASE range): +0x15 OG_IM_START, +0x16 OG_IM_RESULT, +0x17 OG_IM_TERM, +0x19
 * OG_DIAL, +0x1A OG_DIAL_NEW. No .data record. The strip adapter writes only the stock strip
 * record, which stock rewrites at every strip build; its asset pointers stay valid after a disarm
 * (the asset is part of the image). */

#define IM_ROWS_HIGH   0x42bbb340ull
#define IM_ROWS_NORMAL 0x42bbb4d8ull
#define IM_CONDITIONS  0x43ac81b8ull           /* the N-RAW availability ids (17) */
#define IM_SESSION     0xa03427f34ull          /* u8, 1 inside an i-menu session (set by 0x403FFE38) */
#define IM_ITEM        0xa03488638ull          /* current i-menu item */
#define IM_ITEM_NRAW   0xb7u                   /* N-RAW frame size */
#define IM_KEY_SIZE    0x05000f4eu
#define IM_KEY_RATE    0x05000f50u
#define IM_KEY_QUALITY 0x05000f55u
#define IM_STOCK       17u
#define IM_ROWS        23u
#define IM_STRIP_OBJ   0xa034826f8ull          /* strip object: +0x20 = the record */
#define IM_STRIP_REC   0xa03482d18ull          /* {count, 1, position, byte; 0, icons; 0} */
#define IM_ICONS_HIGH  0xa028c00acull          /* u16[17] N-RAW strip icons, quality 1 */
#define IM_ICONS_NORM  0xa028c00ceull          /* u16[17] otherwise */
#define IM_LABELS      0xa028c00f0ull          /* u32[17] N-RAW strip labels */
/* The derived asset (open_gate_imenu.py OFF_*; the tests compare). */
#define IM_OFF_ICONS_HIGH 0x000u
#define IM_OFF_ICONS_NORM 0x040u
#define IM_OFF_COND       0x080u

u64 stock_im_lapply(u64,u64,u64);                        /* 0x403FFE38 (keys, values, n) */
u64 stock_im_dapply(u64,u64,u64);                        /* 0x403FFCEC (keys, values, n: the site sets only these) */
u64 stock_im_session(u64);                               /* 0x410E14C0 (w0 0 = rollback; the only argument) */
u64 stock_im_match(u64,u64,u64,u64,u64,u64,u64,u64);     /* 0x40506010 (&index): the dial's matcher */
u64 stock_im_prev(u64,u64,u64,u64,u64);                  /* 0x40504C14 (start, count, ids, cur, wrap) */
u64 stock_im_next(u64,u64,u64,u64,u64);                  /* 0x40504C88 */
u64 stock_im_strip(u64,u64,u64,u64,u64,u64,u64,u64);     /* 0x404C5E28 (strip object) */
u64 stock_im_bind(u64,u64);                              /* 0x404054FC (widget, label) */
extern const unsigned char z63_derived_401f4000[];
#define IM_D(off) ((u64)(uintptr_t)(z63_derived_401f4000+(off)))

/* ---- the C asset: the 23 strip labels (stock 2238 + i, then the twins': the [FX 3:2] text
 * is not shown; a label is a resource number, not a text slot) ---- */
#define IM_ASSET __attribute__((section(".z63asset.401f1100"),used,aligned(16)))
IM_ASSET const u32 g10_im_labels[23]={
    2238,2239,2240,2241,2242,2243,2244,2245,2246,2247,2248,2249,2250,2251,2252,2253,2254,
    2240,2241,2242,2245,2246,2247};

static inline u32 im_twin(u32 k) {return k<3u?2u+k:4u+k;}            /* option 17 + k -> stock index */
static inline u32 im_k(u32 index) {return index>=2u&&index<=4u?index-2u:index>=7u&&index<=9u?index-4u:6u;}
static inline u32 im_ld8(u64 a) {return *(volatile const u8 *)(uintptr_t)a;}

/* G10 and G2 LIVE (a choice can be saved) and the file type N-RAW. */
static inline __attribute__((always_inline)) u32 im_live(u64 p) {
    return p&&z63g_phase(p,G10)==Z63_LIVE&&z63g_phase(p,Z63S_G2)==Z63_LIVE&&z63g_ld32(Z63_SLOT_FILE_TYPE)==6u;
}
static inline __attribute__((always_inline)) u32 im_keys(u64 keys,u64 n) {
    return (u32)n==3u&&keys&&z63g_ld32(keys)==IM_KEY_SIZE&&z63g_ld32(keys+4u)==IM_KEY_RATE&&z63g_ld32(keys+8u)==IM_KEY_QUALITY;
}
/* A save result after which the stock write may follow: saved, nothing to change, or (regular
 * pick only) a stored record that is not ours, which cannot say open gate. */
static inline u32 im_write_ok(u32 s,u32 want) {
    return s==Z63S_SAVED||s==ENT_SAME||(!want&&s==ENT_FOREIGN);
}
/* The position of stock index r: 17 + k when r is twin k, the size and rate slots hold its pair
 * and the stored choice is on for it (og_chosen: bound to the current pair). */
static inline __attribute__((always_inline)) u32 im_pair(u32 k) {
    return z63g_ld32(Z63_SLOT_KEY_0F4E)==(k<3u?6u:4u)&&z63g_ld32(Z63_SLOT_KEY_0F50)==6u+k%3u;
}
__attribute__((noinline)) static u32 im_position(u64 p,u32 r) {
    u32 k=im_k(r);
    if(k<6u&&im_pair(k)&&ent_stored_on(p))
        return IM_STOCK+k;
    return r;
}
/* The strip record is this part's: count 23 and one of the asset's icon lists. */
static inline __attribute__((always_inline)) u32 im_strip_ours(void) {
    u64 ic=z63g_ld64(IM_STRIP_REC+0x18u);
    return z63g_ld32(IM_STRIP_REC)==IM_ROWS&&(ic==IM_D(IM_OFF_ICONS_HIGH)||ic==IM_D(IM_OFF_ICONS_NORM));
}
static inline __attribute__((always_inline)) u32 im_state_ok(u64 p) {return p&&z63g_phase(p,G10);}

/* ---- strip ---- */
u64 g10_im_strip(OG_ARGS) {
    u64 p=z63g_page();
    if(a0==IM_STRIP_OBJ&&z63g_ld64(IM_STRIP_OBJ+0x20u)==IM_STRIP_REC&&im_live(p)&&
       z63g_ld32(IM_STRIP_REC)==IM_STOCK&&z63g_ld32(IM_STRIP_REC+4u)==1u) {
        u64 ic=z63g_ld64(IM_STRIP_REC+0x18u);
        u32 cur=z63g_ld32(IM_STRIP_REC+8u);
        u64 to=ic==IM_ICONS_HIGH?IM_D(IM_OFF_ICONS_HIGH):ic==IM_ICONS_NORM?IM_D(IM_OFF_ICONS_NORM):0;
        if(to&&cur<IM_STOCK) {
            *(volatile u32 *)(uintptr_t)(IM_STRIP_REC+8u)=im_position(p,cur);
            *(volatile u64 *)(uintptr_t)(IM_STRIP_REC+0x18u)=to;
            *(volatile u32 *)(uintptr_t)IM_STRIP_REC=IM_ROWS;
        }
    }
    return stock_im_strip(OG_PASS);
}

/* ---- dial ---- */
#define OG_DIAL_ON  0x80u
#define OG_DIAL_DEC 0x40u
/* Reached by a tail b from the stub below (x1 = the caller's w22, its file type). */
__attribute__((used,noinline)) u64 g10_im_dcur_c(u64 at,u64 ft) {
    u64 p=z63g_page();
    u64 r=stock_im_match(at,0,0,0,0,0,0,0);
    if(im_state_ok(p)) {
        og_put8(p,OG_DIAL,0);og_put8(p,OG_DIAL_NEW,0);
        if(at&&(u32)ft==6u&&z63g_ld32(Z63_SLOT_FILE_TYPE)==6u&&im_strip_ours()) {
            u32 c=z63g_ld32(at);
            if(c<IM_STOCK) {
                c=im_position(p,c);
                *(volatile u32 *)(uintptr_t)at=c;
                og_put8(p,OG_DIAL,OG_DIAL_ON|c);
            }
        }
    }
    return r;
}
__asm__(
".text\n"
".p2align 2\n"
".global g10_im_dcur\n"
".type g10_im_dcur,%function\n"
"g10_im_dcur:\n"
OG_LOAD_P
"  mov w1,w22\n"
"  b g10_im_dcur_c\n"
".size g10_im_dcur,.-g10_im_dcur\n"
);
/* Previous (dec) / next index. Stock's search (start, count, ids, cur, wrap) returns the first
 * selectable index from start in its direction, or cur. */
static inline __attribute__((always_inline)) u64 im_search(u32 dec,u64 a0,u64 a1,u64 a2,u64 a3,u64 a4) {
    return dec?stock_im_prev(a0,a1,a2,a3,a4):stock_im_next(a0,a1,a2,a3,a4);
}
__attribute__((noinline)) static u64 im_step(u64 p,u32 dec,u64 a0,u64 a1,u64 a2,u64 a3,u64 a4) {
    u32 cur=(u32)a3;
    if(a2!=IM_CONDITIONS||(u32)a1!=IM_STOCK)return im_search(dec,a0,a1,a2,a3,a4);
    u32 st=im_state_ok(p)?og_st(p)[OG_DIAL]:0u;
    u32 on=(st&OG_DIAL_ON)&&(st&31u)==cur&&cur<IM_ROWS;
    u32 r;
    if(on&&im_live(p)) {
        u32 v=(u32)im_search(dec,a0,IM_ROWS,IM_D(IM_OFF_COND),a3,a4);
        r=v<IM_STOCK?v:v<IM_ROWS?im_twin(v-IM_STOCK):cur;
        if(v>=IM_ROWS||v==cur||r==cur)return cur;      /* no move: the matcher then reports cur */
        og_put8(p,OG_DIAL,st|(dec?OG_DIAL_DEC:0u));og_put8(p,OG_DIAL_NEW,OG_DIAL_ON|v);
        return r;
    }
    /* the regular entries only; a cur 17..22 starts next to the 3:2 block (16 back, 17 = wrap on) */
    if(cur>=IM_STOCK&&cur<IM_ROWS)a0=dec?IM_STOCK-1u:IM_STOCK;
    r=(u32)im_search(dec,a0,a1,a2,a3,a4);
    if(r>=IM_STOCK&&r!=cur)r=cur;                       /* stock's search never returns past 16 */
    if(on&&r!=cur) {og_put8(p,OG_DIAL,st|(dec?OG_DIAL_DEC:0u));og_put8(p,OG_DIAL_NEW,OG_DIAL_ON|r);}
    return r;
}
u64 g10_im_dprev(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4) {u64 p=z63g_page();return im_step(p,1u,a0,a1,a2,a3,a4);}
u64 g10_im_dnext(u64 a0,u64 a1,u64 a2,u64 a3,u64 a4) {u64 p=z63g_page();return im_step(p,0u,a0,a1,a2,a3,a4);}
/* The new position after the apply; with OG_DIAL, reported as cur -/+ d (d = the step over 23
 * positions in the dial's direction) so that stock's step (computed mod 17) is d. */
u64 g10_im_dnew(OG_ARGS) {
    u64 p=z63g_page();
    u64 r=stock_im_match(OG_PASS);
    if(!im_state_ok(p))return r;
    u32 st=og_st(p)[OG_DIAL],nw=og_st(p)[OG_DIAL_NEW];
    if(!(st&OG_DIAL_ON))return r;
    og_put8(p,OG_DIAL,0);og_put8(p,OG_DIAL_NEW,0);
    if(!a0)return r;
    u32 c=z63g_ld32(a0);
    if(c>=IM_STOCK)return r;
    u32 cur=st&31u,a=im_position(p,c),out=a;
    if((nw&OG_DIAL_ON)&&a==(nw&31u)&&a!=cur&&cur<IM_ROWS) {
        u32 d=st&OG_DIAL_DEC?(cur+IM_ROWS-a)%IM_ROWS:(a+IM_ROWS-cur)%IM_ROWS;
        out=st&OG_DIAL_DEC?cur-d:cur+d;
    }
    *(volatile u32 *)(uintptr_t)a0=out;
    return r;
}

/* ---- list apply ---- */
/* The session start (first apply of a session): the stored choice as shown, and its term. */
static inline __attribute__((always_inline)) void im_start(u64 p) {
    u32 old=z63g_acq32(Z63S_WORD),on=og_chosen(p,old);
    og_put8(p,OG_IM_TERM,on?og_term_of(old):0u);
    og_put8(p,OG_IM_START,2u|on);
}
/* Reached from g10_im_lapply by a tail b (x30 = the stock caller's return), so its frame is the
 * only one above the save (verifier bound 256 B with ENT's commit frame). A position 17..22 has
 * the twin row's size, rate and quality written over the caller's values (its frame at the BL;
 * the caller only returns afterwards), in every phase, before anything else. */
__attribute__((used,noinline)) u64 g10_im_lapply_c(u64 keys,u64 values,u64 n,u64 position,u64 rows) {
    u64 p=z63g_page();
    u32 i=(u32)(int32_t)(int16_t)position-IM_STOCK;      /* sxth, as stock indexes the row */
    u32 ext=i<6u&&(rows==IM_ROWS_HIGH||rows==IM_ROWS_NORMAL)&&values!=0u;
    if(ext) {
        u64 row=rows+24u*im_twin(i);
        volatile u32 *v=(volatile u32 *)(uintptr_t)values;
        v[0]=z63g_ld32(row);v[1]=z63g_ld32(row+4u);v[2]=z63g_ld32(row+12u);
    }
    if(p&&z63g_phase(p,G10)==Z63_LIVE&&im_keys(keys,n)) {
        if(!im_live(p)) {
            if(ext)return 0;                             /* no save possible: a 3:2 pick writes nothing */
        } else {
            if(!im_ld8(IM_SESSION))im_start(p);
            u32 s=ent_save(p,ext,z63g_ld32(values),z63g_ld32(values+4u));
            og_put8(p,OG_IM_RESULT,s);
            if(!im_write_ok(s,ext))return 0;
            if(ext)og_pick_marks(p);
        }
    }
    return stock_im_lapply(keys,values,n);
}
/* x21 (list position) and x24 (row table) are the caller's callee-saved registers at the BL. */
__asm__(
".text\n"
".p2align 2\n"
".global g10_im_lapply\n"
".type g10_im_lapply,%function\n"
"g10_im_lapply:\n"
OG_LOAD_P
"  sxth x3,w21\n"
"  mov x4,x24\n"
"  b g10_im_lapply_c\n"
".size g10_im_lapply,.-g10_im_lapply\n"
);


/* ---- dial apply ---- */
/* A move recorded by g10_im_dprev / g10_im_dnext onto 17 + k (the values are twin k's row) saves
 * the choice bound to that pair; any other LIVE N-RAW dial pick saves open gate off. */
u64 g10_im_dapply(u64 a0,u64 a1,u64 a2) {
    u64 p=z63g_page();
    u32 nw=im_state_ok(p)?og_st(p)[OG_DIAL_NEW]:0u,v=nw&31u;
    u32 ext=(nw&OG_DIAL_ON)&&v>=IM_STOCK&&v<IM_ROWS;
    if(ext) {
        u32 k=v-IM_STOCK;
        if(!im_live(p)||!im_keys(a0,a2)||z63g_ld32(a1)!=(k<3u?6u:4u)||z63g_ld32(a1+4u)!=6u+k%3u) {
            og_put8(p,OG_DIAL_NEW,0);
            return 0;                                    /* a 3:2 move that cannot be saved writes nothing */
        }
    }
    if(im_live(p)&&im_keys(a0,a2)) {
        u32 s=ent_save(p,ext,z63g_ld32(a1),z63g_ld32(a1+4u));
        og_put8(p,OG_IM_RESULT,s);
        if(!im_write_ok(s,ext)) {
            if(im_state_ok(p))og_put8(p,OG_DIAL_NEW,0);
            return 0;
        }
        if(ext)og_pick_marks(p);
    }
    return stock_im_dapply(a0,a1,a2);
}


/* ---- strip title: the [FX 3:2] text above the strip ---- */
/* The title widgets 0x900 (and 0x901 in mode 2) are pooled text widgets (class teardown
 * 0x4032DED4) re-made by 0x40486B58 at every strip event (type 2, the cursor's label from the
 * list) and bound by 0x4048699C (the strip's label callback 0x40480590 -> b 0x4048699C, cursor =
 * record +8) through bl 0x404054FC(widget, label) at 0x40486A04 / 0x40486A98. Wrapper, every
 * phase: (1) a title widget at type 3 with slot 286..291 (only this wrapper makes one) is put back
 * to type 2 with the caller's label before the stock bind (clear 0x40405598, then 0x40409F44(w, 2,
 * label)), because 0x404054FC keeps the type and would clamp the label to slot 299; (2) the stock
 * bind; (3) G10 and G2 LIVE, file type N-RAW (im_live), the strip record this part's, the label list this part's,
 * the cursor 17 + k and the label its entry: the [FX 3:2] text in slot 286 + k (ENT's strings, the
 * same store as the page's), clear, 0x40409F44(w, 3, slot). A setter that refuses puts the twin's
 * label back (type 2), so a refusal shows the twin's label. Regular positions and every other phase
 * keep the stock label. */
#define IM_TITLE        0x900u
#define IM_LIST_PTR     0xa03483cb8ull
static inline __attribute__((always_inline)) u32 im_title_widget(u64 w) {
    u32 id=w?z63g_ld32(w):0u;
    return id==IM_TITLE||id==IM_TITLE+1u;
}
__attribute__((noinline)) static void im_title_stock(u64 w,u32 label) {
    if(z63g_ld32(w+0xcu)!=2u)stock_ent_clear((void *)(uintptr_t)w);
    stock_ent_set_value((void *)(uintptr_t)w,2u,label);
}
__attribute__((noinline)) static u32 im_title_text(u64 w,u32 k) {
    if(k==0u)ENT_STORE(286u,ent_l0);
    else if(k==1u)ENT_STORE(287u,ent_l1);
    else if(k==2u)ENT_STORE(288u,ent_l2);
    else if(k==3u)ENT_STORE(289u,ent_l3);
    else if(k==4u)ENT_STORE(290u,ent_l4);
    else ENT_STORE(291u,ent_l5);
    stock_ent_clear((void *)(uintptr_t)w);
    return stock_ent_set_value((void *)(uintptr_t)w,3u,286u+k)&1u;
}
__attribute__((noinline)) static u64 im_title(u64 p,u64 w,u64 label) {
    u32 t=im_title_widget(w);
    if(t&&z63g_ld32(w+0xcu)==3u&&z63g_ld32(w+8u)-286u<6u)im_title_stock(w,(u32)label);
    u64 r=stock_im_bind(w,label);
    if(t&&im_live(p)&&im_strip_ours()&&
       z63g_ld64(IM_LIST_PTR)==(u64)(uintptr_t)g10_im_labels) {
        u32 c=z63g_ld32(IM_STRIP_REC+8u);
        if(c>=IM_STOCK&&c<IM_ROWS&&(u32)label==g10_im_labels[c]&&
           z63g_ld32(w+0xcu)==2u&&z63g_ld32(w+8u)==(u32)label&&!im_title_text(w,c-IM_STOCK))
            im_title_stock(w,(u32)label);
    }
    return r;
}
u64 g10_im_title(u64 a0,u64 a1) {u64 p=z63g_page();return im_title(p,a0,a1);}
u64 g10_im_title2(u64 a0,u64 a1) {u64 p=z63g_page();return im_title(p,a0,a1);}

/* ---- rollback: the session-start choice back ---- */
/* The stored record with open gate = want, bound with `term` when want (the start record's own
 * term: stock re-applies the start pair right after). Same frame split as ent_next. */
__attribute__((noinline)) static u64 im_next(u32 old,u32 want,u32 term) {
    u32 next=0;
    struct z63_feature_settings v;
    if(z63_settings_decode(old,&v)==Z63_RECORD_FOREIGN)return 1ull<<32;
    v.open_gate=want;
    if(!settings_encode_term(old,&v,term,&next))return 1ull<<32;
    return next;
}
u64 g10_im_rollback(u64 a0) {
    u64 p=z63g_page();
    if(im_live(p)&&z63g_ld32(IM_ITEM)==IM_ITEM_NRAW&&im_ld8(IM_SESSION)==1u) {
        u32 st=og_st(p)[OG_IM_START];
        if(st&2u) {
            u32 want=st&1u,old=z63g_acq32(Z63S_WORD);
            u64 next=im_next(old,want,og_st(p)[OG_IM_TERM]);
            u32 s=next>>32?ENT_FOREIGN:(u32)next==old?ENT_SAME:ent_commit(p,old,(u32)next);
            og_put8(p,OG_IM_START,0);
            og_put8(p,OG_IM_RESULT,s);
            if(want&&(s==Z63S_SAVED||s==ENT_SAME))og_pick_marks(p);
        }
    }
    return stock_im_session(a0);
}

/* ---- consumers, S class (no page: pure maps of indices stock never produces) ---- */
__asm__(
".text\n"
".p2align 2\n"
/* availability: x8 = condition array, x9 = sxth index, w0 = the id read (past the array for
 * 17..22); N-RAW 0x43AC81B8 and 17..22 -> w0 = the twin's id. Then the stock tail. */
".global g10_im_avail\n"
".type g10_im_avail,%function\n"
"g10_im_avail:\n"
"  movz x10,#0x81b8\n"
"  movk x10,#0x43ac,lsl #16\n"
"  cmp x8,x10\n"
"  b.ne 1f\n"
"  sub x11,x9,#17\n"
"  cmp x11,#5\n"
"  b.hi 1f\n"
"  add x12,x11,#2\n"
"  add x13,x11,#4\n"
"  cmp x11,#3\n"
"  csel x12,x12,x13,lo\n"
"  ldr w0,[x8,x12,lsl #2]\n"
"1: b stock_im_avail\n"
".size g10_im_avail,.-g10_im_avail\n"
/* code search (x0 table, w1 count, w2 index, x3 out): N-RAW tables, count 17, index 17..22 ->
 * the twin's index (the stock search then finds the twin's code). */
".p2align 2\n"
".global g10_im_codes\n"
".type g10_im_codes,%function\n"
"g10_im_codes:\n"
"  movz x9,#0xb8b0\n"
"  movk x9,#0x42bb,lsl #16\n"
"  cmp x0,x9\n"
"  b.eq 1f\n"
"  movz x9,#0xb938\n"
"  movk x9,#0x42bb,lsl #16\n"
"  cmp x0,x9\n"
"  b.ne 2f\n"
"1: cmp w1,#17\n"
"  b.ne 2f\n"
"  sub w9,w2,#17\n"
"  cmp w9,#5\n"
"  b.hi 2f\n"
"  add w10,w9,#2\n"
"  add w11,w9,#4\n"
"  cmp w9,#3\n"
"  csel w2,w10,w11,lo\n"
"2: b stock_im_codes\n"
".size g10_im_codes,.-g10_im_codes\n"
/* strip labels (x0): the N-RAW list -> the 23-entry list while the strip record is this part's
 * (count 23 and an asset icon list), in every phase, so the labels always match the record. */
".p2align 2\n"
".global g10_im_slabels\n"
".type g10_im_slabels,%function\n"
"g10_im_slabels:\n"
"  movz x9,#0x00f0\n"
"  movk x9,#0x028c,lsl #16\n"
"  movk x9,#0xa,lsl #32\n"
"  cmp x0,x9\n"
"  b.ne 2f\n"
"  movz x9,#0x2d18\n"
"  movk x9,#0x0348,lsl #16\n"
"  movk x9,#0xa,lsl #32\n"
"  ldr w10,[x9]\n"
"  cmp w10,#23\n"
"  b.ne 2f\n"
"  ldr x10,[x9,#0x18]\n"
"  adrp x11,z63_derived_401f4000\n"
"  add x11,x11,:lo12:z63_derived_401f4000\n"
"  cmp x10,x11\n"
"  add x11,x11,#0x40\n"
"  ccmp x10,x11,#4,ne\n"
"  b.ne 2f\n"
"  adrp x0,g10_im_labels\n"
"  add x0,x0,:lo12:g10_im_labels\n"
"2: b stock_im_labels\n"
".size g10_im_slabels,.-g10_im_slabels\n"
);
